CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra

SRC_DIR = src
BUILD_DIR = build
TARGET_CORE = hf_core
TARGET_CLI = hf

SRCS = $(wildcard $(SRC_DIR)/*.cpp)
OBJS = $(SRCS:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)

all: $(TARGET_CORE) cli

$(TARGET_CORE): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

cli:
	@if command -v go >/dev/null 2>&1; then \
		echo "🐹 Compiling Go CLI wrapper..."; \
		go build -o $(TARGET_CLI) cli/main.go; \
	else \
		echo "⚠️  Warning: Go is not installed. Skipping Go CLI build."; \
		echo "👉 Please install Go (e.g., 'sudo dnf install golang') and run 'make cli'."; \
	fi

clean:
	rm -rf $(BUILD_DIR) $(TARGET_CORE) $(TARGET_CLI)

install: all
	install -d /usr/local/bin
	install -m 0755 $(TARGET_CLI) /usr/local/bin/$(TARGET_CLI)
	install -m 0755 $(TARGET_CORE) /usr/local/bin/$(TARGET_CORE)

.PHONY: all clean cli install
