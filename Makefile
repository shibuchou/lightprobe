CC ?= gcc

CFLAGS ?= -O2 -g
CFLAGS += -std=gnu11 -Wall -Wextra -Wpedantic -Werror -Iinclude
ASFLAGS ?= -g
LDFLAGS ?=

BUILD_DIR := build
BIN := $(BUILD_DIR)/lightprobe

OBJS := \
	$(BUILD_DIR)/controller/controller_stub.o \
	$(BUILD_DIR)/controller/elf_resolver.o \
	$(BUILD_DIR)/controller/maps_parser.o \
	$(BUILD_DIR)/controller/process_attach.o \
	$(BUILD_DIR)/controller/remote_mem_ptrace.o \
	$(BUILD_DIR)/controller/thread_control.o \
	$(BUILD_DIR)/runtime/event_buffer.o \
	$(BUILD_DIR)/runtime/remote_layout.o \
	$(BUILD_DIR)/runtime/runtime_config.o \
	$(BUILD_DIR)/runtime/shadow_stack.o \
	$(BUILD_DIR)/injector/instruction_x86_64.o \
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

.PHONY: all clean test

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

test: $(BUILD_DIR)/tests/test_instruction_len
	$<

$(BUILD_DIR)/tests/test_instruction_len: tests/unit/test_instruction_len.c injector/instruction_x86_64.c include/injector.h include/probe_types.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/unit/test_instruction_len.c injector/instruction_x86_64.c
