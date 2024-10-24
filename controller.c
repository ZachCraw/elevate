#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>

#define BUFFER_SIZE 1024
#define PORT 3000
#define MAX_CARS 10
#define MAX_QUEUE 10

typedef struct {
    char name[256];
    char lowest_floor[4];
    char highest_floor[4];
    char current_floor[4];
    char destination_floor[4];
    char status[8];
    int socket;
    pthread_mutex_t mutex;
    char queue[MAX_QUEUE][4];
    int queue_size;
} Car;

Car cars[MAX_CARS];
int car_count = 0;
int server_socket;
pthread_mutex_t car_mutex = PTHREAD_MUTEX_INITIALIZER;

void handle_sigint(int sig);
void log_message(const char *message);
void *handle_car(void *arg);
int can_service_floor(Car *car, const char *floor);
void add_to_queue(Car *car, const char *floor);
void find_available_car();
void *handle_call_pad(void *arg);
void start_server();

int main() {
    start_server();
    return 0;
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
    int car_socket = *(int *)arg;
    free(arg);
    char buffer[BUFFER_SIZE];
    int bytes_read;

    // Read car information
    bytes_read = recv(car_socket, buffer, BUFFER_SIZE, 0);
    if (bytes_read <= 0) {
        close(car_socket);
        return NULL;
    }
    buffer[bytes_read] = '\0';
    log_message(buffer);

    // Parse car information
    char car_name[256], lowest_floor[4], highest_floor[4];
    if (sscanf(buffer, "CAR %s %s %s", car_name, lowest_floor, highest_floor) != 3) {
        fprintf(stderr, "Error parsing car information: %s\n", buffer);
        close(car_socket);
        return NULL;
    }

    // Add car to the list
    pthread_mutex_lock(&car_mutex);
    Car *car = &cars[car_count++];
    strncpy(car->name, car_name, sizeof(car->name));
    strncpy(car->lowest_floor, lowest_floor, sizeof(car->lowest_floor));
    strncpy(car->highest_floor, highest_floor, sizeof(car->highest_floor));
    strncpy(car->current_floor, lowest_floor, sizeof(car->current_floor));
    strncpy(car->destination_floor, lowest_floor, sizeof(car->destination_floor));
    strncpy(car->status, "Closed", sizeof(car->status));
    car->socket = car_socket;
    pthread_mutex_init(&car->mutex, NULL);
    pthread_mutex_unlock(&car_mutex);

    // Process car commands
    while ((bytes_read = recv(car_socket, buffer, BUFFER_SIZE, 0)) > 0) {
        buffer[bytes_read] = '\0';

        // Handle STATUS command
        if (strncmp(buffer, "STATUS", 6) == 0) {
            char status[8], current_floor[4], destination_floor[4];
            if (sscanf(buffer, "STATUS %s %s %s", status, current_floor, destination_floor) != 3) {
                fprintf(stderr, "Error parsing status update: %s\n", buffer);
                continue;
            }
            pthread_mutex_lock(&car->mutex);
            strncpy(car->status, status, sizeof(car->status));
            strncpy(car->current_floor, current_floor, sizeof(car->current_floor));
            strncpy(car->destination_floor, destination_floor, sizeof(car->destination_floor));
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

void add_to_queue(Car *car, const char *floor) {
    pthread_mutex_lock(&car->mutex);
    if (car->queue_size < MAX_QUEUE) {
        strncpy(car->queue[car->queue_size++], floor, sizeof(car->queue[0]));
    }
    pthread_mutex_unlock(&car->mutex);
}

void find_available_car() {
    pthread_mutex_lock(&car_mutex);
    Car *selected_car = NULL;
    int diff_from_call = __INT_MAX__;
    int my_car_diff = diff_from_call;
    for (int i = 0; i < car_count; i++) {
        pthread_mutex_lock(&cars[i].mutex);
        if (strcmp(cars[i].status, "Closed") == 0 && can_service_floor(&cars[i], source_floor) && can_service_floor(&cars[i], destination_floor)) {
            my_car_diff = abs(atoi(source_floor) - atoi(cars[i].current_floor));
            if (my_car_diff < diff_from_call){
                diff_from_call = my_car_diff;
                selected_car = &cars[i];
                pthread_mutex_unlock(&cars[i].mutex);
            }
        }
        pthread_mutex_unlock(&cars[i].mutex);
    }
    return selected_car;
}

void *handle_call_pad(void *arg) {
    int call_pad_socket = *(int *)arg;
    free(arg);
    char buffer[BUFFER_SIZE];
    int bytes_read;

    // Read call pad request
    bytes_read = recv(call_pad_socket, buffer, BUFFER_SIZE, 0);
    if (bytes_read <= 0) {
        close(call_pad_socket);
        return NULL;
    }
    buffer[bytes_read] = '\0';
    log_message(buffer);

    // Parse call pad request
    char source_floor[4], destination_floor[4];
    if (sscanf(buffer, "CALL %s %s", source_floor, destination_floor) != 2) {
        fprintf(stderr, "Error parsing call pad request: %s\n", buffer);
        close(call_pad_socket);
        return NULL;
    }

    // Find an available car
    selected_car = find_available_car();

    if (selected_car) {
        // Send car name to call pad
        char response[BUFFER_SIZE];
        snprintf(response, sizeof(response), "CAR %s", selected_car->name);
        send(call_pad_socket, response, strlen(response), 0);

        // Update car destination
        pthread_mutex_lock(&selected_car->mutex);
        strncpy(selected_car->destination_floor, source_floor, sizeof(selected_car->destination_floor));
        pthread_mutex_unlock(&selected_car->mutex);

        // Notify car of new destination
        char command[BUFFER_SIZE];
        snprintf(command, sizeof(command), "FLOOR %s", source_floor);
        send(selected_car->socket, command, strlen(command), 0);
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
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int opt_enable = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt_enable, sizeof(opt_enable));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, 10) == -1) {
        perror("listen");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    signal(SIGINT, handle_sigint);
    signal(SIGPIPE, SIG_IGN);

    log_message("Controller is running");

    while (1) {
        int *client_socket = malloc(sizeof(int));
        *client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        if (*client_socket == -1) {
            perror("accept");
            free(client_socket);
            continue;
        }

        log_message("Accepted a new connection");

        // Determine if the connection is from a car or a call pad
        char buffer[BUFFER_SIZE];
        int bytes_read = recv(*client_socket, buffer, BUFFER_SIZE, MSG_PEEK);
        if (bytes_read <= 0) {
            close(*client_socket);
            free(client_socket);
            continue;
        }

        buffer[bytes_read] = '\0';

        if (strncmp(buffer, "CAR", 3) == 0) {
            // Create a thread to handle the car connection
            pthread_t car_thread;
            pthread_create(&car_thread, NULL, handle_car, client_socket);
            pthread_detach(car_thread);
        } else if (strncmp(buffer, "CALL", 4) == 0) {
            // Create a thread to handle the call pad connection
            pthread_t call_pad_thread;
            pthread_create(&call_pad_thread, NULL, handle_call_pad, client_socket);
            pthread_detach(call_pad_thread);
        } else {
            close(*client_socket);
            free(client_socket);
        }
    }
}