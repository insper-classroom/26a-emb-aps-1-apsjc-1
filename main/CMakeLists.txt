add_executable(pico_emb daniboy.c display.c)

set_target_properties(pico_emb PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# pull in common dependencies
target_link_libraries(pico_emb
    pico_stdlib
    hardware_pwm
    hardware_irq
    hardware_sync
    hardware_clocks
    hardware_spi
    hardware_adc
    hardware_gpio
)

# create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(pico_emb)
