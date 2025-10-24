## Currency Exchange System (C Project – Client-Server TCP/IP Version)

### Project Summary

This project is a **multi-client currency exchange system** written in C that implements a client-server architecture using TCP/IP sockets. The system simulates a banking environment where users can register, login, manage multiple currency accounts, perform currency exchanges, deposits, withdrawals, and track transaction history. The server handles multiple concurrent clients using **forked processes** and maintains persistent data storage with file-based synchronization for shared accounts.

---

### Core Features

* **User Authentication System** - Registration and login with username/password
* **Multi-Currency Account Management** - Create, view, and delete personal/shared currency accounts
* **Real-Time Currency Exchange** - Convert between 8 different currencies (Euro, Dollar, Pound, Yen, Rupee, Peso, Franc, Drachmas) using fixed exchange rates
* **Financial Operations** - Deposit and withdraw funds from currency accounts with balance validation
* **Transaction History** - Complete audit trail of all financial operations
* **Shared Account Support** - File locking mechanism for synchronized access to shared accounts
* **Persistent Data Storage** - Automatic save/load of user data and transaction history
* **Multi-Client Support** - Concurrent handling of multiple clients using process forking
* **Admin Server Controls** - Graceful shutdown and server management commands

---

### Key Methods and Algorithms

* **TCP/IP Socket Programming:**
  Implements full client-server communication using Internet stream sockets on port 8080 with proper connection handling and data serialization.

* **Process Management:**
  Uses `fork()` to create separate child processes for each connected client, ensuring isolation and concurrent operation.

* **File Locking for Synchronization:**
  Implements `fcntl()` file locking to prevent race conditions in shared currency accounts, ensuring data consistency.

* **Dynamic Memory Management:**
  Comprehensive use of `malloc()`, `calloc()`, `realloc()`, and `free()` with proper error checking for all data structures.

* **Structured Data Persistence:**
  Binary file I/O operations to save and load complex nested data structures including user accounts, currency balances, and transaction history.

* **Modular Program Architecture:**
  Separation of concerns between client operations, server handling, database management, and utility functions.

* **Signal Handling:**
  Implements SIGINT handling for graceful server shutdown with proper cleanup procedures.

---

### Skills Demonstrated

* Implementation of TCP/IP client-server architecture in C
* Concurrent programming using process forking for multi-client support
* File-based data persistence with binary serialization/deserialization
* Critical section protection using file locking mechanisms
* Dynamic memory management for complex nested data structures
* Socket programming with proper error handling and connection management
* Modular program design with clear separation between client and server logic
* Input validation and user interface design for console applications
* Signal handling for graceful application termination
* **Automated builds using Makefile for compilation and project management**

---

### File Overview

| File Name        | Description                                                                 |
| ---------------- | --------------------------------------------------------------------------- |
| **Bank.c**       | Main server application handling client connections and process management  |
| **Client.c**     | Client application providing user interface and server communication        |
| **Functions.c**  | Core business logic, database operations, and utility functions             |
| **Functions.h**  | Data structure definitions and function prototypes for the entire system    |
| **makefile.mak** | Makefile automating compilation, debugging, installation, and cleanup tasks |

---

### Technical Architecture

* **Server Process:** Listens on port 8080, accepts connections, forks child processes
* **Child Processes:** Handle individual client sessions independently
* **Database Structure:** Hierarchical data with users → currency accounts → transaction history
* **Currency Support:** 8 currencies with Euro as base currency for conversions
* **Concurrency Model:** Process-based isolation with file locking for shared resources
* **Data Persistence:** Automatic saving to `database.txt` with transaction logging

---

### How to Compile and Run

1. Compile the server and client applications using the **Makefile** (`makefile.mak`):

   ```bash
   make all
   ```

   Or compile manually:

   ```bash
   gcc -o server Bank.c Functions.c -lpthread
   gcc -o client Client.c Functions.c -lpthread
   ```

2. Start the server in one terminal:

   ```bash
   ./server
   ```

3. Run clients in separate terminals:

   ```bash
   ./client
   ```

4. Follow the interactive menus to register, login, and perform currency operations.

5. Use the `shutdown` command in the server terminal for graceful termination.

---

### System Requirements

* **Operating System:** Linux/Unix environment
* **Compiler:** GCC with pthread support
* **Network:** Localhost TCP/IP connectivity
* **Permissions:** File read/write access for database persistence

---
