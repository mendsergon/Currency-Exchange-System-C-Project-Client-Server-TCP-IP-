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
#include <time.h>
#include <errno.h>
#include "Functions.h"

#define MAX_SIZE 1024

// Global Variables
jmp_buf env;
volatile bool server_running = true;
pthread_mutex_t server_state_mutex = PTHREAD_MUTEX_INITIALIZER;
int server_socket_main;

// File descriptor for database lock
static int db_lock_fd = -1;

// ============================================================
// File Locking Implementation for Shared Accounts
// ============================================================

int lock_database_file() {
    db_lock_fd = open(LOCK_FILE, O_CREAT | O_RDWR, 0666);
    if (db_lock_fd == -1) {
        perror("Error opening lock file");
        return -1;
    }
    
    struct flock fl;
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    
    if (fcntl(db_lock_fd, F_SETLKW, &fl) == -1) {
        perror("Error locking database file");
        close(db_lock_fd);
        return -1;
    }
    return 0;
}

int unlock_database_file() {
    if (db_lock_fd == -1) return 0;
    
    struct flock fl;
    fl.l_type = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    
    if (fcntl(db_lock_fd, F_SETLK, &fl) == -1) {
        perror("Error unlocking database file");
        return -1;
    }
    
    close(db_lock_fd);
    db_lock_fd = -1;
    return 0;
}

// ============================================================
// Database Persistence Functions
// ============================================================

void initializeExchangeRates(Coins *rates) {
    rates->Euro = 1.00;
    rates->Dollar = 1.08;
    rates->Franc = 6.55;
    rates->Peso = 166.38;
    rates->Rupee = 89.98;
    rates->Pound = 0.85;
    rates->Yen = 158.83;
    rates->Drachmas = 340.75;
}

int saveServerDatabaseToFile(ServerDatabase *db, const char *filename) {
    if (lock_database_file() == -1) {
        return 0;
    }
    
    FILE *file = fopen(filename, "wb");
    if (!file) {
        unlock_database_file();
        return 0;
    }
    
    // Save basic database info
    fwrite(&db->totalUsers, sizeof(int), 1, file);
    fwrite(&db->userid, sizeof(int), 1, file);
    fwrite(&db->exchange_rates, sizeof(Coins), 1, file);
    
    // Save each user
    for (int i = 0; i < db->totalUsers; i++) {
        UserAccount *user = &db->userAccountArr[i];
        
        // Save user basic info
        fwrite(&user->client_id, sizeof(int), 1, file);
        fwrite(&user->coin_account_id_counter, sizeof(int), 1, file);
        fwrite(&user->currencyAccountNum, sizeof(int), 1, file);
        
        // Save username and password
        int username_len = strlen(user->username) + 1;
        fwrite(&username_len, sizeof(int), 1, file);
        fwrite(user->username, username_len, 1, file);
        
        int password_len = strlen(user->password) + 1;
        fwrite(&password_len, sizeof(int), 1, file);
        fwrite(user->password, password_len, 1, file);
        
        // Save currency accounts
        for (int j = 0; j < user->currencyAccountNum; j++) {
            CurrencyAccount *acc = &user->currencyAccounts[j];
            fwrite(acc, sizeof(CurrencyAccount), 1, file);
        }
    }
    
    fclose(file);
    unlock_database_file();
    return 1;
}

int loadServerDatabaseFromFile(ServerDatabase *db, const char *filename) {
    if (lock_database_file() == -1) {
        return 0;
    }
    
    FILE *file = fopen(filename, "rb");
    if (!file) {
        unlock_database_file();
        return 0;
    }
    
    // Load basic database info
    fread(&db->totalUsers, sizeof(int), 1, file);
    fread(&db->userid, sizeof(int), 1, file);
    fread(&db->exchange_rates, sizeof(Coins), 1, file);
    
    // Allocate memory for users
    db->userAccountArr = malloc(db->totalUsers * sizeof(UserAccount));
    if (!db->userAccountArr) {
        fclose(file);
        unlock_database_file();
        return 0;
    }
    
    // Load each user
    for (int i = 0; i < db->totalUsers; i++) {
        UserAccount *user = &db->userAccountArr[i];
        
        // Load user basic info
        fread(&user->client_id, sizeof(int), 1, file);
        fread(&user->coin_account_id_counter, sizeof(int), 1, file);
        fread(&user->currencyAccountNum, sizeof(int), 1, file);
        
        // Load username
        int username_len;
        fread(&username_len, sizeof(int), 1, file);
        user->username = malloc(username_len);
        fread(user->username, username_len, 1, file);
        
        // Load password
        int password_len;
        fread(&password_len, sizeof(int), 1, file);
        user->password = malloc(password_len);
        fread(user->password, password_len, 1, file);
        
        // Load currency accounts
        if (user->currencyAccountNum > 0) {
            user->currencyAccounts = malloc(user->currencyAccountNum * sizeof(CurrencyAccount));
            for (int j = 0; j < user->currencyAccountNum; j++) {
                fread(&user->currencyAccounts[j], sizeof(CurrencyAccount), 1, file);
            }
        } else {
            user->currencyAccounts = NULL;
        }
    }
    
    // Initialize transaction history (empty for now)
    db->transaction_history = NULL;
    
    fclose(file);
    unlock_database_file();
    return 1;
}

// ============================================================
// Currency Exchange Functions
// ============================================================

int getCurrencyIndex(const char *currency_name) {
    if (strcmp(currency_name, "Euro") == 0) return 0;
    if (strcmp(currency_name, "Dollar") == 0) return 1;
    if (strcmp(currency_name, "Pound") == 0) return 2;
    if (strcmp(currency_name, "Yen") == 0) return 3;
    if (strcmp(currency_name, "Rupee") == 0) return 4;
    if (strcmp(currency_name, "Peso") == 0) return 5;
    if (strcmp(currency_name, "Franc") == 0) return 6;
    if (strcmp(currency_name, "Drachmas") == 0) return 7;
    return -1;
}

const char* getCurrencyName(int index) {
    switch(index) {
        case 0: return "Euro";
        case 1: return "Dollar";
        case 2: return "Pound";
        case 3: return "Yen";
        case 4: return "Rupee";
        case 5: return "Peso";
        case 6: return "Franc";
        case 7: return "Drachmas";
        default: return "Unknown";
    }
}

void applyExchangeRates(Coins *rates, double amount, const char *from_currency, 
                       double *result, const char *to_currency) {
    // Convert to Euros first
    double in_euros;
    if (strcmp(from_currency, "Euro") == 0) in_euros = amount;
    else if (strcmp(from_currency, "Dollar") == 0) in_euros = amount / rates->Dollar;
    else if (strcmp(from_currency, "Pound") == 0) in_euros = amount / rates->Pound;
    else if (strcmp(from_currency, "Yen") == 0) in_euros = amount / rates->Yen;
    else if (strcmp(from_currency, "Rupee") == 0) in_euros = amount / rates->Rupee;
    else if (strcmp(from_currency, "Peso") == 0) in_euros = amount / rates->Peso;
    else if (strcmp(from_currency, "Franc") == 0) in_euros = amount / rates->Franc;
    else if (strcmp(from_currency, "Drachmas") == 0) in_euros = amount / rates->Drachmas;
    else in_euros = amount;
    
    // Convert from Euros to target currency
    if (strcmp(to_currency, "Euro") == 0) *result = in_euros;
    else if (strcmp(to_currency, "Dollar") == 0) *result = in_euros * rates->Dollar;
    else if (strcmp(to_currency, "Pound") == 0) *result = in_euros * rates->Pound;
    else if (strcmp(to_currency, "Yen") == 0) *result = in_euros * rates->Yen;
    else if (strcmp(to_currency, "Rupee") == 0) *result = in_euros * rates->Rupee;
    else if (strcmp(to_currency, "Peso") == 0) *result = in_euros * rates->Peso;
    else if (strcmp(to_currency, "Franc") == 0) *result = in_euros * rates->Franc;
    else if (strcmp(to_currency, "Drachmas") == 0) *result = in_euros * rates->Drachmas;
    else *result = in_euros;
}

