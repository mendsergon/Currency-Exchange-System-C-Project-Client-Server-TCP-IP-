# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pedantic -g
LIBS = -lpthread

# Targets
TARGETS = server client

# Source files
SERVER_SRC = Bank.c Functions.c
CLIENT_SRC = Client.c Functions.c
COMMON_SRC = Functions.c

# Object files
SERVER_OBJ = Bank.o Functions.o
CLIENT_OBJ = Client.o Functions.o

# Header files
HEADERS = Functions.h

# Default target
all: $(TARGETS)

# Server executable
server: $(SERVER_OBJ)
	$(CC) $(CFLAGS) -o $@ $(SERVER_OBJ) $(LIBS)

# Client executable
client: $(CLIENT_OBJ)
	$(CC) $(CFLAGS) -o $@ $(CLIENT_OBJ) $(LIBS)

# Object file dependencies
Bank.o: Bank.c $(HEADERS)
	$(CC) $(CFLAGS) -c Bank.c

Client.o: Client.c $(HEADERS)
	$(CC) $(CFLAGS) -c Client.c

Functions.o: Functions.c $(HEADERS)
	$(CC) $(CFLAGS) -c Functions.c

# Clean build artifacts
clean:
	rm -f $(TARGETS) *.o database.txt database.lock

# Clean everything including backup files
distclean: clean
	rm -f *~ *.bak *.tmp

# Install to system (optional)
install: all
	@echo "Installing to /usr/local/bin/"
	@sudo cp server client /usr/local/bin/ || echo "Installation failed - need sudo privileges"

# Uninstall from system
uninstall:
	@echo "Uninstalling from /usr/local/bin/"
	@sudo rm -f /usr/local/bin/server /usr/local/bin/client || echo "Uninstall failed"

# Debug build with extra warnings
debug: CFLAGS += -DDEBUG -O0
debug: clean all

# Release build with optimizations
release: CFLAGS += -O2 -DNDEBUG
release: clean all

# Run server in background and client in foreground
run: all
	@echo "Starting server in background..."
	@./server &
	@sleep 2
	@echo "Starting client..."
	@./client

# Kill any running server processes
kill-server:
	@-pkill -f "./server" || true
	@echo "Server processes terminated"

# Show project information
info:
	@echo "Currency Exchange System Build Information"
	@echo "=========================================="
	@echo "Compiler: $(CC)"
	@echo "Flags: $(CFLAGS)"
	@echo "Libraries: $(LIBS)"
	@echo "Targets: $(TARGETS)"
	@echo "Source files:"
	@echo "  Server: $(SERVER_SRC)"
	@echo "  Client: $(CLIENT_SRC)"
	@echo "  Headers: $(HEADERS)"

# Create backup of source files
backup:
	@tar -czf currency_exchange_backup_$(shell date +%Y%m%d_%H%M%S).tar.gz *.c *.h Makefile README* 2>/dev/null || echo "Backup created"

# Static analysis with cppcheck
analyze:
	@echo "Running static analysis..."
	cppcheck --enable=all --suppress=missingIncludeSystem *.c

# Memory leak check with valgrind (server)
valgrind-server: debug
	@echo "Starting server with valgrind..."
	valgrind --leak-check=full --track-origins=yes ./server

# Memory leak check with valgrind (client)
valgrind-client: debug
	@echo "Starting client with valgrind (server must be running)..."
	valgrind --leak-check=full --track-origins=yes ./client

# Help target
help:
	@echo "Currency Exchange System Makefile Targets"
	@echo "=========================================="
	@echo "all       - Build server and client (default)"
	@echo "server    - Build only the server"
	@echo "client    - Build only the client"
	@echo "clean     - Remove executables and object files"
	@echo "distclean - Remove all generated files including backups"
	@echo "debug     - Build with debug symbols and no optimizations"
	@echo "release   - Build with optimizations for production"
	@echo "run       - Build and run server+client automatically"
	@echo "kill-server - Stop any running server processes"
	@echo "info      - Show build configuration information"
	@echo "analyze   - Run static code analysis"
	@echo "valgrind-server - Run server with memory leak detection"
	@echo "valgrind-client - Run client with memory leak detection"
	@echo "backup    - Create backup of source files"
	@echo "help      - Show this help message"

# Phony targets (not actual files)
.PHONY: all clean distclean install uninstall debug release run kill-server info analyze valgrind-server valgrind-client backup help