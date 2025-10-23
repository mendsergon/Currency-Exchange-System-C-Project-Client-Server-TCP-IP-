#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>

#define DELIMS "\t\r\n"
#define MAX_SIZE 1024
#define DATABASE_FILE "database.txt"
#define LOCK_FILE "database.lock"

// Global Variables
extern volatile bool server_running;
extern pthread_mutex_t server_state_mutex;
extern int server_socket_main;
extern jmp_buf env;

// Structure for Exchange Rates (to Euro)
typedef struct {
    double Euro;
    double Dollar;
    double Pound;
    double Yen;
    double Rupee;
    double Peso;
    double Franc;
    double Drachmas;
} Coins;

// Structure for currency account
typedef struct {
    int account_id;
    int is_shared;
    Coins coins;
    double total_balance;
} CurrencyAccount;

// Structure for user account
typedef struct {
    int coin_account_id_counter;
    int client_id;
    int currencyAccountNum;
    char *username;
    char *password;
    CurrencyAccount *currencyAccounts;
} UserAccount;

// Structure for transaction
typedef struct Transaction {
    int transaction_id;
    int client_id;
    int account_id;
    char transaction_type[20];
    char currency_from[20];
    char currency_to[20];
    double amount_from;
    double amount_to;
    double exchange_rate;
    time_t timestamp;
    struct Transaction *next;
} Transaction;

// Structure for Database
typedef struct {
    int userid;
    int totalUsers;
    UserAccount *userAccountArr;
    Transaction *transaction_history;
    Coins exchange_rates;
} ServerDatabase;

// ==================== CORE FUNCTION DECLARATIONS ====================

// Database Management
void initializeServerDatabase(ServerDatabase *db);
void initializeExchangeRates(Coins *rates);
int saveServerDatabaseToFile(ServerDatabase *db, const char *filename);
int loadServerDatabaseFromFile(ServerDatabase *db, const char *filename);
void freeServerDatabase(ServerDatabase *db);

// File Locking
int lock_database_file();
int unlock_database_file();

// Currency Operations
int getCurrencyIndex(const char *currency_name);
const char* getCurrencyName(int index);
void applyExchangeRates(Coins *rates, double amount, const char *from_currency, 
                       double *result, const char *to_currency);
double getCurrencyBalance(CurrencyAccount *account, int currency_index);
int updateCurrencyBalance(CurrencyAccount *account, int currency_index, double amount);
int exchangeCurrency(int client_socket, ServerDatabase *db, UserAccount *user);

// User Management
int findUserByUsername(ServerDatabase *db, const char *username);
int authenticateUser(ServerDatabase *db, const char *username, const char *password);
int createNewUser(ServerDatabase *db, const char *username, const char *password);
UserAccount* searchUserByUsername(ServerDatabase* db, const char* username);

// Transaction Management
void addTransaction(ServerDatabase *db, int client_id, int account_id, 
                   const char *type, const char *from_currency, const char *to_currency,
                   double amount_from, double amount_to, double exchange_rate);
void printTransactionHistory(int client_socket, ServerDatabase *db, int client_id);

// Client-Server Communication
void handle_client(int client_socket, int server_socket, ServerDatabase *ServerDatabase);
void initiate_client_operations(int client_socket);

// ==================== UTILITY FUNCTION DECLARATIONS ====================

// Signal and Thread Handlers
void signal_handler(int sig);
void* server_command_listener(void* arg);

// Input/Output Utilities
void displayMenu(bool loggedIn);
void clearBuffer(char* buffer);
void nullTerminate(char* str);
void tokenizeInput(char *buffer, char ***output);
void confirmLoginDataTransfer(int socket, char buffer[], size_t bufferSize, int *conf_s);
void loginData(char buffer[], size_t bufferSize);
void checkForInvalidType(void *ptr, bool *check, bool isNum);
void fixBuffer();

// Validation Utilities
bool containsDigit(char *str);
int checkForInt();
int checkInput(int choice);
int bytesRecievedCheck(ssize_t bytes_received);

// Memory Management
void memoryAllocationCheck(void *ptr);
int deepCopyUserAccount(UserAccount* destination, const UserAccount* source);
void freeTokens(char ***tokens_ptr);

// Error Handling
void socketPerror(int socket);
void forkPerror(int pid);
void listenPerror(int socket);
void dataError(ssize_t bytes_received);
void unError();
void filePerror(FILE *file);

// File Operations
int lock_file(int fd, bool operation);
void initializeCoins(Coins *coins);
void initializeUserAccount(UserAccount *userAccount);

// Legacy functions (for compatibility)
int create_account(int client_socket, UserAccount *clientAccount, char **user_input);
void saveServerDatabase(const char* filename, ServerDatabase* db);
void loadServerDatabase(const char* filename, ServerDatabase* db);

#endif