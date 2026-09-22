cmake_minimum_required(VERSION 3.16)
project(GrainBin C ASM)

set(CMAKE_C_STANDARD 11)
add_compile_definitions(USE_STDPERIPH_DRIVER STM32F10X_MD)
include_directories(stdlib/STM32F10x_StdPeriph_Driver/inc)
include_directories(stdlib/CMSIS/CM3/CoreSupport)
include_directories(modules/esp8266)
include_directories(modules/hc05)
include_directories(modules/oled)
include_directories(modules/buzzer)
include_directories(modules/mq4)
include_directories(modules/dht11)
include_directories(modules/bme280)
include_directories(modules/fan)
file(GLOB STDLIB_SRC
        "stdlib/STM32F10x_StdPeriph_Driver/src/*.c"
        "stdlib/system_stm32f10x.c"
        "stdlib/CMSIS/CM3/CoreSupport/core_cm3.c"
        "stdlib/CMSIS/CM3/DeviceSupport/ST/STM32F10X/startup/gcc_ride7/startup_stm32f10x_md.s"
)

add_compile_options(-mcpu=cortex-m3 -mthumb -mthumb-interwork)
add_compile_options(-ffunction-sections -fdata-sections -fno-common -fmessage-length=0)
add_link_options(-Wl,-gc-sections,--print-memory-usage,-Map=${PROJECT_BINARY_DIR}/${PROJECT_NAME}.map)
add_link_options(-mcpu=cortex-m3 -mthumb -mthumb-interwork)

add_executable(target
    ${STDLIB_SRC}
    main.c
    stm32f10x_it.c
    modules/esp8266/esp8266.c
    modules/hc05/hc05.c
    modules/oled/oled.c
    modules/buzzer/buzzer.c
    modules/mq4/mq4.c
    modules/dht11/dht11.c
    modules/bme280/bme280.c
    modules/fan/fan.c
)

target_link_options(target PRIVATE
    -T "${CMAKE_SOURCE_DIR}/stm32f10x_md.ld"
)
