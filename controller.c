#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include "car_shared_mem.h"
#include <stdbool.h>

#define BUFFER_SIZE 1024
#define PORT 3000
#define MAX_QUEUE 50
#define MAX_CARS 10
#define MAX_FLOOR_LEN 4
#define NO_MOVEMENT "NONE"

Car cars[MAX_CARS];
int car_count = 0;
int server_socket;
pthread_mutex_t car_mutex = PTHREAD_MUTEX_INITIALIZER;

struct thread_args {
    int socket;
    char *initial_message;
};

void handle_sigint(int sig);
void log_message(const char *message);
void *handle_car(void *arg);
int can_service_floor(Car *car, const char *floor);
Car *find_available_car(const char *source_floor, const char *destination_floor);
void *handle_call_pad(void *arg);
void start_server();
Car* add_car(const char *car_name, const char *lowest_floor, const char *highest_floor, int socket);
void init_queue(Queue *queue, int capacity);
void addFloorToQueue(Car *car, char floor, Direction dir);
bool canAccessFloor(Car car, int floor);
void processRequest(int sourceFloor, int destFloor, Car cars[], int numCars);
void updateCarDestination(Car *car);

int main() {
    start_server();
    return 0;
}

// Initialize queue
void init_queue(Queue *queue, int capacity) {
    queue->items = malloc(sizeof(QueueItem) * capacity);
    queue->size = 0;
    queue->capacity = capacity;
}

// Add a floor with a direction to the car's queue
void addFloorToQueue(Car *car, char floor, Direction dir) {
    if (car->queue.size < car->queue.capacity) {
        car->queue.items[car->queue.size++] = (QueueItem){floor, dir};
    }
}

void processRequest(int sourceFloor, int destFloor, Car cars[], int numCars) {
    
    Car *car = find_available_car(sourceFloor, destFloor);

    Direction direction = sourceFloor < destFloor ? UP : DOWN;

    // Insert source floor and destination floor based on the direction
    if (sourceFloor == car->current_floor) {
        addFloorToQueue(car, destFloor, direction);
    } else {
        addFloorToQueue(car, sourceFloor, direction);
        addFloorToQueue(car, destFloor, direction);
    }
}

void updateCarDestination(Car *car) {
    pthread_mutex_lock(&car->mutex);
    if (car->queue.size > 0) {
        QueueItem nextStop = car->queue.items[0];
        car->direction = nextStop.dir;
        strcpy(car->current_destination, nextStop.floor);
        // Shift queue after reaching floor
        for (int i = 0; i < car->queue.size - 1; i++) {
            car->queue.items[i] = car->queue.items[i + 1];
        }
        car->queue.size--;
    }
    pthread_mutex_unlock(&car->mutex);
}


Car* add_car(const char *car_name, const char *lowest_floor, const char *highest_floor, int socket) {
    pthread_mutex_lock(&car_mutex);
    Car *car = &cars[car_count++];
    strncpy(car->name, car_name, sizeof(car->name));
    strncpy(car->lowest_floor, lowest_floor, sizeof(car->lowest_floor));
    strncpy(car->highest_floor, highest_floor, sizeof(car->highest_floor));
    strncpy(car->current_floor, lowest_floor, sizeof(car->current_floor)); // Initialize current floor
    strncpy(car->status, "Closed", sizeof(car->status)); // Initialize status
    car->socket = socket;
    pthread_mutex_init(&car->mutex, NULL);
    pthread_cond_init(&car->cond, NULL);
    init_queue(&car->queue, MAX_QUEUE);
    pthread_mutex_unlock(&car_mutex);
    return car;
}

