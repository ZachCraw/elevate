#ifndef CAR_SHARED_MEM_H
#define CAR_SHARED_MEM_H

#include <pthread.h>
#include <stdint.h>
#include <fcntl.h>

#define MAX_QUEUE 10
#define MAX_FLOOR_LEN 4

#define SHM_NAME_PREFIX "/car"

typedef struct {
    pthread_mutex_t mutex;           // Locked while accessing struct contents
    pthread_cond_t cond;             // Signalled when the contents change
    char current_floor[4];           // C string in the range B99-B1 and 1-999
    char destination_floor[4];       // Same format as above
    char status[8];                  // C string indicating the elevator's status
    uint8_t open_button;             // 1 if open doors button is pressed, else 0
    uint8_t close_button;            // 1 if close doors button is pressed, else 0
    uint8_t door_obstruction;        // 1 if obstruction detected, else 0
    uint8_t overload;                // 1 if overload detected
    uint8_t emergency_stop;          // 1 if stop button has been pressed, else 0
    uint8_t individual_service_mode; // 1 if in individual service mode, else 0
    uint8_t emergency_mode;          // 1 if in emergency mode, else 0
} car_shared_mem;

enum car_status {
    OPENING,
    OPEN,
    CLOSING,
    CLOSED,
    BETWEEN
};

enum operations {
    OPEN_DOOR,
    CLOSE_DOOR,
    STOP,
    SERVICE_ON,
    SERVICE_OFF,
    MOVE_UP,
    MOVE_DOWN
};

#define MAX_FLOORS 10 // Define as per the building's max floors

typedef enum { UP, DOWN, NONE } Direction;

typedef struct {
    char floor[4];
    Direction dir;
} QueueItem;

typedef struct {
    QueueItem *items;
    int size;
    int capacity;
} Queue;

typedef struct Car {
    char name[256];
    char current_floor[4];
    char current_destination[4];
    char lowest_floor[4];
    char highest_floor[4];
    char status[8];
    int socket;
    pthread_cond_t cond;
    pthread_mutex_t mutex;
    Direction direction;
    Queue queue;
    int queue_size;
} Car;

void recv_looped(int fd, void *buf, size_t sz) {
    char *ptr = buf;
    size_t remain = sz;
    while (remain > 0) {
        ssize_t received = read(fd, ptr, remain);
        if (received == -1) {
            perror("read()");
            exit(1);
        } else if (received == 0) {
            fprintf(stderr, "Connection closed by peer\n");
            exit(1);
        }
        ptr += received;
        remain -= received;
    }
}

char *receive_msg(int fd) {
    uint32_t nlen;
    recv_looped(fd, &nlen, sizeof(nlen));
    uint32_t len = ntohl(nlen);
    char *buf = malloc(len + 1);
    if (buf == NULL) {
        perror("malloc");
        exit(1);
    }
    buf[len] = '\0';
    recv_looped(fd, buf, len);
    return buf;
}

void send_looped(int fd, const void *buf, size_t sz)
{
    const char *ptr = buf;
    size_t remain = sz;

    while (remain > 0) {
        ssize_t sent = write(fd, ptr, remain);
        if (sent == -1) {
            perror("write()");
            exit(1);
        }
        ptr += sent;
        remain -= sent;
    }
}

void send_message(int fd, const char *buf)
{
    uint32_t len = htonl(strlen(buf));
    send_looped(fd, &len, sizeof(len));
    send_looped(fd, buf, strlen(buf));
}

#endif // CAR_SHARED_MEM_H