cmake_minimum_required(VERSION 3.22)

project(impedance_tube_DAQ C ASM)

enable_language(C ASM)

set(CMAKE_C_STANDARD 11)


add_executable(${PROJECT_NAME}

    Core/Src/main.c
    Core/Src/stm32f4xx_it.c
    Core/Src/stm32f4xx_hal_msp.c
    Core/Src/system_stm32f4xx.c
    Core/Src/syscalls.c
    Core/Src/sysmem.c

    USB/usb_descriptors.c


    tinyusb/src/tusb.c
    tinyusb/src/common/tusb_fifo.c
    tinyusb/src/device/usbd.c

    tinyusb/src/class/audio/audio_device.c

    tinyusb/src/portable/synopsys/dwc2/dcd_dwc2.c
    tinyusb/src/portable/synopsys/dwc2/dwc2_common.c


    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_adc.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_adc_ex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_gpio.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc_ex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pcd.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pcd_ex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_ll_usb.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_uart.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_cortex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_tim.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_tim_ex.c


    startup_stm32f401xc.s
)


target_include_directories(${PROJECT_NAME} PRIVATE
${CMAKE_SOURCE_DIR}

    Core/Inc

    Drivers/CMSIS/Device/ST/STM32F4xx/Include
    Drivers/CMSIS/Include

    Drivers/STM32F4xx_HAL_Driver/Inc

    USB 
    tinyusb/src
    tinyusb/src/common
    tinyusb/src/device
    tinyusb/src/class/audio
    tinyusb/src/portable/synopsys/dwc2

)


target_compile_definitions(${PROJECT_NAME} PRIVATE

    USE_HAL_DRIVER
    STM32F401xC

    CFG_TUSB_MCU=OPT_MCU_STM32F4
    CFG_TUSB_OS=OPT_OS_NONE 
)


target_link_options(${PROJECT_NAME} PRIVATE


    -Wl,--gc-sections

)


set_target_properties(${PROJECT_NAME} PROPERTIES

    SUFFIX ".elf"

)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${PROJECT_NAME}> ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}> ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin
    COMMENT "Generating HEX and BIN files"
)