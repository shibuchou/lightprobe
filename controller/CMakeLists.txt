add_library(lightprobe_controller
    controller_stub.c
    elf_resolver.c
    maps_parser.c
    process_attach.c
    remote_mem_ptrace.c
    thread_control.c
)

target_include_directories(lightprobe_controller PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)
