# Compiler and flags
CC = gcc
CFAGS = -Wall -Wextra -Wpedantic
LDFLAGS = -lusb-1.0 -ldl
USER = $(shell id -u)
DIR = $(shell pwd)
HOME = "/home/"$(shell logname)
TARGET = KD100
DEBUG_TARGET = KD100-debug
VERSION = 1.4.3

# Debug flags
DEBUG_FLAGS = -g -ggdb3 -O0 -fno-omit-frame-pointer -fno-inline \
              -fstack-protector-all -rdynamic -DDEBUG=1

# Release flags
RELEASE_FLAGS = -O2 -DNDEBUG

# Default target
all: $(TARGET)

# Standard build
$(TARGET): KD100.c
	$(CC) $(CFLAGS) KD100.c $(LDFLAGS) -o $(TARGET)
	@echo "Build complete: $(TARGET)"
	@echo "Run './$(TARGET)' to execute"

# Debug build with maximum debugability
debug: CFLAGS += $(DEBUG_FLAGS)
debug: $(DEBUG_TARGET)

$(DEBUG_TARGET): KD100.c
	$(CC) $(CFLAGS) KD100.c $(LDFLAGS) -o $(DEBUG_TARGET)
	@echo "✓ Debug build complete: $(DEBUG_TARGET)"
	@echo "✓ Debug symbols: $(shell readelf -S $(DEBUG_TARGET) 2>/dev/null | grep -c "\.debug" || echo 0) sections"
	@echo "✓ Size: $(shell stat -c%s $(DEBUG_TARGET) 2>/dev/null || echo 0) bytes"
	@echo ""
	@echo "Run with:"
	@echo "  ./$(DEBUG_TARGET)           # Normal execution with crash handler"
	@echo "  gdb ./$(DEBUG_TARGET)       # Interactive debugger"
	@echo "  valgrind ./$(DEBUG_TARGET)  # Memory checker"

# Release build (optimized, no debug)
release: CFLAGS += $(RELEASE_FLAGS)
release: $(TARGET)-release

$(TARGET)-release: KD100.c
	$(CC) $(CFLAGS) KD100.c $(LDFLAGS) -o $(TARGET)-release
	@strip $(TARGET)-release 2>/dev/null || true
	@echo "Release build complete: $(TARGET)-release"
	@echo "Size: $(shell stat -c%s $(TARGET)-release) bytes"

# Install target (keeps your original logic)
install: $(TARGET)
	@if [ "${USER}" = "0" ]; then \
		install -m 755 $(TARGET) /bin/$(TARGET); \
		mkdir -p "${HOME}/.config/$(TARGET)"; \
		cp "default.cfg" "${HOME}/.config/$(TARGET)/" 2>/dev/null || echo "No default.cfg found"; \
		chmod a+wr "${HOME}/.config/$(TARGET)/default.cfg" 2>/dev/null || true; \
		echo "Default config file is located in: ${HOME}/.config/$(TARGET)/"; \
		echo "Installed to /bin/$(TARGET)"; \
	else \
		echo "You need to be root to install system-wide."; \
		echo "Run: sudo make install"; \
	fi

# Version 1.4 build (keeps your original)
1.4:
	$(CC) KD100-1421.c $(LDFLAGS) -g -o KD100-1421
	@echo "Version 1.4 build complete: KD100-1421"

# Run with GDB automatically
gdb: debug
	@echo "Starting GDB debug session..."
	@gdb -q ./$(DEBUG_TARGET) -ex "run" -ex "bt" -ex "quit"

# Run with Valgrind (memory checker)
valgrind: debug
	@echo "Running with Valgrind memory checker..."
	@valgrind --leak-check=full --show-leak-kinds=all \
	          --track-origins=yes --verbose \
	          ./$(DEBUG_TARGET) || true

# Run with strace (system call tracer)
strace: debug
	@echo "Running with strace (system call trace)..."
	@strace -f -o strace.log ./$(DEBUG_TARGET)

# Analyze core dump (if exists)
analyze:
	@if [ -f "core" ] || [ -f "core.*" ]; then \
		echo "Analyzing core dump..."; \
		gdb -q ./$(DEBUG_TARGET) core* -ex "bt full" -ex "info registers" -ex "quit"; \
	else \
		echo "No core dump found."; \
		echo "Enable core dumps with: ulimit -c unlimited"; \
	fi

# Build with address sanitizer (advanced memory checking)
asan: CFLAGS += -fsanitize=address -fno-omit-frame-pointer
asan: LDFLAGS += -fsanitize=address
asan: $(TARGET)-asan

$(TARGET)-asan: KD100.c
	$(CC) $(CFLAGS) KD100.c $(LDFLAGS) -o $(TARGET)-asan
	@echo "AddressSanitizer build complete: $(TARGET)-asan"
	@echo "Run with: ./$(TARGET)-asan"

# Build with undefined behavior sanitizer
ubsan: CFLAGS += -fsanitize=undefined
ubsan: LDFLAGS += -fsanitize=undefined
ubsan: $(TARGET)-ubsan

$(TARGET)-ubsan: KD100.c
	$(CC) $(CFLAGS) KD100.c $(LDFLAGS) -o $(TARGET)-ubsan
	@echo "UndefinedBehaviorSanitizer build complete: $(TARGET)-ubsan"

# Clean targets
clean:
	rm -f $(TARGET) $(DEBUG_TARGET) $(TARGET)-release $(TARGET)-asan $(TARGET)-ubsan KD100-1421
	rm -f *.o *.log core core.*
	@echo "Cleaned build artifacts"

# Help target
help:
	@echo "Available targets:"
	@echo "  make              - Build standard version"
	@echo "  make debug        - Build with full debug symbols and crash handler"
	@echo "  make release      - Build optimized release version"
	@echo "  make install      - Install system-wide (requires root)"
	@echo "  make 1.4          - Build version 1.4.2.1"
	@echo "  make gdb          - Build debug and run in GDB"
	@echo "  make valgrind     - Build debug and run with Valgrind"
	@echo "  make strace       - Build debug and run with strace"
	@echo "  make analyze      - Analyze core dump (if exists)"
	@echo "  make asan         - Build with AddressSanitizer"
	@echo "  make ubsan        - Build with UndefinedBehaviorSanitizer"
	@echo "  make clean        - Remove all build artifacts"
	@echo "  make help         - Show this help"

# Version info
version:
	@echo "KD100 Makefile v$(VERSION)"
	@echo "Compiler: $(shell $(CC) --version | head -1)"

.PHONY: all debug release install 1.4 gdb valgrind strace analyze asan ubsan clean help version