double getCurrencyBalance(CurrencyAccount *account, int currency_index) {
    switch(currency_index) {
        case 0: return account->coins.Euro;
        case 1: return account->coins.Dollar;
        case 2: return account->coins.Pound;
        case 3: return account->coins.Yen;
        case 4: return account->coins.Rupee;
        case 5: return account->coins.Peso;
        case 6: return account->coins.Franc;
        case 7: return account->coins.Drachmas;
        default: return 0.0;
    }
}

int updateCurrencyBalance(CurrencyAccount *account, int currency_index, double amount) {
    // For shared accounts, we need to lock the database
    if (account->is_shared) {
        if (lock_database_file() == -1) {
            return 0;
        }
    }
    
    int success = 1;
    switch(currency_index) {
        case 0: 
            if (account->coins.Euro + amount >= 0) account->coins.Euro += amount;
            else success = 0;
            break;
        case 1: 
            if (account->coins.Dollar + amount >= 0) account->coins.Dollar += amount;
            else success = 0;
            break;
        case 2: 
            if (account->coins.Pound + amount >= 0) account->coins.Pound += amount;
            else success = 0;
            break;
        case 3: 
            if (account->coins.Yen + amount >= 0) account->coins.Yen += amount;
            else success = 0;
            break;
        case 4: 
            if (account->coins.Rupee + amount >= 0) account->coins.Rupee += amount;
            else success = 0;
            break;
        case 5: 
            if (account->coins.Peso + amount >= 0) account->coins.Peso += amount;
            else success = 0;
            break;
        case 6: 
            if (account->coins.Franc + amount >= 0) account->coins.Franc += amount;
            else success = 0;
            break;
        case 7: 
            if (account->coins.Drachmas + amount >= 0) account->coins.Drachmas += amount;
            else success = 0;
            break;
        default: success = 0;
    }
    
    // Update total balance in Euros
    if (success) {
        account->total_balance = 0;
        account->total_balance += account->coins.Euro;
        account->total_balance += account->coins.Dollar / account->coins.Dollar;
    }
    
    if (account->is_shared) {
        unlock_database_file();
    }
    
    return success;
}

// ============================================================
// Transaction History Functions
// ============================================================

void addTransaction(ServerDatabase *db, int client_id, int account_id, 
                   const char *type, const char *from_currency, const char *to_currency,
                   double amount_from, double amount_to, double exchange_rate) {
    Transaction *new_transaction = malloc(sizeof(Transaction));
    static int transaction_id_counter = 1;
    
    new_transaction->transaction_id = transaction_id_counter++;
    new_transaction->client_id = client_id;
    new_transaction->account_id = account_id;
    strcpy(new_transaction->transaction_type, type);
    strcpy(new_transaction->currency_from, from_currency);
    strcpy(new_transaction->currency_to, to_currency);
    new_transaction->amount_from = amount_from;
    new_transaction->amount_to = amount_to;
    new_transaction->exchange_rate = exchange_rate;
    new_transaction->timestamp = time(NULL);
    new_transaction->next = db->transaction_history;
    
    db->transaction_history = new_transaction;
}

void printTransactionHistory(int client_socket, ServerDatabase *db, int client_id) {
    Transaction *current = db->transaction_history;
    int count = 0;
    char buffer[MAX_SIZE];
    
    while (current != NULL) {
        if (current->client_id == client_id) {
            count++;
            struct tm *timeinfo = localtime(&current->timestamp);
            strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
            
            printf("Transaction %d: %s - %s %lf to %s %lf (Rate: %lf) at %s\n",
                   current->transaction_id, current->transaction_type,
                   current->currency_from, current->amount_from,
                   current->currency_to, current->amount_to,
                   current->exchange_rate, buffer);
        }
        current = current->next;
    }
    
    if (count == 0) {
        printf("No transactions found for client %d\n", client_id);
    }
}

// ============================================================
// User Authentication and Management
// ============================================================

int findUserByUsername(ServerDatabase *db, const char *username) {
    for (int i = 0; i < db->totalUsers; i++) {
        if (strcmp(db->userAccountArr[i].username, username) == 0) {
            return i;
        }
    }
    return -1;
}

int authenticateUser(ServerDatabase *db, const char *username, const char *password) {
    int user_index = findUserByUsername(db, username);
    if (user_index == -1) return -1; // User not found
    
    if (strcmp(db->userAccountArr[user_index].password, password) == 0) {
        return user_index;
    }
    return -1; // Wrong password
}

int createNewUser(ServerDatabase *db, const char *username, const char *password) {
    if (findUserByUsername(db, username) != -1) {
        return 0; // Username already exists
    }
    
    // Reallocate user array
    UserAccount *temp = realloc(db->userAccountArr, (db->totalUsers + 1) * sizeof(UserAccount));
    if (!temp) return 0;
    db->userAccountArr = temp;
    
    // Initialize new user
    UserAccount *new_user = &db->userAccountArr[db->totalUsers];
    new_user->client_id = db->userid++;
    new_user->coin_account_id_counter = 1;
    new_user->currencyAccountNum = 0;
    new_user->currencyAccounts = NULL;
    
    new_user->username = malloc(strlen(username) + 1);
    strcpy(new_user->username, username);
    
    new_user->password = malloc(strlen(password) + 1);
    strcpy(new_user->password, password);
    
    db->totalUsers++;
    return 1;
}

// ============================================================
// Currency Exchange Operations
// ============================================================

