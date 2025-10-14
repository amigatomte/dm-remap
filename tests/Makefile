# Test Makefile for dm-remap v4.0 metadata and validation functions
# Compiles userspace test suite that validates metadata functionality

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -I../include -g -O0
TARGET1 = test_v4_metadata_creation
TARGET2 = test_v4_validation_engine
SOURCE1 = test_v4_metadata_creation.c
SOURCE2 = test_v4_validation_engine.c

# Default target - build both tests
all: $(TARGET1) $(TARGET2)

# Build the metadata creation test
$(TARGET1): $(SOURCE1)
	$(CC) $(CFLAGS) -o $(TARGET1) $(SOURCE1)

# Build the validation engine test
$(TARGET2): $(SOURCE2)
	$(CC) $(CFLAGS) -o $(TARGET2) $(SOURCE2)

# Run both tests
test: $(TARGET1) $(TARGET2)
	@echo "Running Task 1 (Metadata Creation) tests..."
	./$(TARGET1)
	@echo ""
	@echo "Running Task 2 (Validation Engine) tests..."
	./$(TARGET2)

# Run just Task 1 tests
test-task1: $(TARGET1)
	./$(TARGET1)

# Run just Task 2 tests  
test-task2: $(TARGET2)
	./$(TARGET2)

# Clean up
clean:
	rm -f $(TARGET1) $(TARGET2)

# Install (copy to system test directory)
install: $(TARGET1) $(TARGET2)
	mkdir -p /tmp/dm-remap-tests
	cp $(TARGET1) $(TARGET2) /tmp/dm-remap-tests/

# Run memory check with valgrind (if available)
memcheck: $(TARGET1) $(TARGET2)
	@if command -v valgrind >/dev/null 2>&1; then \
		echo "Running Task 1 with valgrind..."; \
		valgrind --leak-check=full --show-leak-kinds=all ./$(TARGET1); \
		echo "Running Task 2 with valgrind..."; \
		valgrind --leak-check=full --show-leak-kinds=all ./$(TARGET2); \
	else \
		echo "Valgrind not available, running normal tests..."; \
		./$(TARGET1); \
		./$(TARGET2); \
	fi

# Help target
help:
	@echo "Available targets:"
	@echo "  all        - Build both test executables (default)"
	@echo "  test       - Build and run both test suites"
	@echo "  test-task1 - Build and run Task 1 (metadata creation) tests"
	@echo "  test-task2 - Build and run Task 2 (validation engine) tests"
	@echo "  clean      - Remove built files"
	@echo "  install    - Copy tests to /tmp/dm-remap-tests"
	@echo "  memcheck   - Run tests with valgrind (if available)"
	@echo "  help       - Show this help message"

.PHONY: all test test-task1 test-task2 clean install memcheck help