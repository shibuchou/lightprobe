add_executable(lightprobe
    main.c
    cmd_attach.c
    cmd_detach.c
    cmd_enable.c
    cmd_disable.c
    cmd_events.c
    cmd_list.c
)

target_include_directories(lightprobe PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(lightprobe PRIVATE
    lightprobe_injector
)
