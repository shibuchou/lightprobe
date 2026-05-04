add_library(lightprobe_controller
    controller_stub.c
)

target_include_directories(lightprobe_controller PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

