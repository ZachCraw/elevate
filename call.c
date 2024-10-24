#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 3000
#define BUFFER_SIZE 1024

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s {source floor} {destination floor}\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *source_floor = argv[1];
    char *destination_floor = argv[2];

    // Validate floors
    if (strcmp(source_floor, destination_floor) == 0) {
        fprintf(stderr, "Source and destination floors cannot be the same\n");
        exit(EXIT_FAILURE);
    }

    // Create socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Connect to controller
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        close(sock);
        fprintf(stderr, "Unable to connect to elevator system.\n");
        exit(EXIT_FAILURE);
    }

    // Send call request
    char request[BUFFER_SIZE];
    snprintf(request, BUFFER_SIZE, "CALL %s %s", source_floor, destination_floor);
    send(sock, request, strlen(request), 0);

    // Receive response
    char response[BUFFER_SIZE];
    int bytes_read = recv(sock, response, BUFFER_SIZE, 0);
    if (bytes_read <= 0) {
        close(sock);
        fprintf(stderr, "Error receiving response from elevator system.\n");
        exit(EXIT_FAILURE);
    }

    response[bytes_read] = '\0';

    printf("Received response: %s\n", response);

    // Handle response
    if (strncmp(response, "CAR", 3) == 0) {
        char car_name[50];
        sscanf(response, "CAR %s", car_name);
        printf("Car %s is arriving.\n", car_name);
    } else if (strcmp(response, "UNAVAILABLE") == 0) {
        printf("Sorry, no car is available to take this request.\n");
    } else {
        printf("Invalid response from controller.\n");
    }

    close(sock);
    return 0;
}