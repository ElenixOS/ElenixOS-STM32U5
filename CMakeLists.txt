cmake_minimum_required(VERSION 3.20)

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)
set(CMAKE_C_COMPILER_FORCED TRUE)
set(CMAKE_CXX_COMPILER_FORCED TRUE)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/target.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/gcc-arm-none-eabi.cmake)

project(ElenixOS-STM32U5 C ASM)

set(PROJECT_NAME ElenixOS-STM32U5)
add_executable(${PROJECT_NAME})

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/flags.cmake)

set(STM32_HAL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/STM32U5xx_HAL_Driver)
set(STM32_DEVICE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/CMSIS_Device_STM32U5xx)
set(STM32_BSP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/STM32U5G9J-DK2)
set(LVGL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lvgl)
set(EOS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ElenixOS)
set(LITTLEFS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/littlefs)
set(MX66_COMPONENT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/Components/mx66uw1g45g)
set(NEMA_GFX_LIB ${LVGL_DIR}/libs/nema_gfx/lib/core/cortex_m33_revC/gcc/libnemagfx-float-abi-hard.a)

file(GLOB STM32_HAL_SRC CONFIGURE_DEPENDS ${STM32_HAL_DIR}/Src/*.c)
list(FILTER STM32_HAL_SRC EXCLUDE REGEX ".*_template\\.c$")
file(GLOB_RECURSE LVGL_SRC CONFIGURE_DEPENDS ${LVGL_DIR}/src/*.c)
# The stock STM32 Nema HAL adapter has an unbounded GPU-completion wait.  Use
# the board-owned adapter so a GPU fault cannot stop LVGL's refresh loop.
list(FILTER LVGL_SRC EXCLUDE REGEX ".*/draw/nema_gfx/lv_draw_nema_gfx_stm32_hal\\.c$")
file(GLOB_RECURSE EOS_SRC CONFIGURE_DEPENDS ${EOS_DIR}/src/*.c)
# Use the STM32 implementation below. The generic ElenixOS bare-metal port
# currently enables interrupts unconditionally only for some contexts and is
# not safe when dispatcher_call() is entered from an ISR.
list(FILTER EOS_SRC EXCLUDE REGEX ".*/port/critical/eos_port_critical_bare_metal\\.c$")
file(GLOB_RECURSE EOS_HEADER_FILES CONFIGURE_DEPENDS ${EOS_DIR}/src/*.h)
set(EOS_INCLUDE_DIRS ${EOS_DIR}/src)
foreach(EOS_HEADER IN LISTS EOS_HEADER_FILES)
  get_filename_component(EOS_HEADER_DIR ${EOS_HEADER} DIRECTORY)
  list(APPEND EOS_INCLUDE_DIRS ${EOS_HEADER_DIR})
endforeach()
list(REMOVE_DUPLICATES EOS_INCLUDE_DIRS)
file(GLOB_RECURSE EOS_RESOURCE_FONT_SRC CONFIGURE_DEPENDS
     ${EOS_DIR}/resources/font/RemixIcon.c)
file(GLOB BSP_COMPONENT_SRC CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/third_party/Components/rk050hr18/*.c
     ${CMAKE_CURRENT_SOURCE_DIR}/third_party/Components/gt911/*.c
     ${MX66_COMPONENT_DIR}/*.c)
set(LITTLEFS_SRC ${LITTLEFS_DIR}/lfs.c ${LITTLEFS_DIR}/lfs_util.c)

target_sources(${PROJECT_NAME} PRIVATE
  Src/main.c
  Src/eos_vcp.c
  Src/eos_port_stm32u5.c
  Src/eos_lvgl_draw_buf_port.c
  Src/eos_nema_gfx_stm32_hal.c
  Src/eos_port_critical_stm32u5.c
  Src/eos_fs_port_stm32u5.c
  Src/syscall.c
  Src/sysmem.c
  Src/system_stm32u5xx.c
  Src/stm32u5xx_hal_msp.c
  Src/stm32u5xx_it.c
  Src/startup_stm32u5g9xx.S
  ${STM32_HAL_SRC}
  ${STM32_BSP_DIR}/stm32u5g9j_discovery_lcd.c
  ${STM32_BSP_DIR}/stm32u5g9j_discovery_bus.c
  ${STM32_BSP_DIR}/stm32u5g9j_discovery_ts.c
  ${STM32_BSP_DIR}/stm32u5g9j_discovery.c
  ${STM32_BSP_DIR}/stm32u5g9j_discovery_hspi.c
  ${BSP_COMPONENT_SRC}
  ${LITTLEFS_SRC}
  ${EOS_SRC}
  ${EOS_RESOURCE_FONT_SRC}
)

target_include_directories(${PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/Inc
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/CMSIS_Core/Include
  ${STM32_DEVICE_DIR}/Include
  ${STM32_HAL_DIR}/Inc
  ${STM32_BSP_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/Components/Common
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/Components/rk050hr18
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/Components/gt911
  ${MX66_COMPONENT_DIR}
  ${LITTLEFS_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
  ${LVGL_DIR}
  ${LVGL_DIR}/src
  ${EOS_INCLUDE_DIRS}
  ${EOS_DIR}/resources
  ${EOS_DIR}/third_party/cJSON
  ${EOS_DIR}/third_party/jerryscript/jerry-core/include
  ${EOS_DIR}/third_party/jerryscript/jerry-ext/include
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
  STM32U5
  STM32U5G9xx
  USE_HAL_DRIVER
  LV_CONF_INCLUDE_SIMPLE
  EOS_SYS_ROOT_DIR="/"
  EOS_PLATFORM_STM32U5
  LFS_DEFINES=eos_littlefs_config.h
  EOS_USE_ASSERT=0
)

target_compile_options(${PROJECT_NAME} PRIVATE
  # Keep source-level debugging while allowing the MCU and GPU submission
  # path to run at a useful speed during bring-up.
  $<$<CONFIG:Debug>:-O3>
  -Wno-unused-function
  -Wno-unused-variable
  -Wno-sign-compare
  -Wno-missing-field-initializers
)

add_library(lvgl STATIC ${LVGL_SRC})
target_compile_options(lvgl PRIVATE
  ${CPU_FLAGS} ${CC_SECURE}
  $<$<CONFIG:Debug>:-O3>
)
target_include_directories(lvgl PUBLIC
  ${LVGL_DIR}
  ${LVGL_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/Inc
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party/CMSIS_Core/Include
  ${STM32_DEVICE_DIR}/Include
  ${STM32_HAL_DIR}/Inc
)
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)
target_compile_definitions(lvgl PUBLIC
  STM32U5
  STM32U5G9xx
  USE_HAL_DRIVER
)

add_library(cjson STATIC ${EOS_DIR}/third_party/cJSON/cJSON.c)
target_compile_options(cjson PRIVATE
  ${CPU_FLAGS} ${CC_SECURE}
  $<$<CONFIG:Debug>:-O3>
)
target_include_directories(cjson PUBLIC ${EOS_DIR}/third_party/cJSON)

set(JERRY_VM_HALT ON CACHE INTERNAL "Enable VM halt feature")
set(JERRY_LINE_INFO OFF CACHE INTERNAL "Enable line info feature")
set(JERRY_ERROR_MESSAGES ON CACHE INTERNAL "Enable error message strings")
set(JERRY_MEM_STATS OFF CACHE INTERNAL "Disable JerryScript memory statistics")
set(JERRY_CMDLINE OFF CACHE BOOL "Disable JerryScript command line" FORCE)
set(JERRY_CMDLINE_TEST OFF CACHE BOOL "Disable JerryScript tests" FORCE)
set(JERRY_CMDLINE_SNAPSHOT OFF CACHE BOOL "Disable JerryScript snapshot tool" FORCE)
set(JERRY_LIBFUZZER OFF CACHE BOOL "Disable JerryScript libfuzzer" FORCE)
set(UNITTESTS OFF CACHE BOOL "Disable JerryScript unit tests" FORCE)
set(DOCTESTS OFF CACHE BOOL "Disable JerryScript doc tests" FORCE)
set(ENABLE_AMALGAM OFF CACHE BOOL "Disable JerryScript amalgam" FORCE)
set(ENABLE_LTO OFF CACHE BOOL "Disable JerryScript LTO for arm-none-eabi GCC compatibility" FORCE)
set(JERRY_PORT ON CACHE BOOL "Build JerryScript port" FORCE)
set(FLAGS_COMMON_ARCH ${CPU_FLAGS})
add_subdirectory(${EOS_DIR}/third_party/jerryscript)

target_link_libraries(${PROJECT_NAME} PRIVATE
  lvgl cjson jerry-core jerry-ext jerry-port
  ${NEMA_GFX_LIB}
  # NemaGFX calls the STM32 HAL adapter implemented inside LVGL. Repeat LVGL
  # after the Nema archive so GNU ld resolves this static-library cycle.
  lvgl
  # The LVGL HAL adapter also references the TSI allocator object inside
  # NemaGFX, so close the archive cycle with a second NemaGFX pass.
  ${NEMA_GFX_LIB}
)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/stm32u5g9xj_flash.ld
               ${CMAKE_CURRENT_BINARY_DIR}/stm32u5g9xj_flash.ld COPYONLY)
set_target_properties(${PROJECT_NAME} PROPERTIES
  LINK_DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/stm32u5g9xj_flash.ld
  SUFFIX ".elf")

target_link_options(${PROJECT_NAME} PRIVATE
  ${CPU_FLAGS}
  -T${CMAKE_CURRENT_BINARY_DIR}/stm32u5g9xj_flash.ld
  -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.map
  --specs=nosys.specs
  -Wl,--start-group -lc -lm -lgcc -Wl,--end-group
  -Wl,-z,max-page-size=8
  -Wl,--gc-sections
  -Wl,--print-memory-usage
)
