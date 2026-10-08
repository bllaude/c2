CC?=gcc
CFLAGS?=-Wall -O2
TESTFLAGS=-Wall -Wextra -g -O0
BIN_DIR=bin

all: dirs controller agent

dirs:
	mkdir -p $(BIN_DIR)

controller: controller.c
	$(CC) $(CFLAGS) controller.c -o $(BIN_DIR)/controller

agent: agent.c
	$(CC) $(CFLAGS) agent.c -o $(BIN_DIR)/agent

# --- Tests ---------------------------------------------------------------
# Unit tests for the static send_all() helper. Each source is compiled as a
# separate translation unit with main()/send_all() renamed so they can be
# linked into one test binary; libc send() is mocked via --wrap.
test_send_all: agent.c controller.c tests/test_send_all.c tests/send_mock.c tests/send_mock.h
	$(CC) $(TESTFLAGS) -c -Itests -Dmain=agent_main -Dsend_all=agent_send_all -Dstatic= agent.c -o $(BIN_DIR)/agent_for_test.o
	$(CC) $(TESTFLAGS) -c -Itests -Dmain=controller_main -Dsend_all=controller_send_all -Dstatic= controller.c -o $(BIN_DIR)/controller_for_test.o
	$(CC) $(TESTFLAGS) -c -Itests tests/test_send_all.c -o $(BIN_DIR)/test_send_all.o
	$(CC) $(TESTFLAGS) -c -Itests tests/send_mock.c -o $(BIN_DIR)/send_mock.o
	$(CC) $(TESTFLAGS) $(BIN_DIR)/test_send_all.o $(BIN_DIR)/agent_for_test.o \
		$(BIN_DIR)/controller_for_test.o $(BIN_DIR)/send_mock.o \
		-Wl,--wrap=send -o $(BIN_DIR)/test_send_all

mock_controller: tests/mock_controller.c
	$(CC) $(TESTFLAGS) tests/mock_controller.c -o $(BIN_DIR)/mock_controller

test: dirs all test_send_all mock_controller
	@echo "== unit tests =="
	./$(BIN_DIR)/test_send_all
	@echo "== consistency test =="
	sh tests/consistency_test.sh
	@echo "== end-to-end test =="
	bash tests/e2e_test.sh $(BIN_DIR)

clean:
	rm -rf $(BIN_DIR)

.PHONY: all dirs test clean mock_controller test_send_all