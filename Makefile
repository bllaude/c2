CC=gcc
CFLAGS=-Wall -O2
BIN_DIR=bin

all: dirs controller agent

dirs:
	mkdir -p $(BIN_DIR)

controller: controller.c
	$(CC) $(CFLAGS) controller.c -o $(BIN_DIR)/controller

agent: agent.c
	$(CC) $(CFLAGS) agent.c -o $(BIN_DIR)/agent

clean:
	rm -rf $(BIN_DIR)