int exchangeCurrency(int client_socket, ServerDatabase *db, UserAccount *user) {
    int TRUE = 1;
    int FALSE = 0;
    
    // Send available accounts count
    int accounts = user->currencyAccountNum;
    send(client_socket, &accounts, sizeof(accounts), 0);
    
    if (accounts <= 0) {
        printf("No accounts available for exchange\n");
        return 0;
    }
    
    // Receive account selection
    int account_index;
    recv(client_socket, &account_index, sizeof(account_index), 0);
    
    if (account_index < 1 || account_index > accounts) {
        send(client_socket, &FALSE, sizeof(FALSE), 0);
        return 0;
    }
    
    CurrencyAccount *account = &user->currencyAccounts[account_index - 1];
    
    // Send current exchange rates to client
    send(client_socket, &db->exchange_rates, sizeof(Coins), 0);
    
    // Receive source currency, target currency, and amount
    int from_currency, to_currency;
    double amount;
    recv(client_socket, &from_currency, sizeof(from_currency), 0);
    recv(client_socket, &to_currency, sizeof(to_currency), 0);
    recv(client_socket, &amount, sizeof(amount), 0);
    
    // Check if source currency has sufficient balance
    double current_balance = getCurrencyBalance(account, from_currency);
    if (current_balance < amount) {
        send(client_socket, &FALSE, sizeof(FALSE), 0);
        return 0;
    }
    
    // Calculate exchange
    const char *from_curr_name = getCurrencyName(from_currency);
    const char *to_curr_name = getCurrencyName(to_currency);
    double exchanged_amount;
    
    applyExchangeRates(&db->exchange_rates, amount, from_curr_name, &exchanged_amount, to_curr_name);
    
    // Update balances
    if (updateCurrencyBalance(account, from_currency, -amount) &&
        updateCurrencyBalance(account, to_currency, exchanged_amount)) {
        
        // Add transaction to history
        addTransaction(db, user->client_id, account->account_id, "EXCHANGE", 
                      from_curr_name, to_curr_name, amount, exchanged_amount,
                      exchanged_amount / amount);
        
        // Save database
        saveServerDatabaseToFile(db, DATABASE_FILE);
        
        send(client_socket, &TRUE, sizeof(TRUE), 0);
        send(client_socket, &exchanged_amount, sizeof(exchanged_amount), 0);
        return 1;
    }
    
    send(client_socket, &FALSE, sizeof(FALSE), 0);
    return 0;
}

// ============================================================
// Updated Server Database Initialization
// ============================================================

void initializeServerDatabase(ServerDatabase *db) {
    db->totalUsers = 0;
    db->userid = 1;
    db->userAccountArr = malloc(sizeof(UserAccount));
    db->transaction_history = NULL;
    initializeExchangeRates(&db->exchange_rates);
}

// ============================================================
// Safe Token Management Functions - COMPLETELY FIXED
// ============================================================

void freeTokens(char ***tokens_ptr) {
    if (tokens_ptr == NULL || *tokens_ptr == NULL) {
        return;
    }
    
    char **tokens = *tokens_ptr;
    for (int i = 0; tokens[i] != NULL; i++) {
        free(tokens[i]);
    }
    free(tokens);
    *tokens_ptr = NULL;
}

void tokenizeInput(char *buffer, char ***output) {
    if (buffer == NULL || output == NULL) {
        *output = NULL;
        return;
    }
    
    char **tokens = NULL;
    int token_count = 0;
    
    // Create a copy of the buffer since strtok modifies the original
    char *buffer_copy = strdup(buffer);
    if (buffer_copy == NULL) {
        *output = NULL;
        return;
    }
    
    char *token = strtok(buffer_copy, DELIMS);
    while (token != NULL) {
        char **temp = realloc(tokens, (token_count + 1) * sizeof(char *));
        if (temp == NULL) {
            // Cleanup on allocation failure
            for (int i = 0; i < token_count; i++) {
                free(tokens[i]);
            }
            free(tokens);
            free(buffer_copy);
            *output = NULL;
            return;
        }
        tokens = temp;
        
        tokens[token_count] = strdup(token);
        if (tokens[token_count] == NULL) {
            // Cleanup on strdup failure
            for (int i = 0; i < token_count; i++) {
                free(tokens[i]);
            }
            free(tokens);
            free(buffer_copy);
            *output = NULL;
            return;
        }
        
        token_count++;
        token = strtok(NULL, DELIMS);
    }
    
    // Add NULL terminator
    if (token_count > 0) {
        char **temp = realloc(tokens, (token_count + 1) * sizeof(char *));
        if (temp == NULL) {
            for (int i = 0; i < token_count; i++) {
                free(tokens[i]);
            }
            free(tokens);
            free(buffer_copy);
            *output = NULL;
            return;
        }
        tokens = temp;
        tokens[token_count] = NULL;
    }
    
    free(buffer_copy);
    *output = tokens;
    
    if (tokens == NULL) {
        printf("Tokenization Failed\n");
    } else {
        printf("Tokenization Successful: %d tokens\n", token_count);
    }
}

// ============================================================
// Enhanced Client Handler with All Operations - MEMORY FIXED
// ============================================================

