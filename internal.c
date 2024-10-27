#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "car_shared_mem.h"

enum operations check_operation(char *operation_name) {
    if (strcmp(operation_name, "open") == 0) {
        return OPEN_DOOR;
    } else if (strcmp(operation_name, "close") == 0) {
        return CLOSE_DOOR;
    } else if (strcmp(operation_name, "stop") == 0) {
        return STOP;
    } else if (strcmp(operation_name, "service_on") == 0) {
        return SERVICE_ON;
    } else if (strcmp(operation_name, "service_off") == 0) {
        return SERVICE_OFF;
    } else if (strcmp(operation_name, "up") == 0) {
        return MOVE_UP;
    } else if (strcmp(operation_name, "down") == 0) {
        return MOVE_DOWN;
    } else {
        fprintf(stderr, "Invalid operation: \"%s\"\n", operation_name);
        exit(EXIT_FAILURE);
    }
}

int parse_floor(const char *floor_str) {
    if (floor_str[0] == 'B') {
        return -atoi(floor_str + 1);
    } else {
        return atoi(floor_str);
    }
}

void format_floor(int floor, char *floor_str, size_t size) {
    if (floor < 0) {
        snprintf(floor_str, size, "B%d", -floor);
    } else {
        snprintf(floor_str, size, "%d", floor);
    }
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s {car name} {operation}\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *car_name = argv[1];
    char *operation = argv[2];
    enum operations op = check_operation(operation);

    char shm_name[256];
    snprintf(shm_name, sizeof(shm_name), "%s%s", SHM_NAME_PREFIX, car_name);

    int shm_fd = shm_open(shm_name, O_RDWR, 0666);
    if (shm_fd == -1) {
        printf("Unable to access car %s.\n", car_name);
        exit(EXIT_FAILURE);
    }

    car_shared_mem *shared_mem = mmap(NULL, sizeof(car_shared_mem), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_mem == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    if (pthread_mutex_lock(&shared_mem->mutex) != 0) {
        perror("pthread_mutex_lock");
        exit(EXIT_FAILURE);
    }

    switch (op) {
        case OPEN_DOOR:
            shared_mem->open_button = 1;
            pthread_cond_broadcast(&shared_mem->cond);
            break;
        case CLOSE_DOOR:
            shared_mem->close_button = 1;
            pthread_cond_broadcast(&shared_mem->cond);
            break;
        case STOP:
            shared_mem->emergency_stop = 1;
            pthread_cond_broadcast(&shared_mem->cond);
            break;
        case SERVICE_ON:
            shared_mem->individual_service_mode = 1;
            shared_mem->emergency_mode = 0;
            pthread_cond_broadcast(&shared_mem->cond);
            break;
        case SERVICE_OFF:
            shared_mem->individual_service_mode = 0;
            pthread_cond_broadcast(&shared_mem->cond);
            break;
        case MOVE_UP:
            if (shared_mem->individual_service_mode != 1) {
                fprintf(stderr, "Operation \"%s\" only allowed in service mode.\n", operation);
            } else if (strcmp(shared_mem->status, "Between") == 0) {
                fprintf(stderr, "Operation \"%s\" not allowed while elevator is moving.\n", operation);
            } else if (strcmp(shared_mem->status, "Closed") != 0) {
                fprintf(stderr, "Operation \"%s\" not allowed while doors are open.\n", operation);
            } else {
                int current_floor = parse_floor(shared_mem->current_floor);
                int new_floor = current_floor + 1;
                if (new_floor >= -99 && new_floor <= 999) {
                    format_floor(new_floor, shared_mem->destination_floor, sizeof(shared_mem->destination_floor));
                } else {
                    fprintf(stderr, "Floor value out of range\n");
                }
            }
            pthread_cond_broadcast(&shared_mem->cond);
            break;
        case MOVE_DOWN:
            if (shared_mem->individual_service_mode != 1) {
                fprintf(stderr, "Operation \"%s\" only allowed in service mode.\n", operation);
            } else if (strcmp(shared_mem->status, "Between") == 0) {
                fprintf(stderr, "Operation \"%s\" not allowed while elevator is moving.\n", operation);
            } else if (strcmp(shared_mem->status, "Closed") != 0) {
                fprintf(stderr, "Operation \"%s\" not allowed while doors are open.\n", operation);
            } else {
                int current_floor = parse_floor(shared_mem->current_floor);
                int new_floor = current_floor - 1;
                if (new_floor >= -99 && new_floor <= 999) {
                    format_floor(new_floor, shared_mem->destination_floor, sizeof(shared_mem->destination_floor));
                } else {
                    fprintf(stderr, "Floor value out of range\n");
                }
            }
            pthread_cond_broadcast(&shared_mem->cond);
            break;
        default:
            fprintf(stderr, "Invalid operation: \"%s\"\n", operation);
            exit(EXIT_FAILURE);
    }

    // Signal the condition variable
    if (pthread_cond_signal(&shared_mem->cond) != 0) {
        perror("pthread_cond_signal");
        pthread_mutex_unlock(&shared_mem->mutex);
        exit(EXIT_FAILURE);
    }

    // Unlock the mutex
    if (pthread_mutex_unlock(&shared_mem->mutex) != 0) {
        perror("pthread_mutex_unlock");
        exit(EXIT_FAILURE);
    }

    return 0;
}