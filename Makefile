CC ?= gcc

CFLAGS ?= -O2 -g
CFLAGS += -std=gnu11 -Wall -Wextra -Wpedantic -Werror -Iinclude
ASFLAGS ?= -g
LDFLAGS ?=

BUILD_DIR := build
BIN := $(BUILD_DIR)/lightprobe

OBJS := \
	$(BUILD_DIR)/controller/controller_stub.o \
	$(BUILD_DIR)/runtime/event_buffer.o \
	$(BUILD_DIR)/runtime/remote_layout.o \
	$(BUILD_DIR)/runtime/runtime_config.o \
	$(BUILD_DIR)/runtime/shadow_stack.o \
	$(BUILD_DIR)/injector/patch_x86_64.o \
	$(BUILD_DIR)/injector/probe_manager.o \
	$(BUILD_DIR)/injector/trampoline_x86_64.o \
	$(BUILD_DIR)/injector/context_x86_64.o \
	$(BUILD_DIR)/injector/probe_stub_x86_64.o \
	$(BUILD_DIR)/injector/ret_stub_x86_64.o \
	$(BUILD_DIR)/cli/main.o \
	$(BUILD_DIR)/cli/cmd_attach.o \
	$(BUILD_DIR)/cli/cmd_detach.o \
	$(BUILD_DIR)/cli/cmd_enable.o \
	$(BUILD_DIR)/cli/cmd_disable.o \
	$(BUILD_DIR)/cli/cmd_events.o \
	$(BUILD_DIR)/cli/cmd_list.o

.PHONY: all clean

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD_DIR)
