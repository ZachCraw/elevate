#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <time.h>
#include <signal.h>
#include "car_shared_mem.h"

#define BUFFER_SIZE 1024
#define MAX_DESTINATIONS 10

int server_socket;
car_shared_mem *shared_mem;
char car_name[256];
char lowest_floor[4];
char highest_floor[4];
int delay;
int delay_ms;
int shm_fd;
char shm_name[256];
char local_destination_floors[MAX_DESTINATIONS][4];
int local_destination_count = 0;

void initialize_shared_memory() {
    snprintf(shm_name, sizeof(shm_name), "/car%s", car_name);

    shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        exit(EXIT_FAILURE);
    }

    if (ftruncate(shm_fd, sizeof(car_shared_mem)) == -1) {
        perror("ftruncate");
        exit(EXIT_FAILURE);
    }

    shared_mem = mmap(NULL, sizeof(car_shared_mem), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_mem == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    pthread_mutexattr_t mutex_attr;
    pthread_condattr_t cond_attr;

    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&shared_mem->mutex, &mutex_attr);

    pthread_condattr_init(&cond_attr);
    pthread_condattr_setpshared(&cond_attr, PTHREAD_PROCESS_SHARED);
    pthread_cond_init(&shared_mem->cond, &cond_attr);

    strncpy(shared_mem->current_floor, lowest_floor, sizeof(shared_mem->current_floor));
    strncpy(shared_mem->status, "Closed", sizeof(shared_mem->status));
    shared_mem->open_button = 0;
    shared_mem->close_button = 0;
    shared_mem->emergency_stop = 0;
    shared_mem->individual_service_mode = 0;
    shared_mem->emergency_mode = 0;
}

void signal_handler(int signum) {
    if (signum == SIGINT) {
        munmap(shared_mem, sizeof(car_shared_mem));
        close(shm_fd);
        shm_unlink(shm_name);
        close(server_socket);
        printf("Shared memory unlinked and closed\n");
        exit(EXIT_SUCCESS);
    }
}

void log_message(const char *message) {
    time_t now;
    time(&now);
    char *time_str = ctime(&now);
    time_str[strlen(time_str) - 1] = '\0'; // Remove the newline character from the time string

    // Create a copy of the message to modify
    char message_copy[BUFFER_SIZE];
    strncpy(message_copy, message, BUFFER_SIZE - 1);
    message_copy[BUFFER_SIZE - 1] = '\0'; // Ensure null-termination

    // Remove trailing newline characters from the message
    size_t len = strlen(message_copy);
    if (len > 0 && message_copy[len - 1] == '\n') {
        message_copy[len - 1] = '\0';
    }

    printf("[%s] %s\n", time_str, message_copy);
}

int convert_floor(const char *floor) {
    if (floor[0] == 'B') {
        return -atoi(floor + 1);
    } else {
        return atoi(floor);
    }
}

void format_floor(int floor, char *floor_str, size_t size) {
    if (floor < 0) {
        snprintf(floor_str, size, "B%d", -floor);
    } else {
        snprintf(floor_str, size, "%d", floor);
    }
}

void *receive_commands(void *arg) {
    char buffer[BUFFER_SIZE];
    int bytes_read;

    while ((bytes_read = recv(server_socket, buffer, sizeof(buffer), 0)) > 0) {
        buffer[bytes_read] = '\0';
        printf("Received command: %s\n", buffer);

        if (strncmp(buffer, "FLOOR", 5) == 0) {
            char floor[4];
            sscanf(buffer, "FLOOR %s", floor);
            pthread_mutex_lock(&shared_mem->mutex);
            if (local_destination_count < MAX_DESTINATIONS) {
                strncpy(local_destination_floors[local_destination_count], floor, sizeof(local_destination_floors[local_destination_count]));
                local_destination_count++;
            }
            pthread_cond_broadcast(&shared_mem->cond);
            pthread_mutex_unlock(&shared_mem->mutex);
        }
    }
    return NULL;
}

void connect_to_controller() {
    struct sockaddr_in server_addr;
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(3000);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    connect(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr));

    char message[BUFFER_SIZE];
    snprintf(message, BUFFER_SIZE, "CAR %s %s %s", car_name, lowest_floor, highest_floor);
    send(server_socket, message, strlen(message), 0);
}

void send_status_update() {
    char message[BUFFER_SIZE];
    snprintf(message, BUFFER_SIZE, "STATUS %s %s %s", shared_mem->status, shared_mem->current_floor, shared_mem->destination_floor);
    for (int i = 0; i < local_destination_count; i++) {
        strncat(message, " ", BUFFER_SIZE - strlen(message) - 1);
        strncat(message, local_destination_floors[i], BUFFER_SIZE - strlen(message) - 1);
    }
    send(server_socket, message, strlen(message), 0);
}

void send_individual_service_message() {
    char message[BUFFER_SIZE];
    snprintf(message, BUFFER_SIZE, "SERVICE %s", car_name);
    send(server_socket, message, strlen(message), 0);
    close(server_socket);
}

void send_emergency_message() {
    char message[BUFFER_SIZE];
    snprintf(message, BUFFER_SIZE, "EMERGENCY %s", car_name);
    send(server_socket, message, strlen(message), 0);
    close(server_socket);
}

