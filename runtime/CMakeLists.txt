add_library(lightprobe_runtime
    event_buffer.c
    remote_layout.c
    runtime_config.c
    shadow_stack.c
)

target_include_directories(lightprobe_runtime PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)
