add_library(lightprobe_injector
    instruction_x86_64.c
    patch_x86_64.c
    probe_manager.c
    trampoline_x86_64.c
    context_x86_64.S
    probe_stub_x86_64.S
    ret_stub_x86_64.S
    xmm_save_restore_x86_64.S
)

target_include_directories(lightprobe_injector PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(lightprobe_injector PUBLIC
    lightprobe_controller
    lightprobe_runtime
)
