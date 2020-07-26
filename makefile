MODE := debug
NAME := clox

CC := gcc
CFLAGS := -std=c99 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function

SOURCE_DIR := src

ifeq ($(MODE), debug)
	CFLAGS += -O0 -g
	BUILD_DIR := bld/dbg
else
	CFLAGS += -O3
	BUILD_DIR := bld/rel
endif

HEADERS := $(wildcard $(SOURCE_DIR)/*.h)
SOURCES := $(wildcard $(SOURCE_DIR)/*.c)
OBJECTS := $(addprefix $(BUILD_DIR)/, $(notdir $(SOURCES:.c=.o)))


bin/$(NAME): $(OBJECTS)
	@ printf "%8s %-40s %s\n" $(CC) $@ "$(CFLAGS)"
	@ mkdir -p bin
	@ $(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/%.o: $(SOURCE_DIR)/%.c $(HEADERS)
	@ printf "%8s %-40s %s\n" $(CC) $@ "$(CFLAGS)"
	@ mkdir -p $(BUILD_DIR)
	@ $(CC) -c $(CFLAGS) -o $@ $<

clean:
	@ rm -rf $(BUILD_DIR)
