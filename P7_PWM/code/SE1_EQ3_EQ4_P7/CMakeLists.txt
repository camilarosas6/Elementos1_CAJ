# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 13_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(SE1_EQ3_EQ4_P7 C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Crear nuestra libreria de comunicacion
add_library(consola STATIC consola.c)

# Permitir que otros archivos encuentren consola.h
target_include_directories(consola PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

# Herramientas del SDK que utiliza la libreria
target_link_libraries(consola PUBLIC
    pico_stdlib
    hardware_uart
    hardware_irq
)

# Add executable. Default name is the project name, version 0.1

add_executable(SE1_EQ3_EQ4_P7 main.c extra.c)

pico_set_program_name(SE1_EQ3_EQ4_P7 "SE1_EQ3_EQ4_P7")
pico_set_program_version(SE1_EQ3_EQ4_P7 "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(SE1_EQ3_EQ4_P7 0)
pico_enable_stdio_usb(SE1_EQ3_EQ4_P7 0)

# Add the standard library to the build
target_link_libraries(SE1_EQ3_EQ4_P7
    pico_stdlib
    consola
    hardware_pwm
    hardware_clocks
    hardware_sync
)

# Add the standard include files to the build
target_include_directories(SE1_EQ3_EQ4_P7 PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
)

pico_add_extra_outputs(SE1_EQ3_EQ4_P7)

