# =========================
# compiler
# =========================

CC := gcc

CFLAGS := \
	-Wall \
	-Wextra \
	-g \
	-O0

# =========================
# project name
# =========================

TARGET := liblightprobe.a

# =========================
# source files
# =========================

SRCS := \
	controller_stub.c \
	process_attach.c \
	thread_control.c \
	maps_parser.c \
	elf_resolver.c \
	remote_mem_ptrace.c\
	remote_syscall.c

# =========================
# object files
# =========================

OBJS := $(SRCS:.c=.o)

# =========================
# default target
# =========================

all: $(TARGET)

# =========================
# build static library
# =========================

$(TARGET): $(OBJS)
	ar rcs $@ $^

# =========================
# compile .c -> .o
# =========================

%.o: %.c controller_internal.h controller.h
	$(CC) $(CFLAGS) -c $< -o $@

# =========================
# clean
# =========================

clean:
	rm -f \
		$(OBJS) \
		$(TARGET)

# =========================
# phony
# =========================

.PHONY: all clean