void handle_client(int client_socket, int server_socket, ServerDatabase *ServerDatabase){
    
    // Allocate memory for the client account structure
    UserAccount *clientAccount = malloc(sizeof(UserAccount));
    clientAccount->password = calloc(50, sizeof(char));
    clientAccount->username = calloc(50, sizeof(char));

    // Initialize control variables
    int input_count = 0;
    int client_option = 0;
    int TRUE = 1;
    int FALSE = 0;
    bool errorCheck = false;
    bool isLoggedIn = false;
    char **tokens = NULL;
    char handle_client_buffer[MAX_SIZE] = {0};
    char temp_buffer[MAX_SIZE] = {0}; 
    ssize_t bytes_received = 0;
    bool exit = false;
    int logged_in_user_index = -1;
    
    // Main loop to process client requests until logout or exit
    while (!exit){
        printf("\nWaiting to receive user Input\n");
        fflush(stdout);

        // Receive client request option
        bytes_received = recv(client_socket, &client_option, sizeof(client_option), 0 );
        printf("User Request Number %d Received. Input Value: %d\n", input_count, client_option);
        fflush(stdout);

        if (bytesRecievedCheck(bytes_received)){

            input_count++;

            // Handle requests for logged-in clients
            if (isLoggedIn == true){
                UserAccount *currentUser = &ServerDatabase->userAccountArr[logged_in_user_index];
                
                switch (client_option) {
                    case 1:
                        // View client currency accounts
                        fflush(stdout);
                        printf("Requested \"View Currency Accounts\"\n");
                        
                        // Send number of accounts
                        send(client_socket, &currentUser->currencyAccountNum, sizeof(currentUser->currencyAccountNum), 0);
                        
                        // Send each account details
                        for (int i = 0; i < currentUser->currencyAccountNum; i++) {
                            CurrencyAccount *acc = &currentUser->currencyAccounts[i];
                            send(client_socket, acc, sizeof(CurrencyAccount), 0);
                        }
                        break;

                    case 2:
                        // Exchange coins between accounts
                        fflush(stdout);
                        printf("Requested \"Exchange Coins\"\n");
                        exchangeCurrency(client_socket, ServerDatabase, currentUser);
                        break;

                    case 3:
                        // Withdraw coins from a selected account
                        fflush(stdout);
                        printf("Requested \"Withdraw Coins from Account\"\n");

                        int w_coin = 0;
                        int w_accounts = currentUser->currencyAccountNum;
                        int w_account = 0;
                        double w_amount = 0;

                        // If no accounts exist, notify client and abort
                        if (w_accounts <= 0){
                            printf("No Coin Accounts Found for Client\n");
                            send(client_socket, &FALSE, sizeof(FALSE), 0);
                            break;
                        } else {
                            // Send number of currency accounts to client
                            send(client_socket, &w_accounts, sizeof(w_accounts), 0);
                            printf("Coin Accounts Found, Continuing\n");
                        }

                        // Receive selected account from client
                        if ((bytes_received = recv(client_socket, &w_account, sizeof(w_account), 0)) > 0){
                            printf("Received account: %d\n", w_account);
                        } else printf("Data Transfer Failure: Account\n");

                        // Send the balances of all coins in the selected account
                        CurrencyAccount *withdraw_account = &currentUser->currencyAccounts[w_account - 1];
                        send(client_socket, &(withdraw_account->coins), sizeof(Coins), 0);

                        // Receive coin type and amount for withdrawal
                        if ((bytes_received = recv(client_socket, &w_coin, sizeof(w_coin), 0)) > 0){
                            printf("Received coin: %d\n", w_coin);
                        } else printf("Data Transfer Failure: Coin\n");

                        if ((bytes_received = recv(client_socket, &w_amount, sizeof(w_amount), 0)) > 0){
                            printf("Received amount: %lf\n", w_amount);
                        } else printf("Data Transfer Failure: Amount\n");

                        // Withdraw coins based on the selected type
                        if (updateCurrencyBalance(withdraw_account, w_coin - 1, -w_amount)) {
                            const char* w_coin_name = getCurrencyName(w_coin - 1);
                            addTransaction(ServerDatabase, currentUser->client_id, withdraw_account->account_id,
                                         "WITHDRAW", w_coin_name, "", w_amount, 0, 0);
                            saveServerDatabaseToFile(ServerDatabase, DATABASE_FILE);
                            printf("Funds Withdrawn Successfully: %lf %s\n", w_amount, w_coin_name);
                            send(client_socket, &TRUE, sizeof(TRUE), 0);
                        } else {
                            send(client_socket, &FALSE, sizeof(FALSE), 0);
                            printf("Withdrawal Failed - Insufficient funds\n");
                        }
                        break;

                    case 4:
                        // Deposit coins into an account
                        fflush(stdout);
                        printf("Requested \"Deposit Coins to Account\"\n");

                        int d_coin = 0;
                        int d_accounts = currentUser->currencyAccountNum;
                        int d_account = 0;
                        double d_amount = 0;

                        // If no accounts exist, notify client and abort
                        if (d_accounts <= 0){
                            printf("No Coin Accounts Found for Client\n");
                            send(client_socket, &FALSE, sizeof(FALSE), 0);
                            break;
                        } else {
                            // Send number of accounts to client
                            send(client_socket, &d_accounts, sizeof(d_accounts), 0);
                            printf("Coin Accounts Found, Continuing\n");
                        }

                        // Receive account, coin type, and amount from client
                        if ((bytes_received = recv(client_socket, &d_account, sizeof(d_account), 0)) > 0){
                            printf("Received account: %d\n", d_account);
                        } else printf("Data Transfer Failure: Account\n");

                        if ((bytes_received = recv(client_socket, &d_coin, sizeof(d_coin), 0)) > 0){
                            printf("Received coin: %d\n", d_coin);
                        } else printf("Data Transfer Failure: Coin\n");

                        if ((bytes_received = recv(client_socket, &d_amount, sizeof(d_amount), 0)) > 0){
                            printf("Received amount: %lf\n", d_amount);
                        } else printf("Data Transfer Failure: Amount\n");

                        CurrencyAccount *deposit_account = &currentUser->currencyAccounts[d_account - 1];
                        
                        if (updateCurrencyBalance(deposit_account, d_coin - 1, d_amount)) {
                            const char* coin_name = getCurrencyName(d_coin - 1);
                            addTransaction(ServerDatabase, currentUser->client_id, deposit_account->account_id,
                                         "DEPOSIT", coin_name, "", d_amount, 0, 0);
                            saveServerDatabaseToFile(ServerDatabase, DATABASE_FILE);
                            printf("Funds Added Successfully: %lf %s\n", d_amount, coin_name);
                            send(client_socket, &TRUE, sizeof(TRUE), 0);
                        } else {
                            send(client_socket, &FALSE, sizeof(FALSE), 0);
                            printf("Deposit Failed\n");
                        }
                        break;

                    case 5:
                        // Create a new currency account for the client
                        fflush(stdout);
                        printf("Requested \"Create Coin Account\"\n");

                        int initDepo = 0;
                        int isShared = 0;

                        // Receive initial deposit
                        if (recv(client_socket, &initDepo, sizeof(initDepo), 0) <= 0) {
                            perror("Failed to receive initial deposit");
                            send(client_socket, &FALSE, sizeof(FALSE), 0);
                            break;
                        }
                        printf("Initial Deposit Received: %d\n", initDepo);

                        // Receive shared account flag
                        if (recv(client_socket, &isShared, sizeof(isShared), 0) <= 0) {
                            perror("Failed to receive isShared status");
                            send(client_socket, &FALSE, sizeof(FALSE), 0);
                            break;
                        }
                        printf("isShared Received: %d\n", isShared);

                        // Reallocate memory for new account and initialize it
                        CurrencyAccount *temp = realloc(currentUser->currencyAccounts,
                                                        (currentUser->currencyAccountNum + 1) * sizeof(CurrencyAccount));
                        if (temp == NULL) {
                            perror("Failed to allocate memory for a new currency account\n");
                            send(client_socket, &FALSE, sizeof(FALSE), 0);
                            break;
                        }
                        currentUser->currencyAccounts = temp;

                        int newAccountIndex = currentUser->currencyAccountNum;
                        memset(&currentUser->currencyAccounts[newAccountIndex], 0, sizeof(CurrencyAccount));
                        currentUser->currencyAccounts[newAccountIndex].coins.Euro = initDepo;
                        currentUser->currencyAccounts[newAccountIndex].is_shared = isShared;
                        currentUser->currencyAccounts[newAccountIndex].account_id = currentUser->coin_account_id_counter++;

                        currentUser->currencyAccountNum++;
                        
                        // Add transaction
                        addTransaction(ServerDatabase, currentUser->client_id, 
                                      currentUser->currencyAccounts[newAccountIndex].account_id,
                                      "CREATE_ACCOUNT", "Euro", "", initDepo, 0, 0);
                        
                        // Save database
                        saveServerDatabaseToFile(ServerDatabase, DATABASE_FILE);

                        printf("Account Creation Successful. Initial Deposit: %lf\n", currentUser->currencyAccounts[newAccountIndex].coins.Euro);
                        send(client_socket, &TRUE, sizeof(TRUE), 0);
                        break;

                    case 6:
                        // Delete a currency account
                        fflush(stdout);
                        printf("Requested \"Delete Coin Account\"\n");
                        
                        int del_account = 0;
                        int total_accounts = currentUser->currencyAccountNum;
                        
                        send(client_socket, &total_accounts, sizeof(total_accounts), 0);
                        
                        if (recv(client_socket, &del_account, sizeof(del_account), 0) <= 0) {
                            send(client_socket, &FALSE, sizeof(FALSE), 0);
                            break;
                        }
                        
                        if (del_account < 1 || del_account > total_accounts) {
                            send(client_socket, &FALSE, sizeof(FALSE), 0);
                            break;
                        }
                        
                        // Shift accounts array
                        for (int i = del_account - 1; i < total_accounts - 1; i++) {
                            currentUser->currencyAccounts[i] = currentUser->currencyAccounts[i + 1];
                        }
                        
                        currentUser->currencyAccountNum--;
                        
                        // Reallocate to smaller size
                        temp = realloc(currentUser->currencyAccounts, 
                                      currentUser->currencyAccountNum * sizeof(CurrencyAccount));
                        if (temp || currentUser->currencyAccountNum == 0) {
                            currentUser->currencyAccounts = temp;
                        }
                        
                        saveServerDatabaseToFile(ServerDatabase, DATABASE_FILE);
                        send(client_socket, &TRUE, sizeof(TRUE), 0);
                        break;

                    case 7:
                        // Send or request coins
                        fflush(stdout);
                        printf("Requested \"Send or Request Coins\"\n");
                        send(client_socket, &FALSE, sizeof(FALSE), 0); // Not implemented yet
                        break;

                    case 8:
                        // Transaction history
                        fflush(stdout);
                        printf("Requested \"Transaction History\"\n");
                        printTransactionHistory(client_socket, ServerDatabase, currentUser->client_id);
                        break;
                    
                    case 9:
                        // Logout and exit
                        fflush(stdout);
                        printf("Requested \"Logout & Exit\"\n");
                        isLoggedIn = false;
                        logged_in_user_index = -1;
                        exit = true;
                        break;

                    case 10:
                        // Delete user account
                        fflush(stdout);
                        printf("Requested \"Delete My Account\"\n");
                        send(client_socket, &FALSE, sizeof(FALSE), 0); // Not implemented yet
                        break;

                    default:
                        // Unexpected request
                        printf("(Logged-in) Unexpected Error. Client Unresponsive: %d\n", client_socket);
                        exit = true;
                        break;
                }
            } else {
                // Handle requests for non-logged-in clients (login, create account, exit)
                switch (client_option) {
                    case 1:
                        // Login request
                        fflush(stdout);
                        printf("Requested \"Login\"\n");

                        tokens = NULL;
                        clearBuffer(handle_client_buffer);
                        recv(client_socket, handle_client_buffer, sizeof(handle_client_buffer), 0);

                        // Confirm data received
                        if (handle_client_buffer[0] != '\0'){
                            send(client_socket, &TRUE, sizeof(TRUE), 0);
                            printf("Login Data Received\n");
                        }else{
                            send(client_socket, &FALSE, sizeof(FALSE), 0);
                            printf("Data transfer FAILED\n");
                            break;
                        }

                        tokenizeInput(handle_client_buffer, &tokens);
                        if (tokens == NULL || tokens[0] == NULL || tokens[1] == NULL) {
                            send(client_socket, &FALSE, sizeof(FALSE), 0);
                            send(client_socket, &FALSE, sizeof(FALSE), 0);
                            break;
                        }

                        printf("Username: %s\n", tokens[0]);
                        printf("Password: %s\n", tokens[1]);

                        // Authenticate user
                        logged_in_user_index = authenticateUser(ServerDatabase, tokens[0], tokens[1]);
                        
                        if (logged_in_user_index != -1){
                            send(client_socket, &TRUE, sizeof(TRUE), 0);
                            send(client_socket, &TRUE, sizeof(TRUE), 0);
                            isLoggedIn = true;
                            
                            // Copy user data to clientAccount for backward compatibility
                            strcpy(clientAccount->username, tokens[0]);
                            strcpy(clientAccount->password, tokens[1]);
                            clientAccount->client_id = ServerDatabase->userAccountArr[logged_in_user_index].client_id;
                            clientAccount->currencyAccountNum = ServerDatabase->userAccountArr[logged_in_user_index].currencyAccountNum;
                            
                            printf("Client Logged in successfully.\n");
                        } else {
                            send(client_socket, &FALSE, sizeof(FALSE), 0);
                            send(client_socket, &FALSE, sizeof(FALSE), 0);
                            isLoggedIn = false;
                            printf("Client failed to log in. Incorrect credentials\n");
                        }
                        
                        // Free tokens safely - FIXED
                        freeTokens(&tokens);
                        break;

                    case 2:
                        // Account creation request
                        fflush(stdout);
                        printf("Requested \"Account Creation\"\n");

                        tokens = NULL;
                        clearBuffer(handle_client_buffer);
                        recv(client_socket, handle_client_buffer, sizeof(handle_client_buffer), 0);
                        
                        if (handle_client_buffer[0] == '\0') {
                            send(client_socket, &FALSE, sizeof(TRUE), 0);
                            break;
                        }
                        
                        tokenizeInput(handle_client_buffer, &tokens);
                        
                        if (tokens == NULL || tokens[0] == NULL || tokens[1] == NULL) {
                            send(client_socket, &FALSE, sizeof(TRUE), 0);
                            break;
                        }

                        if (createNewUser(ServerDatabase, tokens[0], tokens[1])){
                            // Save the database after creating new user
                            saveServerDatabaseToFile(ServerDatabase, DATABASE_FILE);
                            send(client_socket, &TRUE, sizeof(TRUE), 0);
                            printf("Account Creation Successful\n");
                        } else{
                            send(client_socket, &FALSE, sizeof(TRUE), 0);
                            printf("Account Creation FAILED - Username may already exist\n");
                        }
                        
                        // Free tokens safely - FIXED
                        freeTokens(&tokens);
                        clearBuffer(handle_client_buffer);
                        break;

                    case 3:
                        // Exit request
                        printf("Requested \"Exit\". Exiting.\n");
                        exit = true;
                        break;

                    default:
                        // Unexpected request
                        printf("Unexpected Error. Client Unresponsive: %d\n", client_socket);
                        exit = true;
                        break;
                }
            }
        } else {
            // Client disconnected unexpectedly
            printf("Client Disconnected Unexpectedly. Server Stopped Receiving Data %d\n", client_socket);
            exit = true;
        }
    }
    
    // Free allocated memory
    free(clientAccount->username);
    free(clientAccount->password);
    free(clientAccount);
}

