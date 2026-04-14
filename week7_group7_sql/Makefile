CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g -Isrc
SRC_DIR = src
TEST_DIR = tests
BUILD_DIR = build
TARGET = sql_processor
TEST_BIN_DIR = $(BUILD_DIR)/tests

MAIN_SRCS = $(filter-out $(SRC_DIR)/main.c,$(wildcard $(SRC_DIR)/*.c))
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
CORE_OBJS = $(MAIN_SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
TEST_SRCS = $(wildcard $(TEST_DIR)/test_*.c)
TEST_BINS = $(TEST_SRCS:$(TEST_DIR)/%.c=$(TEST_BIN_DIR)/%)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TEST_BIN_DIR)/%: $(TEST_DIR)/%.c $(CORE_OBJS) | $(TEST_BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $< $(CORE_OBJS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TEST_BIN_DIR):
	mkdir -p $(TEST_BIN_DIR)

tests: $(TARGET) $(TEST_BINS)
	bash $(TEST_DIR)/run_tests.sh

clean:
	rm -rf $(BUILD_DIR) $(TARGET) data/*.csv

.PHONY: all tests clean
