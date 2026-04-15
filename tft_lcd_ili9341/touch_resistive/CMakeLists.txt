add_library(touch_resistive
        touch_resistive.c
)

target_include_directories(touch_resistive PUBLIC .)

target_link_libraries(touch_resistive pico_stdlib hardware_adc)

