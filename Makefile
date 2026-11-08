# Makefile
#   Auto-discovers every .c/.h file in the project.

TARGET      := dbms
BUILD_DIR   := build

CC          ?= cc
CFLAGS      ?= -std=c11 -Wall -Wextra -O2 -g
CPPFLAGS    +=
LDFLAGS     +=
LDLIBS      +=

# All source files, excluding generated objects and tests.
SRCS        := $(shell find . -type f -name '*.c' -not -path './$(BUILD_DIR)/*' -not -path './tests/*' | sort)
OBJS        := $(patsubst ./%.c,$(BUILD_DIR)/%.o,$(SRCS))
DEPS        := $(OBJS:.o=.d)

# Library objects used by the tests (everything except main.c).
LIB_SRCS    := $(filter-out ./main.c,$(SRCS))
LIB_OBJS    := $(patsubst ./%.c,$(BUILD_DIR)/%.o,$(LIB_SRCS))

# Isolated test programs, one executable per source file under tests/.
TEST_DIR       := tests
TEST_BUILD_DIR := $(BUILD_DIR)/tests
TEST_SRCS      := $(shell find $(TEST_DIR) -type f -name '*.c' | sort)
TEST_BINS      := $(patsubst $(TEST_DIR)/%.c,$(TEST_BUILD_DIR)/%,$(TEST_SRCS))
TEST_DEPS      := $(TEST_BINS:=.d)

# Every directory containing a header, plus its parent directory, becomes
# an include path. This also supports nested includes such as "list/list.h".
HEADER_DIRS  := $(sort $(dir $(shell find . -type f -name '*.h' -not -path './$(BUILD_DIR)/*')))
INCLUDE_DIRS := $(sort . \
                  $(patsubst %/,%,$(HEADER_DIRS)) \
                  $(foreach d,$(HEADER_DIRS),$(dir $(patsubst %/,%,$(d)))))

CPPFLAGS    += $(addprefix -I,$(INCLUDE_DIRS))

.PHONY: all clean run tests test

all: $(TARGET)

# Link all objects.
$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# Compile each .c into build/, generating .d dependency files.
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

# Build and run every test program.
tests: $(TEST_BINS)
	@set -e; for t in $(TEST_BINS); do echo "== $$t =="; ./$$t; done

test: tests

# Each test is linked with the library objects only, never with main.c.
$(TEST_BUILD_DIR)/%: $(TEST_DIR)/%.c $(LIB_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -o $@ $< $(LIB_OBJS) $(LDFLAGS) $(LDLIBS)

# Pull in automatically generated header dependencies.
-include $(DEPS) $(TEST_DEPS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