// ============================================================
// Client Operations Function
// ============================================================

void initiate_client_operations(int client_socket){
    bool isLoggedIn = false;
    bool exit = false;
    int client_option;
    int conf_s = 0;
    int TRUE = 1;
    int FALSE = 0;
    int input_count = 0;
    char *terminationMessage = "LOGOUT_EXIT";
    char temp_buffer[MAX_SIZE]; 
    char buffer[MAX_SIZE];     
    ssize_t bytes_received = 0;

    while (!exit){
        if (isLoggedIn){
            displayMenu(1);
        } else{
            displayMenu(0);
        }

        client_option = checkForInt();

        if (isLoggedIn){
            if (client_option < 1 || client_option > 10){
                printf("Invalid Option, must be 1-10\n");
            } else {
                printf("Valid Option %d. Sending to server\n", client_option);
                send(client_socket, &client_option, sizeof(client_option), 0);
            }
        } else {
            if (client_option < 1 || client_option > 3){
                printf("Invalid Option, must be 1-3\n");
            } else {
                printf("Valid Option %d. Sending to server\n", client_option);
                send(client_socket, &client_option, sizeof(client_option), 0);
            }
        }

        printf("Handling Input %d\n", input_count);
        input_count++;

        if (isLoggedIn){
            switch (client_option){
                case 1:
                    // View coin accounts
                    printf("Requested \"View Coin Accounts\"\n");
                    
                    int account_count;
                    recv(client_socket, &account_count, sizeof(account_count), 0);
                    
                    if (account_count <= 0) {
                        printf("No accounts available.\n");
                    } else {
                        printf("You have %d accounts:\n", account_count);
                        for (int i = 0; i < account_count; i++) {
                            CurrencyAccount account;
                            recv(client_socket, &account, sizeof(CurrencyAccount), 0);
                            printf("Account %d (%s):\n", i + 1, account.is_shared ? "Shared" : "Personal");
                            printf("  Euro: %.2f\n", account.coins.Euro);
                            printf("  Dollar: %.2f\n", account.coins.Dollar);
                            printf("  Pound: %.2f\n", account.coins.Pound);
                            printf("  Yen: %.2f\n", account.coins.Yen);
                            printf("  Rupee: %.2f\n", account.coins.Rupee);
                            printf("  Peso: %.2f\n", account.coins.Peso);
                            printf("  Franc: %.2f\n", account.coins.Franc);
                            printf("  Drachmas: %.2f\n", account.coins.Drachmas);
                            printf("  Total Balance (Euro): %.2f\n", account.total_balance);
                        }
                    }
                    break;
                
                case 2:
                    // Exchange coins
                    printf("Requested \"Exchange Coins\"\n");
                    
                    int ex_accounts;
                    recv(client_socket, &ex_accounts, sizeof(ex_accounts), 0);
                    
                    if (ex_accounts <= 0) {
                        printf("No accounts available for exchange.\n");
                        break;
                    }
                    
                    printf("Select account for exchange (1-%d): ", ex_accounts);
                    int ex_account = checkForInt();
                    send(client_socket, &ex_account, sizeof(ex_account), 0);
                    
                    // Receive exchange rates
                    Coins rates;
                    recv(client_socket, &rates, sizeof(Coins), 0);
                    
                    printf("Current Exchange Rates (to Euro):\n");
                    printf("  Dollar: %.2f\n", rates.Dollar);
                    printf("  Pound: %.2f\n", rates.Pound);
                    printf("  Yen: %.2f\n", rates.Yen);
                    printf("  Rupee: %.2f\n", rates.Rupee);
                    printf("  Peso: %.2f\n", rates.Peso);
                    printf("  Franc: %.2f\n", rates.Franc);
                    printf("  Drachmas: %.2f\n", rates.Drachmas);
                    
                    printf("Select source currency:\n");
                    printf("1: Euro, 2: Dollar, 3: Pound, 4: Yen, 5: Rupee, 6: Peso, 7: Franc, 8: Drachmas\n");
                    int from_currency = checkForInt();
                    
                    printf("Select target currency:\n");
                    printf("1: Euro, 2: Dollar, 3: Pound, 4: Yen, 5: Rupee, 6: Peso, 7: Franc, 8: Drachmas\n");
                    int to_currency = checkForInt();
                    
                    printf("Enter amount to exchange: ");
                    double exchange_amount = checkForInt();
                    
                    send(client_socket, &from_currency, sizeof(from_currency), 0);
                    send(client_socket, &to_currency, sizeof(to_currency), 0);
                    send(client_socket, &exchange_amount, sizeof(exchange_amount), 0);
                    
                    recv(client_socket, &conf_s, sizeof(conf_s), 0);
                    if (conf_s) {
                        double result;
                        recv(client_socket, &result, sizeof(result), 0);
                        printf("Exchange successful! Received: %.2f %s\n", result, getCurrencyName(to_currency - 1));
                    } else {
                        printf("Exchange failed. Insufficient funds or invalid selection.\n");
                    }
                    break;
       
                case 3:
                    // Withdraw coins from an account
                    printf("Requested \"Withdraw Coins from Account\"\n");

                    int w_coin = 0;
                    int w_accounts = 0;
                    int w_account = 0;
                    double w_amount = 0;

                    recv(client_socket, &w_accounts, sizeof(w_accounts), 0);
                    if (w_accounts <= 0){
                        printf("No Coin Accounts Available\n");
                        break;
                    } else printf("Coin Accounts Found, Continuing\n");

                    printf("Which Account do you want to withdraw from? (1-%d)\n", w_accounts);
                    for(int i = 0; i < w_accounts; i++){
                        printf("Account %d\n", i + 1);
                    }

                    while(true){
                        w_account = checkForInt();
                        if (w_account < 1 || w_account > w_accounts){
                            printf("Invalid choice. Try again:");
                        } else {
                            printf("Valid choice. Initiating Coin Withdrawal Sequence\n");
                            break;
                        }
                    }
                    send(client_socket, &w_account, sizeof(w_account), 0);

                    // Receive account balances
                    Coins balances;
                    recv(client_socket, &balances, sizeof(Coins), 0);
                    
                    printf("Current Balances:\n");
                    printf("  Euro: %.2f\n", balances.Euro);
                    printf("  Dollar: %.2f\n", balances.Dollar);
                    printf("  Pound: %.2f\n", balances.Pound);
                    printf("  Yen: %.2f\n", balances.Yen);
                    printf("  Rupee: %.2f\n", balances.Rupee);
                    printf("  Peso: %.2f\n", balances.Peso);
                    printf("  Franc: %.2f\n", balances.Franc);
                    printf("  Drachmas: %.2f\n", balances.Drachmas);

                    printf("Select coin type to withdraw:\n 1: Euro, 2: Dollar, 3: Pound, 4: Yen, 5: Rupee, 6: Peso, 7: Franc, 8: Drachmas\n");
                    while(true){
                        w_coin = checkForInt();
                        if (w_coin < 1 || w_coin > 8){
                            printf("Invalid choice. Try again:");
                        } else {
                            printf("Valid choice. Continuing Coin Withdrawal Sequence\n");
                            break;
                        }
                    }

                    printf("Enter amount to withdraw:\n");
                    w_amount = checkForInt();

                    send(client_socket, &w_coin, sizeof(w_coin), 0);
                    send(client_socket, &w_amount, sizeof(w_amount), 0);

                    recv(client_socket, &conf_s, sizeof(conf_s), 0);                
                    if (conf_s){
                        printf("Coins Successfully Withdrawn\n");
                    } else {
                        printf("Coin Withdrawal Failed. Insufficient Balance\n");
                    }
                    break;
                
                case 4:
                    // Deposit coins to an account
                    printf("Requested \"Deposit Coins to Account\"\n");

                    int d_coin = 0;
                    int d_accounts = 0;
                    int d_account = 0;
                    double d_amount = 0;

                    recv(client_socket, &d_accounts, sizeof(d_accounts), 0);
                    if (d_accounts <= 0){
                        printf("No Coin Accounts Available\n");
                        break;
                    } else printf("Coin Accounts Found, Continuing\n");

                    printf("Select account to deposit into: (1-%d)\n", d_accounts);
                    for(int i = 0; i < d_accounts; i++){
                        printf("Account %d\n", i + 1);
                    }
                    while(true){
                        d_account = checkForInt();
                        if (d_account < 1 || d_account > d_accounts){
                            printf("Invalid choice. Try again:");
                        } else {
                            printf("Valid choice. Initiating Coin Deposit Sequence\n");
                            break;
                        }
                    }

                    printf("Select coin type to deposit:\n 1: Euro, 2: Dollar, 3: Pound, 4: Yen, 5: Rupee, 6: Peso, 7: Franc, 8: Drachmas\n");
                    while(true){
                        d_coin = checkForInt();
                        if (d_coin < 1 || d_coin > 8){
                            printf("Invalid choice. Try again:");
                        } else {
                            printf("Valid choice. Continuing Coin Deposit Sequence\n");
                            break;
                        }
                    }

                    printf("Enter amount to deposit:\n");
                    d_amount = checkForInt();

                    send(client_socket, &d_account, sizeof(d_account), 0);
                    send(client_socket, &d_coin, sizeof(d_coin), 0);
                    send(client_socket, &d_amount, sizeof(d_amount), 0);

                    recv(client_socket, &conf_s, sizeof(conf_s), 0);                
                    if (conf_s){
                        printf("Coins Successfully Deposited\n");
                    } else {
                        printf("Coin Deposit Failed\n");
                    }
                    break;
                
                case 5:
                    // Create a new coin account
                    printf("Requested \"Create Coin Account\"\n");
                    int initDepo = 0;
                    int isShared = 0;

                    conf_s = 0;
                    printf("\nEnter Initial Deposit Amount (Euro): ");
                    initDepo = checkForInt();

                    printf("\nIs it a Shared Account? 1 = YES / 0 = NO: ");
                    while(true){
                        isShared = checkForInt();
                        if (isShared == 0 || isShared == 1){
                            printf("Valid Input. Continuing\n");
                            break;
                        } else {
                            printf("Invalid Input. Try Again: ");
                        }
                    }

                    send(client_socket, &initDepo, sizeof(initDepo), 0);
                    send(client_socket, &isShared, sizeof(isShared), 0);

                    recv(client_socket, &conf_s, sizeof(conf_s), 0);                
                    if (conf_s){
                        printf("Coin Account Successfully Created\n");
                    } else {
                        printf("Coin Account Creation Failed\n");
                    }
                    break;
                
                case 6:
                    printf("Requested \"Delete Coin Account\"\n");
                    
                    int del_accounts;
                    recv(client_socket, &del_accounts, sizeof(del_accounts), 0);
                    
                    if (del_accounts <= 0) {
                        printf("No accounts to delete.\n");
                        break;
                    }
                    
                    printf("Select account to delete (1-%d): ", del_accounts);
                    int account_to_delete = checkForInt();
                    send(client_socket, &account_to_delete, sizeof(account_to_delete), 0);
                    
                    recv(client_socket, &conf_s, sizeof(conf_s), 0);
                    if (conf_s) {
                        printf("Account deleted successfully.\n");
                    } else {
                        printf("Account deletion failed.\n");
                    }
                    break;
                
                case 7:
                    printf("Requested \"Send or Request Coins\"\n");
                    recv(client_socket, &conf_s, sizeof(conf_s), 0);
                    if (!conf_s) {
                        printf("This feature is not yet implemented.\n");
                    }
                    break;
                
                case 8:
                    printf("Requested \"Transaction History\"\n");
                    break;
                                            
                case 9:
                    // Logout and exit
                    printf("Requested \"Logout & Exit\"\n");
                    exit = true;
                    isLoggedIn = false;
                    printf("Exiting. Bye!\n");
                    break;

                case 10:
                    printf("Requested \"Delete My Account\"\n");
                    recv(client_socket, &conf_s, sizeof(conf_s), 0);
                    if (!conf_s) {
                        printf("This feature is not yet implemented.\n");
                    }
                    break;

                default:
                    printf("Invalid Request %d. Try Again\n", client_option);
                    break;
            }
        } else {
            switch (client_option) {
                case 1:
                    // Login
                    printf("Requested \"Login\"\n");

                    int usernameCheck, passwordCheck;

                    confirmLoginDataTransfer(client_socket, buffer, sizeof(buffer), &conf_s);

                    recv(client_socket, &usernameCheck, sizeof(usernameCheck), 0);
                    recv(client_socket, &passwordCheck, sizeof(passwordCheck), 0);

                    if (usernameCheck && passwordCheck){
                        isLoggedIn = true;
                        printf("Successfully Logged in.\n");
                    } else if (!usernameCheck && !passwordCheck){
                        isLoggedIn = false;
                        printf("Log in failed. User does not exist\n");
                    } else if (!usernameCheck){
                        printf("Log in failed. Incorrect username\n");
                    } else if (!passwordCheck){
                        printf("Log in failed. Incorrect password\n");
                    }
                    break;
                
                case 2:
                    // Create new user account
                    printf("Requested \"Create User Account\"\n");
                    confirmLoginDataTransfer(client_socket, buffer, sizeof(buffer), &conf_s);

                    if (conf_s == 1){
                        printf("Account Created Successfully\n");
                    } else {
                        printf("Account Creation Failed - Username may already exist\n");
                    }
                    break;
                
                case 3:
                    // Exit client
                    printf("Requested \"Exit\"\n");
                    exit = true;
                    printf("Exiting. Bye!\n");
                    break;
                
                default:
                    printf("Try again.\n");
                    break;
            }
        }
    }
}

