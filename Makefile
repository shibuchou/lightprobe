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
	$(BUILD_DIR)/controller/gadget_finder.o \
	$(BUILD_DIR)/controller/maps_parser.o \
	$(BUILD_DIR)/controller/process_attach.o \
	$(BUILD_DIR)/controller/remote_mem_ptrace.o \
	$(BUILD_DIR)/controller/remote_syscall.o \
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

UNIT_TESTS := \
	$(BUILD_DIR)/tests/test_instruction_len \
	$(BUILD_DIR)/tests/test_stub_builder

TARGET_BINS := \
	$(BUILD_DIR)/tests/target_getpid_loop \
	$(BUILD_DIR)/tests/target_malloc_loop \
	$(BUILD_DIR)/tests/target_multithread_getpid \
	$(BUILD_DIR)/tests/target_multithread_malloc \
	$(BUILD_DIR)/tests/target_strlen_loop \
	$(BUILD_DIR)/tests/target_write_loop \
	$(BUILD_DIR)/tests/target_probe_stress \
	$(BUILD_DIR)/tests/target_getpid_bench \
	$(BUILD_DIR)/tests/target_fp_probe \
	$(BUILD_DIR)/tests/libfp_probe_target.so \
	$(BUILD_DIR)/tests/target_signal_stress \
	$(BUILD_DIR)/tests/libmulti_probe_target.so \
	$(BUILD_DIR)/tests/target_multi_func

test: $(UNIT_TESTS)
	@for test in $(UNIT_TESTS); do \
		$$test || exit $$?; \
	done

targets: $(TARGET_BINS)

$(BUILD_DIR)/tests/test_instruction_len: tests/unit/test_instruction_len.c injector/instruction_x86_64.c include/injector.h include/probe_types.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/unit/test_instruction_len.c injector/instruction_x86_64.c

$(BUILD_DIR)/tests/test_stub_builder: tests/unit/test_stub_builder.c injector/trampoline_x86_64.c runtime/remote_layout.c include/injector.h include/runtime.h include/probe_types.h include/event.h include/arch_x86_64.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/unit/test_stub_builder.c injector/trampoline_x86_64.c runtime/remote_layout.c

$(BUILD_DIR)/tests/target_getpid_loop: tests/targets/target_getpid_loop.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/targets/target_getpid_loop.c

$(BUILD_DIR)/tests/target_malloc_loop: tests/targets/target_malloc_loop.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/targets/target_malloc_loop.c

$(BUILD_DIR)/tests/target_multithread_getpid: tests/targets/target_multithread_getpid.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -pthread -o $@ tests/targets/target_multithread_getpid.c

$(BUILD_DIR)/tests/target_multithread_malloc: tests/targets/target_multithread_malloc.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -pthread -o $@ tests/targets/target_multithread_malloc.c

$(BUILD_DIR)/tests/target_strlen_loop: tests/targets/target_strlen_loop.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/targets/target_strlen_loop.c

$(BUILD_DIR)/tests/target_write_loop: tests/targets/target_write_loop.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/targets/target_write_loop.c

$(BUILD_DIR)/tests/target_probe_stress: tests/targets/target_probe_stress.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -pthread -o $@ tests/targets/target_probe_stress.c

$(BUILD_DIR)/tests/target_getpid_bench: tests/targets/target_getpid_bench.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tests/targets/target_getpid_bench.c -lm

$(BUILD_DIR)/tests/target_fp_probe: tests/targets/target_fp_probe.c $(BUILD_DIR)/tests/libfp_probe_target.so
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -L$(BUILD_DIR)/tests -Wl,-rpath,'$$ORIGIN' -o $@ tests/targets/target_fp_probe.c -lfp_probe_target -lm

$(BUILD_DIR)/tests/libfp_probe_target.so: tests/targets/libfp_probe_target.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -shared -fPIC -fvisibility=hidden -o $@ tests/targets/libfp_probe_target.c -lm

$(BUILD_DIR)/tests/libmulti_probe_target.so: tests/targets/libmulti_probe_target.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -shared -fPIC -fvisibility=hidden -o $@ tests/targets/libmulti_probe_target.c

$(BUILD_DIR)/tests/target_multi_func: tests/targets/target_multi_func.c $(BUILD_DIR)/tests/libmulti_probe_target.so
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -L$(BUILD_DIR)/tests -Wl,-rpath,'$$ORIGIN' -o $@ tests/targets/target_multi_func.c -lmulti_probe_target

$(BUILD_DIR)/tests/target_signal_stress: tests/targets/target_signal_stress.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -pthread -o $@ tests/targets/target_signal_stress.c
