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

#define PORT 8080
#define SERVER_ADDR "127.0.0.1"
#define MAX_SIZE 1024

// Function to free server database memory
void freeServerDatabase(ServerDatabase *db) {
    if (db == NULL) return;
    
    // Free all user accounts and their data
    for (int i = 0; i < db->totalUsers; i++) {
        UserAccount *user = &db->userAccountArr[i];
        if (user->username != NULL) {
            free(user->username);
        }
        if (user->password != NULL) {
            free(user->password);
        }
        if (user->currencyAccounts != NULL) {
            free(user->currencyAccounts);
        }
    }
    
    // Free transaction history
    Transaction *current = db->transaction_history;
    while (current != NULL) {
        Transaction *next = current->next;
        free(current);
        current = next;
    }
    
    // Free user array and database
    if (db->userAccountArr != NULL) {
        free(db->userAccountArr);
    }
}

int main() {
    // Counter for connected clients
    int numOfClientsConnected = 0;

    // Socket descriptor for each accepted client
    int client_socket;

    // Structures for server and client socket addresses
    struct sockaddr_in server_addr, client_addr;

    // Thread for handling server console commands
    pthread_t cmd_thread;

    // Length of client address structure
    socklen_t client_addr_len = sizeof(client_addr);

    // Process ID used for forked child processes
    pid_t pid;

    // Allocate and initialize server database in dynamic memory
    ServerDatabase *database = malloc(sizeof(ServerDatabase));
    initializeServerDatabase(database);
    memoryAllocationCheck(database);

    // Load existing database or create new one
    printf("Loading database...\n");
    if (!loadServerDatabaseFromFile(database, "database.txt")) {
        printf("No existing database found. Creating new database.\n");
        // Initialize with default exchange rates
        initializeExchangeRates(&database->exchange_rates);
    } else {
        printf("Database loaded successfully with %d users.\n", database->totalUsers);
    }

    // Register SIGINT handler (e.g., Ctrl+C)
    if (signal(SIGINT, signal_handler) == SIG_ERR) {
        perror("Signal setup failed");
        // Cleanup before exit
        freeServerDatabase(database);
        free(database);
        return 1;
    }

    // Create main server socket (TCP stream)
    server_socket_main = socket(AF_INET, SOCK_STREAM, 0);
    socketPerror(server_socket_main);

    // Configure server address structure
    server_addr.sin_family = AF_INET;          // IPv4
    server_addr.sin_port = htons(PORT);        // Convert port to network byte order
    server_addr.sin_addr.s_addr = INADDR_ANY;  // Accept connections on any interface

    // Bind socket to address and port
    if (bind(server_socket_main, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("Error binding");
        // Cleanup before exit
        freeServerDatabase(database);
        free(database);
        exit(EXIT_FAILURE);
    }

    // Start listening for incoming connections
    listenPerror(server_socket_main);
    printf("Server listening on port %d...\n", PORT);

    // Start command listener thread (for admin/server commands)
    pthread_create(&cmd_thread, NULL, server_command_listener, (void*)&server_socket_main);

    // Main server loop: accept incoming client connections
    while (server_running) {
        sleep(1); // Small delay to reduce CPU usage
        printf("Server keeps listening on port %d...\n", PORT);

        // Accept incoming client connection
        client_socket = accept(server_socket_main, (struct sockaddr*)&client_addr, &client_addr_len);

        // Handle accept errors or shutdown signals
        if (client_socket == -1) {
            if (!server_running) {
                printf("Client Socket Accept interrupted.\nServer is shutting down...\n");
                break;
            }
            perror("Client Socket Accept Error");
            continue;
        } else {
            numOfClientsConnected++;
        }

        // Log new client connection
        printf("Client %d connected: %s:%d\n",
               numOfClientsConnected,
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port));

        socketPerror(client_socket);

        // Fork a new process to handle the client
        pid = fork();
        forkPerror(pid);

        if (pid == 0) {
            // --- Child Process ---
            // Close server socket in child (not needed)
            close(server_socket_main);
            
            // Handle client communication
            handle_client(client_socket, server_socket_main, database);

            // Close client socket in child process
            if (close(client_socket) == EOF)
                printf("Child Client Socket Close Failed\n");
            else
                printf("Gracefully Closed Client Child Handler\n");

            exit(EXIT_SUCCESS);
        } else {
            // --- Parent Process ---
            // Close client socket in parent to free resources
            close(client_socket);
        }
    }

    // ============================================================
    // CLEANUP PHASE - Save database and free resources
    // ============================================================

    printf("\n=== Starting server shutdown cleanup ===\n");

    // Save database to file before shutting down
    printf("Saving database to file...\n");
    if (saveServerDatabaseToFile(database, "database.txt")) {
        printf("Database saved successfully.\n");
    } else {
        printf("Failed to save database.\n");
    }

    // Cleanup phase after server loop ends
    printf("Closing main server socket...\n");
    if (close(server_socket_main) == EOF) {
        perror("Error closing main server socket");
    }

    // Wait for the command listener thread to terminate
    printf("Waiting for command thread to terminate...\n");
    if (pthread_join(cmd_thread, NULL) != 0) {
        perror("pthread_join Failed");
    }

    // Wait for any remaining child processes to finish
    printf("Waiting for child processes to finish...\n");
    while (waitpid(-1, NULL, WNOHANG) > 0) {
        // Continue waiting until no more child processes
    }

    // Free database memory
    printf("Freeing database memory...\n");
    freeServerDatabase(database);
    free(database);

    printf("Server shutdown complete.\n");
    return 0;
}