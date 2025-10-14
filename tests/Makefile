# Test Makefile for dm-remap v4.0 metadata creation functions
# Compiles userspace test suite that validates metadata functionality

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -I../include -g -O0
TARGET = test_v4_metadata_creation
SOURCE = test_v4_metadata_creation.c

# Default target
all: $(TARGET)

# Build the test executable
$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCE)

# Run the tests
test: $(TARGET)
	./$(TARGET)

# Run tests with verbose output
test-verbose: $(TARGET)
	./$(TARGET) -v

# Clean up
clean:
	rm -f $(TARGET)

# Install (copy to system test directory)
install: $(TARGET)
	mkdir -p /tmp/dm-remap-tests
	cp $(TARGET) /tmp/dm-remap-tests/

# Run memory check with valgrind (if available)
memcheck: $(TARGET)
	@if command -v valgrind >/dev/null 2>&1; then \
		valgrind --leak-check=full --show-leak-kinds=all ./$(TARGET); \
	else \
		echo "Valgrind not available, running normal test..."; \
		./$(TARGET); \
	fi

# Help target
help:
	@echo "Available targets:"
	@echo "  all        - Build the test executable (default)"
	@echo "  test       - Build and run tests"
	@echo "  test-verbose - Run tests with verbose output"
	@echo "  clean      - Remove built files"
	@echo "  install    - Copy test to /tmp/dm-remap-tests"
	@echo "  memcheck   - Run tests with valgrind (if available)"
	@echo "  help       - Show this help message"

.PHONY: all test test-verbose clean install memcheck help