// ============================================================
// Utility Functions
// ============================================================

bool containsDigit(char *str) {
    for (int i = 0; i < strlen(str); i++) {        
        if (isdigit(str[i]))
            return true;
    }
    return false;
}

int bytesRecievedCheck(ssize_t bytes_received) {
    if (bytes_received == -1) {
        perror("Error receiving data");
        exit(EXIT_FAILURE);
    } else if (bytes_received == 0) {
        printf("Client disconnected.\n");
        return 0;
    } else return 1;
}

int create_account(int client_socket, UserAccount *clientAccount, char **user_input) {
    printf("Data Received.\nUsername: %s\nPassword: %s\n", user_input[0], user_input[1]);

    clientAccount->username = malloc(strlen(user_input[0]) + 1);
    strcpy(clientAccount->username, user_input[0]);

    clientAccount->password = malloc(strlen(user_input[1]) + 1);
    strcpy(clientAccount->password, user_input[1]);

    clientAccount->currencyAccountNum = 0;
    
    if (clientAccount->username != NULL && clientAccount->password != NULL) {
        printf("Data written successfully\n");
        return 1;
    } else {
        printf("Data writing failed\n");
        return 0;
    }
}

int checkForInt() {
    int choice;
    do {
        if (scanf("%d", &choice) != 1) {
            printf("Input was NOT an integer. Try again: ");
            fixBuffer();
            choice = EOF;
        } else return choice;
    } while (choice == EOF);
    return EOF;
}

