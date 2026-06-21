CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra

SRC_DIR = src
BUILD_DIR = build
TARGET_CORE = hf_core
TARGET_CLI = hf

SRCS = $(wildcard $(SRC_DIR)/*.cpp)
OBJS = $(SRCS:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)
# Rebuild all objects when any header changes (coarse but avoids stale builds
# that crash from struct-layout mismatches when an AST/header is edited).
HEADERS = $(wildcard $(SRC_DIR)/*.hpp) $(wildcard $(SRC_DIR)/*.h)

all: $(TARGET_CORE) cli

$(TARGET_CORE): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp $(HEADERS) | $(BUILD_DIR)
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

# Conformance / golden tests (characterization). `golden` (re)records the frozen
# transpiler output; `golden-verify` fails if current output drifts.
golden: $(TARGET_CORE)
	@bash conformance/run.sh record

golden-verify: $(TARGET_CORE)
	@bash conformance/run.sh verify

.PHONY: all clean cli install golden golden-verify
