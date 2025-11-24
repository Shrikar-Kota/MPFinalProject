# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -O3 -fopenmp -march=native
LDFLAGS = -fopenmp -lm
DEBUG_FLAGS = -g -O0 -DDEBUG
SANITIZE_FLAGS = -fsanitize=thread -g

# Directories
SRC_DIR = src
TEST_DIR = tests
BUILD_DIR = build
BIN_DIR = bin

# Source files
SOURCES = $(SRC_DIR)/skiplist_utils.c \
          $(SRC_DIR)/skiplist_coarse.c \
          $(SRC_DIR)/skiplist_fine.c \
          $(SRC_DIR)/skiplist_lockfree.c

# Object files
OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SOURCES))

# Executables
BENCHMARK = $(BIN_DIR)/benchmark
CORRECTNESS_TEST = $(BIN_DIR)/correctness_test

# Targets
.PHONY: all clean debug test benchmark dirs sanitize

all: dirs $(BENCHMARK) $(CORRECTNESS_TEST)

dirs:
	@mkdir -p $(BUILD_DIR) $(BIN_DIR)

# Build benchmark
$(BENCHMARK): $(OBJECTS) $(BUILD_DIR)/benchmark.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Built benchmark executable: $(BENCHMARK)"

# Build correctness tests
$(CORRECTNESS_TEST): $(OBJECTS) $(BUILD_DIR)/correctness_test.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)
	@echo "Built correctness test executable: $(CORRECTNESS_TEST)"

# Compile source files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(SRC_DIR)/skiplist_common.h
	$(CC) $(CFLAGS) -c $< -o $@

# Compile test files
$(BUILD_DIR)/%.o: $(TEST_DIR)/%.c $(SRC_DIR)/skiplist_common.h
	$(CC) $(CFLAGS) -c $< -o $@

# Debug build
debug: CFLAGS += $(DEBUG_FLAGS)
debug: clean all

# Thread sanitizer build (for detecting race conditions)
sanitize: CFLAGS += $(SANITIZE_FLAGS)
sanitize: LDFLAGS += $(SANITIZE_FLAGS)
sanitize: clean all
	@echo "Built with Thread Sanitizer"
	@echo "Run with: TSAN_OPTIONS='history_size=7' ./bin/correctness_test"

# Run correctness tests
test: $(CORRECTNESS_TEST)
	@echo "Running correctness tests..."
	@./$(CORRECTNESS_TEST)

# Run benchmark with default parameters
benchmark: $(BENCHMARK)
	@echo "Running benchmark (lockfree, 4 threads, mixed workload)..."
	@./$(BENCHMARK)

# Run comprehensive benchmarks
benchmark-all: $(BENCHMARK)
	@echo "Running comprehensive benchmarks..."
	@./scripts/run_experiments.sh

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)
	@echo "Cleaned build artifacts"

# Help
help:
	@echo "Available targets:"
	@echo "  all           - Build all executables (default)"
	@echo "  benchmark     - Build and run basic benchmark"
	@echo "  test          - Build and run correctness tests"
	@echo "  debug         - Build with debug symbols"
	@echo "  sanitize      - Build with Thread Sanitizer"
	@echo "  benchmark-all - Run comprehensive benchmarks"
	@echo "  clean         - Remove build artifacts"
	@echo "  help          - Show this help message"
	@echo ""
	@echo "Example usage:"
	@echo "  make                    # Build everything"
	@echo "  make test               # Run correctness tests"
	@echo "  make benchmark          # Run basic benchmark"
	@echo "  make sanitize && make test  # Test with race detection"