int checkInput(int choice) {
    do {
        if (scanf("%d", &choice) != 1) {
            printf("Input was NOT an integer. Try again: ");
            fixBuffer();
            choice = EOF;
        } else return choice;
    } while (choice == EOF);
    return EOF;
}

void* server_command_listener(void* arg) {
    char command[100];
    int server_socket = *((int*)arg);
    
    while (server_running) {
        printf("You can now type server commands\n");
        printf("Type \"shutdown\" to close server\n\n");

        if (fgets(command, sizeof(command), stdin) != NULL) {
            command[strcspn(command, "\n")] = 0;

            if (strcmp(command, "shutdown") == 0) {
                pthread_mutex_lock(&server_state_mutex);
                server_running = false;
                pthread_mutex_unlock(&server_state_mutex);
                if (close(server_socket) == -1)
                    printf("FAILED");
                printf("Shutting down server...\n");
            }
        }
    }
    pthread_exit(NULL);
}

void initializeCoins(Coins *coins) {
    coins->Euro = 1.00;
    coins->Dollar = 1.08;
    coins->Franc = 6.55;
    coins->Peso = 166.38;
    coins->Rupee = 89.98;
    coins->Pound = 0.85;
    coins->Yen = 158.83;
    coins->Drachmas = 340.75;
}