void handle_sigint(int sig) {
    close(server_socket);
    printf("Server closed\n");
    exit(0);
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

void *handle_car(void *arg) {
    struct thread_args *args = (struct thread_args *)arg;
    int car_socket = args->socket;
    char *buffer = args->initial_message;
    free(args);
    
    if (buffer == NULL) {
        perror("No initial message");
        close(car_socket);
        return NULL;
    }

    // Parse car information
    char car_name[256], lowest_floor[4], highest_floor[4];
    if (sscanf(buffer, "CAR %s %s %s", car_name, lowest_floor, highest_floor) != 3) {
        fprintf(stderr, "Error parsing car information: %s\n", buffer);
        free(buffer);
        close(car_socket);
        return NULL;
    }

    // Add car to the list
    Car *car = add_car(car_name, lowest_floor, highest_floor, car_socket);
    free(buffer);

    // Process car commands
    while (1) {
        buffer = receive_msg(car_socket);
        if (!buffer) {
            break;
        }

        // Handle STATUS command
        if (strncmp(buffer, "STATUS", 6) == 0) {
            char status[8], current_floor[4], destination_floor[4];
            if (sscanf(buffer, "STATUS %s %s %s", status, current_floor, destination_floor) != 3) {
                fprintf(stderr, "Error parsing status update: %s\n", buffer);
                free(buffer);
                continue;
            }

            pthread_mutex_lock(&car->mutex);
            
            // Update car status
            strncpy(car->status, status, sizeof(car->status));
            strncpy(car->current_floor, current_floor, sizeof(car->current_floor));
            strncpy(car->current_destination, destination_floor, sizeof(car->current_destination));

            // Handle different car states
            if (strcmp(status, "Closed") == 0 && strcmp(car->current_floor, car->current_destination) == 0) {
                updateCarDestination(car);
            }

            char response[BUFFER_SIZE];
            snprintf(response, BUFFER_SIZE, "FLOOR %s", car->current_destination);
            send_message(car_socket, response);

            pthread_mutex_unlock(&car->mutex);
        }
    }     
    close(car_socket);
    return NULL;
}

int can_service_floor(Car *car, const char *floor) {
    int floor_num = atoi(floor);
    int lowest_floor_num = atoi(car->lowest_floor);
    int highest_floor_num = atoi(car->highest_floor);

    if (floor[0] == 'B') {
        floor_num = -atoi(floor + 1);
    }
    if (car->lowest_floor[0] == 'B') {
        lowest_floor_num = -atoi(car->lowest_floor + 1);
    }
    if (car->highest_floor[0] == 'B') {
        highest_floor_num = -atoi(car->highest_floor + 1);
    }

    return floor_num >= lowest_floor_num && floor_num <= highest_floor_num;
}

Car *find_available_car(const char *source_floor, const char *destination_floor) {
    pthread_mutex_lock(&car_mutex);
    Car *selected_car = NULL;
    int diff_from_call = __INT_MAX__;
    int my_car_diff = diff_from_call;
    for (int i = 0; i < car_count; i++) {
        pthread_mutex_lock(&cars[i].mutex);
        if (strcmp(cars[i].status, "Closed") == 0 && can_service_floor(&cars[i], source_floor) && can_service_floor(&cars[i], destination_floor)) {
            my_car_diff = abs(atoi(source_floor) - atoi(cars[i].current_floor));
            if (my_car_diff < diff_from_call) {
                diff_from_call = my_car_diff;
                selected_car = &cars[i];
            }
        }
        pthread_mutex_unlock(&cars[i].mutex);
    }
    pthread_mutex_unlock(&car_mutex);
    return selected_car;
}

void *handle_call_pad(void *arg) {
    struct thread_args *args = (struct thread_args *)arg;
    int call_pad_socket = args->socket;
    char *buffer = args->initial_message;
    free(args);
    
    if (buffer == NULL) {
        perror("No initial message");
        close(call_pad_socket);
        return NULL;
    }
    // Parse call pad request
    char source_floor[4], destination_floor[4];
    if (sscanf(buffer, "CALL %s %s", source_floor, destination_floor) != 2) {
        fprintf(stderr, "Error parsing call pad request: %s\n", buffer);
        close(call_pad_socket);
        return NULL;
    }

    // Find an available car
    Car *selected_car = find_available_car(source_floor, destination_floor);

    if (selected_car) {
        // Send car name to call pad
        char response[BUFFER_SIZE];
        snprintf(response, sizeof(response), "CAR %s", selected_car->name);
        send(call_pad_socket, response, strlen(response), 0);

        // Update car destination
        pthread_mutex_lock(&selected_car->mutex);
        strncpy(selected_car->current_destination, source_floor, sizeof(selected_car->current_destination));
        pthread_mutex_unlock(&selected_car->mutex);

        // Notify car of new destination
        char command[BUFFER_SIZE];
        snprintf(command, sizeof(command), "FLOOR %s", source_floor);
        send_message(selected_car->socket, command);
    } else {
        // No available car
        char response[BUFFER_SIZE];
        snprintf(response, sizeof(response), "UNAVAILABLE");
        send(call_pad_socket, response, strlen(response), 0);
    }
    pthread_mutex_unlock(&car_mutex);

    close(call_pad_socket);
    return NULL;
}

void start_server() {
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("socket()");
        exit(1);
    }

    int opt_enable = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt_enable, sizeof(opt_enable)) == -1) {
        perror("setsockopt()");
        close(server_socket);
        exit(1);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server_socket, (const struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind()");
        close(server_socket);
        exit(1);
    }

    if (listen(server_socket, 10) == -1) {
        perror("listen()");
        close(server_socket);
        exit(1);
    }

    signal(SIGINT, handle_sigint);
    signal(SIGPIPE, SIG_IGN);

    log_message("Controller is running");

    while (1) {
        int *client_socket = malloc(sizeof(int));
        if (client_socket == NULL) {
            perror("malloc");
            exit(1);
        }
        *client_socket = accept(server_socket, (struct sockaddr *)&client_addr, 
                              &client_addr_len);
        if (*client_socket == -1) {
            perror("accept()");
            free(client_socket);
            continue;
        }

        char *buffer = receive_msg(*client_socket);

        if (buffer == NULL) {
            close(*client_socket);
            free(client_socket);
            continue;
        }

        if (strncmp(buffer, "CAR", 3) == 0) {
            // Handle car connection similarly with thread_args
            struct thread_args *args = malloc(sizeof(struct thread_args));
            args->socket = *client_socket;
            args->initial_message = buffer;  // Pass the buffer to the thread
            
            pthread_t car_thread;
            pthread_create(&car_thread, NULL, handle_car, args);
            pthread_detach(car_thread);
        } 
        else if (strncmp(buffer, "CALL", 4) == 0) {
            // Create thread_args structure to pass both socket and message
            struct thread_args *args = malloc(sizeof(struct thread_args));
            args->socket = *client_socket;
            args->initial_message = buffer;  // Pass the buffer to the thread
            
            pthread_t call_pad_thread;
            pthread_create(&call_pad_thread, NULL, handle_call_pad, args);
            pthread_detach(call_pad_thread);
        } 
        else {
            close(*client_socket);
            free(buffer);
        }
        free(client_socket);
    }
}