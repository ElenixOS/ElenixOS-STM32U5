/* STM32U5G9J-DK2 configuration for the first display bring-up. */
#ifndef EOS_PLATFORM_CONFIG_H
#define EOS_PLATFORM_CONFIG_H

#include <stdint.h>
#include <stdio.h>

#define EOS_USE_VIRTUAL_DISPLAY 0
#define EOS_DISPLAY_WIDTH 390
#define EOS_DISPLAY_HEIGHT 450

/* Use raster snapshots for full-screen transitions.  The U5 port reserves two
 * fixed RGB565 screen buffers, so snapshot creation does not depend on heap
 * contiguity. */
#define EOS_CONFIG_ANIM_SNAPSHOT_ENABLED 1

/* Use the board's 128 MiB OctoSPI NOR as a LittleFS volume. */
#define EOS_FS_TYPE EOS_FS_LITTLEFS
#define EOS_PLATFORM_NO_FILESYSTEM 0

/* Built-in LVGL fonts keep the first image small and require no font FS. */
#define EOS_FONT_TYPE EOS_FONT_C_MULTI
#define EOS_ENABLE_CHINESE_FONT 0

#endif /* EOS_PLATFORM_CONFIG_H */
