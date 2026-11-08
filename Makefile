# Makefile
#   Auto-discovers every .c/.h file in the project.

TARGET      := dbms
BUILD_DIR   := build

CC          ?= cc
CFLAGS      ?= -std=c11 -Wall -Wextra -O2 -g
CPPFLAGS    +=
LDFLAGS     +=
LDLIBS      +=

# All source files, excluding generated objects.
SRCS        := $(shell find . -type f -name '*.c' -not -path './$(BUILD_DIR)/*' | sort)
OBJS        := $(patsubst ./%.c,$(BUILD_DIR)/%.o,$(SRCS))
DEPS        := $(OBJS:.o=.d)

# Every directory containing a header, plus its parent directory, becomes
# an include path. This also supports nested includes such as "list/list.h".
HEADER_DIRS  := $(sort $(dir $(shell find . -type f -name '*.h' -not -path './$(BUILD_DIR)/*')))
INCLUDE_DIRS := $(sort . \
                  $(patsubst %/,%,$(HEADER_DIRS)) \
                  $(foreach d,$(HEADER_DIRS),$(dir $(patsubst %/,%,$(d)))))

CPPFLAGS    += $(addprefix -I,$(INCLUDE_DIRS))

.PHONY: all clean run

all: $(TARGET)

# Link all objects.
$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# Compile each .c into build/, generating .d dependency files.
$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c -o $@ $<

# Pull in automatically generated header dependencies.
-include $(DEPS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
