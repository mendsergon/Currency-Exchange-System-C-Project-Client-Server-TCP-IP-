#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <pthread.h>
#include <sys/select.h>
#include <signal.h>
#include <ctype.h>
#include <setjmp.h>
#include <fcntl.h>
#include "Functions.h"

// Server configuration constants
#define PORT 8080                // TCP port to connect to
#define SERVER_ADDR "127.0.0.1"  // Localhost IP address
#define MAX_SIZE 1024            // Maximum buffer size

int main() {
    // Socket descriptor for the client
    int client_socket = 0;

    // Structure to store server address information
    struct sockaddr_in server_addr;

    // Message sent to server when client disconnects
    char *terminationMessage = "LOGOUT_EXIT";

    // Track login state
    bool isLoggedIn = false;

    // Create client socket (IPv4, TCP)
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    socketPerror(client_socket);

    // Configure server address
    server_addr.sin_family = AF_INET;              // IPv4
    server_addr.sin_port = htons(PORT);            // Convert port to network byte order
    server_addr.sin_addr.s_addr = inet_addr(SERVER_ADDR); // Convert IP to binary form

    // Connect to the server
    printf("Connecting to server at %s:%d...\n", SERVER_ADDR, PORT);
    if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("Error connecting to server");
        printf("Make sure the server is running on %s:%d\n", SERVER_ADDR, PORT);
        exit(EXIT_FAILURE);
    }
    
    printf("Successfully connected to the server!\n");

    // Start client operations (handles authentication, requests, etc.)
    initiate_client_operations(client_socket);
    
    // Gracefully close connection to the server
    printf("Closing connection to server...\n");
    if (shutdown(client_socket, SHUT_RDWR) == EOF || close(client_socket) == EOF) {
        printf("Client socket did not close successfully.\n");
    } else {
        printf("Successfully exited.\n");
    }

    // Return success
    return 0;
}