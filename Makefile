CC?=cc
CFLAGS?=-Wall -Wextra -O2
TESTFLAGS=-Wall -Wextra -g -O0
BIN_DIR=bin

# Both programs share protocol.h / net.h (headers) and protocol.c (framing
# implementation), so there is exactly one definition of every wire-format
# rule. The test binaries link against a mock libc send() via --wrap.
OBJS_COMMON=$(BIN_DIR)/protocol.o

all: dirs $(BIN_DIR)/controller $(BIN_DIR)/agent

dirs:
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/%.o: %.c protocol.h net.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/controller: $(BIN_DIR)/controller.o $(OBJS_COMMON)
	$(CC) $(CFLAGS) $^ -o $@

$(BIN_DIR)/agent: $(BIN_DIR)/agent.o $(OBJS_COMMON)
	$(CC) $(CFLAGS) $^ -o $@

# --- Tests ---------------------------------------------------------------
# Unit tests for the shared framing helpers:
#   * test_send_all: net.h send_all() with libc send() mocked (--wrap)
#   * test_protocol: protocol.c line/frame codec over socketpair()
test_send_all: tests/test_send_all.c tests/send_mock.c net.h
	$(CC) $(TESTFLAGS) -Itests tests/test_send_all.c tests/send_mock.c \
	-Wl,--wrap=send -o $(BIN_DIR)/test_send_all

test_protocol: tests/test_protocol.c protocol.c protocol.h net.h
	$(CC) $(TESTFLAGS) tests/test_protocol.c protocol.c -o $(BIN_DIR)/test_protocol

mock_controller: tests/mock_controller.c protocol.c protocol.h net.h
	$(CC) $(TESTFLAGS) tests/mock_controller.c protocol.c -o $(BIN_DIR)/mock_controller

test: dirs all test_send_all test_protocol mock_controller
	@echo "== unit tests: send_all =="
	./$(BIN_DIR)/test_send_all
	@echo "== unit tests: protocol framing =="
	./$(BIN_DIR)/test_protocol
	@echo "== end-to-end test =="
	bash tests/e2e_test.sh $(BIN_DIR)

clean:
	rm -rf $(BIN_DIR)

.PHONY: all dirs test clean mock_controller test_send_all test_protocol
