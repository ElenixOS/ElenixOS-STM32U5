# ElenixOS-STM32U5

An ElenixOS port for the **STM32U5G9J-DK2** discovery board. The project targets the STM32U5G9ZJT6Q (Cortex-M33) and integrates LVGL, NemaGFX GPU acceleration, LittleFS on external NOR Flash, touch input, the user button, and ST-LINK VCP logging.

## Features

- ElenixOS bare-metal main loop and built-in applications
- LVGL 9 user interface with RGB565 color format
- STM32U5 GPU2D/NemaGFX accelerated rendering
- 390 x 450 logical display area centered on the physical 800 x 480 LCD
- Touchscreen and USER button input
- LittleFS storage backed by the board's OctoSPI NOR Flash (MX66UW1G45G)
- Built-in font resources copied from Flash to RAM during startup
- ST-LINK virtual COM port logging at 115200 8N1
- CMake + Ninja + GNU Arm Embedded Toolchain build system

## Hardware and Software Requirements

### Hardware

- [STM32U5G9J-DK2](https://www.st.com/en/evaluation-tools/stm32u5g9j-dk2.html) discovery board
- USB data cable for power, ST-LINK programming/debugging, and VCP logs

### Software

- Git
- CMake 3.20 or later
- Ninja
- GNU Arm Embedded Toolchain with `arm-none-eabi-gcc` available in `PATH`
- STM32CubeProgrammer for programming and debugging
- Python 3 for the ST-LINK VCP monitor

When using STM32Cube for VS Code, the repository's `.vscode/tasks.json` and `.vscode/launch.json` can be used directly. The project configuration also supports the `cube-cmake` and `cube programmer` commands provided by the STM32Cube toolchain environment.

## Getting the Source

The project manages the ElenixOS core and external dependencies as Git submodules. Initialize all submodules when cloning for the first time:

```bash
git clone --recurse-submodules <repository-url>
cd ElenixOS-STM32U5
```

For an existing checkout:

```bash
git submodule update --init --recursive
```

## Building

Build the Debug configuration using the CMake preset:

```bash
cmake --preset Debug
cmake --build --preset Debug
```

Build the Release configuration:

```bash
cmake --preset Release
cmake --build --preset Release
```

The generated ELF files are located at:

```text
build/Debug/ElenixOS-STM32U5.elf
build/Release/ElenixOS-STM32U5.elf
```

If Ninja is not available, a regular CMake generator can be used instead. Make sure `arm-none-eabi-gcc`, `arm-none-eabi-g++`, and the other GNU Arm tools are available in `PATH`.

## Programming the Board

After connecting the board, use STM32Cube Programmer over SWD to program the Debug image:

```bash
cube programmer -c port=SWD \
  -w build/Debug/ElenixOS-STM32U5.elf \
  -v -rst
```

Alternatively, select **Download ElenixOS STM32U5 (ST-LINK)** in the VS Code Run and Debug panel. This configuration builds the project, downloads the ELF file, verifies it, and resets the target.

## VS Code Debugging and Download

The repository includes ready-to-use VS Code tasks and launch configurations in `.vscode/`.

### Preparation

Install the STM32Cube for VS Code extension/toolchain and STM32CubeProgrammer. Make sure the following are available through the STM32Cube environment:

- `cube-cmake`
- `cube programmer`
- ST-LINK GDB support (`stlinkgdbtarget`)
- GNU Arm Embedded Toolchain

Open the repository root in VS Code and connect the STM32U5G9J-DK2 board through USB.

### Build in VS Code

Use **Terminal > Run Build Task** and select **Build ElenixOS STM32U5**. The task will configure the Debug build and compile the project. It runs the equivalent of:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel 4
```

The VS Code task produces:

```text
build/ElenixOS-STM32U5.elf
```

This path is different from the CMake Preset output path (`build/Debug/ElenixOS-STM32U5.elf`). Use the VS Code build task when using the provided download and debug launch configurations.

### Download and Run

1. Open the **Run and Debug** panel.
2. Select **Download ElenixOS STM32U5 (ST-LINK)**.
3. Press `F5` or click the start button.

No separate build action is required. VS Code automatically runs the **Build ElenixOS STM32U5** pre-launch task, then downloads `build/ElenixOS-STM32U5.elf` over SWD, verifies the image, and resets the MCU.

### Start a Debug Session

1. Set breakpoints in files such as `Src/main.c`.
2. Select **Debug ElenixOS STM32U5 (ST-LINK)** in the **Run and Debug** panel.
3. Press `F5`.

VS Code automatically builds the project before starting the debug session. The configuration targets the STM32U5G9ZJT6Q Cortex-M33 with TrustZone disabled, uses SWD at 8 MHz, loads symbols from `build/ElenixOS-STM32U5.elf`, and starts execution at `main`.

The **Build ElenixOS STM32U5** task is configured as a pre-launch task for both download and debug configurations, so the ELF image is rebuilt before each launch.

## Debugging and Serial Logs

The repository includes a dependency-free ST-LINK VCP monitor:

```bash
python3 tools/vcp_monitor.py
```

The monitor automatically searches common serial device paths. To select a port explicitly:

```bash
STLINK_VCP_PORT=/dev/cu.usbmodemXXXX python3 tools/vcp_monitor.py
```

Stop a running monitor with:

```bash
python3 tools/vcp_monitor.py --stop
```

The serial configuration is **115200 baud, 8 data bits, no parity, 1 stop bit (115200 8N1)**. VS Code can also start the monitor automatically when the workspace is opened.

## Project Structure

```text
.
├── Inc/                         # Application and platform configuration headers
├── Src/                         # Main program, STM32U5 ports, and interrupt handlers
├── cmake/                       # Target, compiler flag, and toolchain configuration
├── third_party/
│   ├── ElenixOS/                # ElenixOS core and built-in applications
│   ├── lvgl/                    # LVGL graphics library
│   ├── littlefs/                # LittleFS filesystem
│   ├── STM32U5xx_HAL_Driver/   # STM32U5 HAL
│   ├── CMSIS_Core/              # CMSIS-Core
│   ├── CMSIS_Device_STM32U5xx/  # STM32U5 device support
│   ├── STM32U5G9J-DK2/          # Discovery board BSP
│   └── Components/              # LCD, touch, and NOR Flash drivers
├── tools/vcp_monitor.py         # ST-LINK VCP log monitor
├── CMakeLists.txt
├── CMakePresets.json
└── stm32u5g9xj_flash.ld         # Linker script
```

## Platform Configuration

Common platform settings are defined in `Inc/eos_platform_config.h` and `Inc/lv_conf.h`:

- `EOS_DISPLAY_WIDTH` / `EOS_DISPLAY_HEIGHT`: ElenixOS logical display size
- `EOS_FS_TYPE`: filesystem type, currently LittleFS
- `EOS_CONFIG_ANIM_SNAPSHOT_ENABLED`: full-screen transition snapshot support
- `LV_COLOR_DEPTH`: currently 16-bit RGB565
- `LV_USE_NEMA_GFX`: NemaGFX support, currently enabled
- `LV_MEM_SIZE`: LVGL built-in memory pool size, currently 512 KiB

On first use, an external NOR Flash detected as blank is automatically formatted as LittleFS. Formatting erases any existing data on that Flash.

## Port and Adapter Files

- `Src/main.c`: clock, LCD, touch, button, GPU, LVGL, and ElenixOS initialization
- `Src/eos_port_stm32u5.c`: ElenixOS STM32U5 platform port and cache allocation
- `Src/eos_lvgl_draw_buf_port.c`: LVGL draw-buffer allocation port
- `Src/eos_nema_gfx_stm32_hal.c`: NemaGFX HAL adapter and GPU timeout recovery
- `Src/eos_fs_port_stm32u5.c`: LittleFS port backed by OctoSPI NOR Flash
- `Src/eos_vcp.c`: ST-LINK VCP initialization and output

## ElenixOS Core

This repository is part of the [ElenixOS](https://github.com/ElenixOS) organization. `third_party/ElenixOS` contains the ElenixOS core and built-in applications used by this board port. It is an organization-owned project component rather than a third-party dependency.

## Third-Party Dependencies

Main dependencies and their upstream repositories:

- [LVGL](https://github.com/lvgl/lvgl)
- [LittleFS](https://github.com/littlefs-project/littlefs)
- [STM32U5xx HAL Driver](https://github.com/STMicroelectronics/stm32u5xx_hal_driver)
- [CMSIS-Core](https://github.com/STMicroelectronics/cmsis_core)
- [CMSIS Device STM32U5xx](https://github.com/STMicroelectronics/cmsis_device_u5)
- [STM32U5G9J-DK2 BSP](https://github.com/STMicroelectronics/stm32u5g9j-dk2-bsp)

When using or distributing this project, comply with the license requirements of all third-party dependencies.