void signal_handler(int sig) {
    printf("\nSignal %d received, shutting down server...\n", sig);
    pthread_mutex_lock(&server_state_mutex);
    server_running = false;
    pthread_mutex_unlock(&server_state_mutex);
    close(server_socket_main);
}

UserAccount* searchUserByUsername(ServerDatabase* db, const char* username) {
    if (db == NULL || username == NULL) {
        printf("Invalid input: %s\n", username);
        return NULL;
    }

    for (int i = 0; i < db->totalUsers; i++) {
        if (strcmp(db->userAccountArr[i].username, username) == 0) {
            printf("Username found\n");
            return &db->userAccountArr[i];
        }
    }

    printf("Could not find username: %s\n", username);
    return NULL;
}

void displayMenu(bool loggedIn) {
    if (loggedIn){
        printf("\nWhat would you like to do?\n");
        printf("1. View Coin Accounts\n");
        printf("2. Exchange Coins\n");
        printf("3. Withdraw Coins from Account\n");
        printf("4. Deposit Coins to Account\n");
        printf("5. Create Coin Account\n");
        printf("6. Delete Coin Account\n");
        printf("7. Send or Request Coins\n");
        printf("8. Transaction History\n");
        printf("9. Logout & Exit\n");
        printf("10. Delete My Account\n");
        printf("Select an Option: ");
    } else {
        printf("\nHello! Welcome to \"CoinCidental Exchange TM\"\n");
        printf("1. Login\n");
        printf("2. Create User Account\n");
        printf("3. Exit\n");
        printf("Select an Option: ");
    }
}

int deepCopyUserAccount(UserAccount* destination, const UserAccount* source) {
    if (destination == NULL || source == NULL) return 0;

    destination->coin_account_id_counter = source->coin_account_id_counter;
    destination->client_id = source->client_id;
    destination->currencyAccountNum = source->currencyAccountNum;

    destination->username = malloc(strlen(source->username) + 1);
    if (!destination->username) return 0;
    strcpy(destination->username, source->username);

    destination->password = malloc(strlen(source->password) + 1);
    if (!destination->password) {
        free(destination->username);
        return 0;
    }
    strcpy(destination->password, source->password);

    if (source->currencyAccountNum > 0) {
        destination->currencyAccounts = malloc(sizeof(CurrencyAccount) * source->currencyAccountNum);
        if (!destination->currencyAccounts) {
            free(destination->username);
            free(destination->password);
            return 0;
        }
        for (int i = 0; i < source->currencyAccountNum; i++) {
            destination->currencyAccounts[i] = source->currencyAccounts[i];
        }
    } else {
        destination->currencyAccounts = NULL;
    }

    return 1;
}

int lock_file(int fd, bool operation) {
    struct flock fl;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;

    if (operation){
        fl.l_type = F_WRLCK;
        if (fcntl(fd, F_SETLKW, &fl) == -1) {
            perror("Error locking file");
            return -1;
        }
        printf("File Locked Successfully\n");
    } else {
        fl.l_type = F_UNLCK;
        if (fcntl(fd, F_SETLK, &fl) == -1) {
            perror("Error unlocking file");
            return -1;
        }
        printf("File Unlocked Successfully\n");
    }
    return 0;
}

void confirmLoginDataTransfer(int socket, char buffer[], size_t bufferSize, int *conf_s){
    clearBuffer(buffer);
    loginData(buffer, bufferSize);

    send(socket, buffer, strlen(buffer), 0);

    recv(socket, conf_s, sizeof(*conf_s), 0);

    if (*conf_s) {
        printf("Server received the data successfully\n");
    } else {
        printf("Server error while receiving the data\n");
    }
}

void initializeUserAccount(UserAccount *userAccount){
    userAccount->coin_account_id_counter = 1;
    userAccount->currencyAccounts = malloc(sizeof(CurrencyAccount));
}

void loginData(char buffer[], size_t bufferSize){
    char name[50], password[50];

    printf("\nEnter your UserName (Up to 50 Characters): ");
    scanf("%49s", name);

    printf("\nEnter your Password (Up to 50 Characters): ");
    scanf("%49s", password);

    snprintf(buffer, bufferSize, "%s\n%s\n", name, password);
}

void checkForInvalidType(void *ptr, bool *check, bool isNum){
    *check = false;

    if (isNum) {
        int* temp = (int*)ptr;
        do {
            if (scanf("%d", temp) != 1) {
                printf("Invalid Type, input was NOT an int. Try Again: ");
                fixBuffer();
            } else {
                *check = true;
            }
        } while (!*check);
    } else {
        char *temp;
        do {
            temp = malloc(50 * sizeof(char));
            memoryAllocationCheck(temp);
            scanf("%s", temp);

            if (containsDigit(temp)) {
                printf("Invalid Type, input contained digits. Try Again: ");
                fixBuffer();
                free(temp);
            } else {
                strcpy((char*)ptr, temp);
                *check = true;
            }
        } while (!*check);
    }
}

void memoryAllocationCheck(void *ptr){
    if (!ptr) {
        fprintf(stderr, "Memory allocation failed. Exiting Program\n");
        exit(EXIT_FAILURE);
    }
}

void fixBuffer(){
    int clear;
    while ((clear = getchar()) != '\n' && clear != EOF);
}

void socketPerror(int socket){
    if (socket == EOF){
        perror("Error creating socket");
        close(socket);
        exit(EXIT_FAILURE);
    }
}

void forkPerror(int pid){
    if (pid < 0){
        perror("Error forking process");
        exit(EXIT_FAILURE);
    }
}

void listenPerror(int socket){
    if (listen(socket, 5) == -1) {
        perror("Error listening");
        exit(EXIT_FAILURE);
    }
}

void dataError(ssize_t bytes_received){
    if (bytes_received == -1) {
        perror("Error receiving data");
        exit(EXIT_FAILURE);
    }
}

void unError(){
    perror("Unexpected Error");
    exit(EXIT_FAILURE);
}

void filePerror(FILE *file){
    if (!file) {
        perror("Error opening file");
        exit(EXIT_FAILURE);
    }
}

void clearBuffer(char* buffer){
    if (buffer != NULL) {
        memset(buffer, 0, MAX_SIZE);
    }
}

void nullTerminate(char* str){
    if (str != NULL) {
        str[MAX_SIZE - 1] = '\0';
    }
}

void saveServerDatabase(const char* filename, ServerDatabase* db) {
    saveServerDatabaseToFile(db, filename);
}

void loadServerDatabase(const char* filename, ServerDatabase* db) {
    loadServerDatabaseFromFile(db, filename);
}