void *status_update_thread(void *arg) {
    while (1) {
        pthread_mutex_lock(&shared_mem->mutex);
        pthread_cond_wait(&shared_mem->cond, &shared_mem->mutex);
        send_status_update();
        pthread_mutex_unlock(&shared_mem->mutex);
    }
    return NULL;
}

void *move_car(void *arg) {
    int operation_complete = 1;

    while (1) {
        printf("Car %s is at floor %s\n", car_name, shared_mem->current_floor);
        pthread_mutex_lock(&shared_mem->mutex);

        while (local_destination_count == 0 && operation_complete) {
            pthread_cond_wait(&shared_mem->cond, &shared_mem->mutex);
        }

        if (local_destination_count > 0) {
            int current_floor_int = atoi(shared_mem->current_floor);
            int destination_floor_int = atoi(local_destination_floors[0]);

            if (current_floor_int < destination_floor_int) {
                strcpy(shared_mem->status, "Between");
                usleep(delay_ms);
                snprintf(shared_mem->current_floor, sizeof(shared_mem->current_floor), "%d", current_floor_int + 1);
                operation_complete = 0;
            } else if (current_floor_int > destination_floor_int) {
                strcpy(shared_mem->status, "Between");
                usleep(delay_ms);
                snprintf(shared_mem->current_floor, sizeof(shared_mem->current_floor), "%d", current_floor_int - 1);
                operation_complete = 0;
            } else {
                strcpy(shared_mem->status, "Opening");
                printf("Opening doors for car %s\n", car_name);
                usleep(delay_ms);
                if (strcmp(shared_mem->status, "Opening") == 0) {
                    strcpy(shared_mem->status, "Open");
                    printf("Doors for car %s are open\n", car_name);
                    usleep(delay_ms);
                }
                if (strcmp(shared_mem->status, "Open") == 0) {
                    strcpy(shared_mem->status, "Closing");
                    printf("Closing doors for car %s\n", car_name);
                    usleep(delay_ms);
                }
                if (strcmp(shared_mem->status, "Closing") == 0) {
                    strcpy(shared_mem->status, "Closed");
                    printf("Doors for car %s are closed\n", car_name);
                    operation_complete = 1;
                    // Remove the serviced floor from the destination list
                    for (int i = 1; i < local_destination_count; i++) {
                        strncpy(local_destination_floors[i - 1], local_destination_floors[i], sizeof(local_destination_floors[i - 1]));
                    }
                    local_destination_count--;
                }
            }
        }

        pthread_cond_broadcast(&shared_mem->cond);
        pthread_mutex_unlock(&shared_mem->mutex);
    }
    return NULL;
}

void *button_handler(void *arg) {
    while (1) {
        pthread_mutex_lock(&shared_mem->mutex);

        if (pthread_cond_wait(&shared_mem->cond, &shared_mem->mutex) != 0) {
            perror("pthread_cond_wait");
            exit(EXIT_FAILURE);
        } else {
            if (shared_mem->open_button) {
                if (strcmp(shared_mem->status, "Open") == 0) {
                    usleep(delay_ms);
                    strcpy(shared_mem->status, "Closing");
                } else if (strcmp(shared_mem->status, "Closing") == 0 || strcmp(shared_mem->status, "Closed") == 0) {
                    strcpy(shared_mem->status, "Opening");
                }
                shared_mem->open_button = 0;
                pthread_cond_broadcast(&shared_mem->cond); // Broadcast condition
            }

            if (shared_mem->close_button) {
                if (strcmp(shared_mem->status, "Open") == 0) {
                    strcpy(shared_mem->status, "Closing");
                }
                shared_mem->close_button = 0;
                pthread_cond_broadcast(&shared_mem->cond); // Broadcast condition
            }

            pthread_mutex_unlock(&shared_mem->mutex);
        }
        
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s {name} {lowest floor} {highest floor} {delay}\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    strncpy(car_name, argv[1], sizeof(car_name) - 1);
    strncpy(lowest_floor, argv[2], sizeof(lowest_floor) - 1);
    strncpy(highest_floor, argv[3], sizeof(highest_floor) - 1);
    delay = atoi(argv[4]);
    delay_ms = delay * 1000;

    initialize_shared_memory();

    // Initialise mutex and condition variable
    pthread_mutex_init(&shared_mem->mutex, NULL);
    pthread_cond_init(&shared_mem->cond, NULL);

    // Set up signal handler for clean termination
    signal(SIGINT, signal_handler);

    connect_to_controller();

    pthread_t command_thread, status_thread, car_thread, button_thread;
    pthread_create(&command_thread, NULL, receive_commands, NULL);
    pthread_create(&status_thread, NULL, status_update_thread, NULL);
    pthread_create(&car_thread, NULL, move_car, NULL);
    pthread_create(&button_thread, NULL, button_handler, NULL);

    pthread_join(command_thread, NULL);
    pthread_join(status_thread, NULL);
    pthread_join(car_thread, NULL);
    pthread_join(button_thread, NULL);

    // Destroy mutex and condition variable
    pthread_mutex_destroy(&shared_mem->mutex);
    pthread_cond_destroy(&shared_mem->cond);

    return 0;
}