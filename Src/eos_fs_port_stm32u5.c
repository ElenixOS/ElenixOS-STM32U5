/**
 * @file eos_fs_port_stm32u5.c
 * @brief LittleFS port backed by the STM32U5G9J-DK2 OctoSPI NOR.
 */

#include "eos_fs_port_stm32u5.h"

#define EOS_LOG_TAG "FsSTM32U5"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "eos_fs_port.h"
#include "eos_mem.h"
#include "eos_log.h"
#include "lfs.h"
#include "stm32u5g9j_discovery_hspi.h"

#define EOS_FS_FLASH_INSTANCE 0U
#define EOS_FS_BLOCK_SIZE MX66UW1G45G_BLOCK_4K
#define EOS_FS_CACHE_SIZE 256U
#define EOS_FS_LOOKAHEAD_SIZE 256U
#define EOS_FS_BLOCK_COUNT (MX66UW1G45G_FLASH_SIZE / EOS_FS_BLOCK_SIZE)

static lfs_t s_lfs;
static struct lfs_config s_lfs_config;
static bool s_mounted;
static uint8_t s_read_cache[EOS_FS_CACHE_SIZE];
static uint8_t s_prog_cache[EOS_FS_CACHE_SIZE];
static uint8_t s_lookahead[EOS_FS_LOOKAHEAD_SIZE];
static uint8_t s_probe[EOS_FS_BLOCK_SIZE];

static uint32_t _eos_fs_flash_address(lfs_block_t block, lfs_off_t offset)
{
    return ((uint32_t)block * EOS_FS_BLOCK_SIZE) + (uint32_t)offset;
}

static int _eos_fs_lfs_read(const struct lfs_config *config,
                            lfs_block_t block, lfs_off_t offset,
                            void *buffer, lfs_size_t size)
{
    (void)config;
    return BSP_HSPI_NOR_Read(EOS_FS_FLASH_INSTANCE, buffer,
                             _eos_fs_flash_address(block, offset), size) == BSP_ERROR_NONE
               ? 0
               : LFS_ERR_IO;
}

static int _eos_fs_lfs_prog(const struct lfs_config *config,
                            lfs_block_t block, lfs_off_t offset,
                            const void *buffer, lfs_size_t size)
{
    (void)config;
    return BSP_HSPI_NOR_Write(EOS_FS_FLASH_INSTANCE, (uint8_t *)buffer,
                              _eos_fs_flash_address(block, offset), size) == BSP_ERROR_NONE
               ? 0
               : LFS_ERR_IO;
}

static int _eos_fs_lfs_erase(const struct lfs_config *config, lfs_block_t block)
{
    (void)config;
    return BSP_HSPI_NOR_Erase_Block(EOS_FS_FLASH_INSTANCE,
                                    _eos_fs_flash_address(block, 0U),
                                    BSP_HSPI_NOR_ERASE_4K) == BSP_ERROR_NONE
               ? 0
               : LFS_ERR_IO;
}

static int _eos_fs_lfs_sync(const struct lfs_config *config)
{
    (void)config;
    return BSP_HSPI_NOR_GetStatus(EOS_FS_FLASH_INSTANCE) == BSP_ERROR_NONE
               ? 0
               : LFS_ERR_IO;
}

static bool _eos_fs_flash_is_erased(void)
{
    if (BSP_HSPI_NOR_Read(EOS_FS_FLASH_INSTANCE, s_probe, 0U,
                          sizeof(s_probe)) != BSP_ERROR_NONE)
    {
        return false;
    }

    for (size_t i = 0U; i < sizeof(s_probe); ++i)
    {
        if (s_probe[i] != 0xFFU)
        {
            return false;
        }
    }
    return true;
}

static void _eos_fs_lfs_configure(void)
{
    memset(&s_lfs_config, 0, sizeof(s_lfs_config));
    s_lfs_config.read = _eos_fs_lfs_read;
    s_lfs_config.prog = _eos_fs_lfs_prog;
    s_lfs_config.erase = _eos_fs_lfs_erase;
    s_lfs_config.sync = _eos_fs_lfs_sync;
    s_lfs_config.read_size = 16U;
    s_lfs_config.prog_size = MX66UW1G45G_PAGE_SIZE;
    s_lfs_config.block_size = EOS_FS_BLOCK_SIZE;
    s_lfs_config.block_count = EOS_FS_BLOCK_COUNT;
    s_lfs_config.block_cycles = 500;
    s_lfs_config.cache_size = EOS_FS_CACHE_SIZE;
    s_lfs_config.lookahead_size = EOS_FS_LOOKAHEAD_SIZE;
    s_lfs_config.read_buffer = s_read_cache;
    s_lfs_config.prog_buffer = s_prog_cache;
    s_lfs_config.lookahead_buffer = s_lookahead;
}

eos_result_t eos_fs_port_init(void)
{
    if (s_mounted)
    {
        return EOS_OK;
    }

    BSP_HSPI_NOR_Init_t init = {
        .InterfaceMode = BSP_HSPI_NOR_SPI_MODE,
        .TransferRate = BSP_HSPI_NOR_STR_TRANSFER,
    };
    if (BSP_HSPI_NOR_Init(EOS_FS_FLASH_INSTANCE, &init) != BSP_ERROR_NONE)
    {
        EOS_LOG_E("NOR initialization failed");
        return EOS_ERR_IO;
    }

    _eos_fs_lfs_configure();
    int ret = lfs_mount(&s_lfs, &s_lfs_config);
    if (ret != 0 && _eos_fs_flash_is_erased())
    {
        EOS_LOG_I("Formatting blank NOR for LittleFS");
        ret = lfs_format(&s_lfs, &s_lfs_config);
        if (ret == 0)
        {
            ret = lfs_mount(&s_lfs, &s_lfs_config);
        }
    }

    if (ret != 0)
    {
        EOS_LOG_E("LittleFS mount failed: %d", ret);
        return EOS_ERR_IO;
    }

    s_mounted = true;
    return EOS_OK;
}

void eos_fs_set_root(const char *root)
{
    (void)root;
}

const char *eos_fs_realpath(const char *path, char *buf, size_t bufsz)
{
    if (path == NULL || buf == NULL || bufsz == 0U)
    {
        return NULL;
    }
    size_t i = 0U;
    while (i + 1U < bufsz && path[i] != '\0')
    {
        buf[i] = path[i];
        ++i;
    }
    buf[i] = '\0';
    return buf;
}

eos_file_t eos_fs_open_read(const char *path)
{
    if (!s_mounted || path == NULL)
    {
        return EOS_FILE_INVALID;
    }
    lfs_file_t *file = eos_malloc(sizeof(*file));
    if (file == NULL || lfs_file_open(&s_lfs, file, path, LFS_O_RDONLY) != 0)
    {
        eos_free(file);
        return EOS_FILE_INVALID;
    }
    return file;
}

eos_file_t eos_fs_open_write(const char *path)
{
    if (!s_mounted || path == NULL)
    {
        return EOS_FILE_INVALID;
    }
    lfs_file_t *file = eos_malloc(sizeof(*file));
    if (file == NULL ||
        lfs_file_open(&s_lfs, file, path,
                      LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC) != 0)
    {
        eos_free(file);
        return EOS_FILE_INVALID;
    }
    return file;
}

int eos_fs_read(eos_file_t file, void *buf, size_t len)
{
    if (!s_mounted || file == EOS_FILE_INVALID || buf == NULL)
    {
        return -1;
    }
    lfs_ssize_t ret = lfs_file_read(&s_lfs, file, buf, len);
    return ret < 0 ? -1 : (int)ret;
}

int eos_fs_write(eos_file_t file, const void *buf, size_t len)
{
    if (!s_mounted || file == EOS_FILE_INVALID || buf == NULL)
    {
        return -1;
    }
    lfs_ssize_t ret = lfs_file_write(&s_lfs, file, buf, len);
    return ret < 0 ? -1 : (int)ret;
}

eos_result_t eos_fs_seek(eos_file_t file, uint32_t pos)
{
    if (!s_mounted || file == EOS_FILE_INVALID ||
        lfs_file_seek(&s_lfs, file, (lfs_soff_t)pos, LFS_SEEK_SET) < 0)
    {
        return EOS_ERR_IO;
    }
    return EOS_OK;
}

eos_result_t eos_fs_size(eos_file_t file, uint32_t *size)
{
    if (!s_mounted || file == EOS_FILE_INVALID || size == NULL)
    {
        return EOS_ERR_IO;
    }
    lfs_soff_t ret = lfs_file_size(&s_lfs, file);
    if (ret < 0)
    {
        return EOS_ERR_IO;
    }
    *size = (uint32_t)ret;
    return EOS_OK;
}

eos_result_t eos_fs_tell(eos_file_t file, uint32_t *pos)
{
    if (!s_mounted || file == EOS_FILE_INVALID || pos == NULL)
    {
        return EOS_ERR_IO;
    }
    lfs_soff_t ret = lfs_file_tell(&s_lfs, file);
    if (ret < 0)
    {
        return EOS_ERR_IO;
    }
    *pos = (uint32_t)ret;
    return EOS_OK;
}

void eos_fs_close(eos_file_t file)
{
    if (s_mounted && file != EOS_FILE_INVALID)
    {
        (void)lfs_file_close(&s_lfs, file);
        eos_free(file);
    }
}

eos_result_t eos_fs_mkdir(const char *path)
{
    if (!s_mounted || path == NULL)
    {
        return EOS_ERR_IO;
    }
    int ret = lfs_mkdir(&s_lfs, path);
    return ret == 0 || ret == LFS_ERR_EXIST ? EOS_OK : EOS_ERR_IO;
}

eos_result_t eos_fs_rmdir(const char *path)
{
    return eos_fs_remove(path);
}

eos_result_t eos_fs_remove(const char *path)
{
    if (!s_mounted || path == NULL)
    {
        return EOS_ERR_IO;
    }
    return lfs_remove(&s_lfs, path) == 0 ? EOS_OK : EOS_ERR_IO;
}

int eos_fs_exists(const char *path)
{
    return eos_fs_type(path) != EOS_FS_TYPE_NOT_EXIST;
}

int eos_fs_type(const char *path)
{
    if (!s_mounted || path == NULL)
    {
        return EOS_FS_TYPE_NOT_EXIST;
    }
    struct lfs_info info;
    if (lfs_stat(&s_lfs, path, &info) != 0)
    {
        return EOS_FS_TYPE_NOT_EXIST;
    }
    return info.type == LFS_TYPE_DIR ? EOS_FS_TYPE_DIR : EOS_FS_TYPE_FILE;
}

eos_dir_t eos_fs_opendir(const char *path)
{
    if (!s_mounted || path == NULL)
    {
        return EOS_DIR_INVALID;
    }
    lfs_dir_t *dir = eos_malloc(sizeof(*dir));
    if (dir == NULL || lfs_dir_open(&s_lfs, dir, path) != 0)
    {
        eos_free(dir);
        return EOS_DIR_INVALID;
    }
    return dir;
}

eos_result_t eos_fs_readdir(eos_dir_t dir, char *name, size_t max_len)
{
    if (!s_mounted || dir == EOS_DIR_INVALID || name == NULL || max_len == 0U)
    {
        return EOS_ERR_IO;
    }
    struct lfs_info info;
    int ret = lfs_dir_read(&s_lfs, dir, &info);
    if (ret <= 0)
    {
        return EOS_ERR_NOT_FOUND;
    }
    size_t len = strlen(info.name);
    if (len >= max_len)
    {
        return EOS_ERR_PATH_TOO_LONG;
    }
    memcpy(name, info.name, len + 1U);
    return EOS_OK;
}

void eos_fs_closedir(eos_dir_t dir)
{
    if (s_mounted && dir != EOS_DIR_INVALID)
    {
        (void)lfs_dir_close(&s_lfs, dir);
        eos_free(dir);
    }
}

eos_result_t eos_fs_mv(const char *old_path, const char *new_path)
{
    if (!s_mounted || old_path == NULL || new_path == NULL)
    {
        return EOS_ERR_IO;
    }
    return lfs_rename(&s_lfs, old_path, new_path) == 0 ? EOS_OK : EOS_ERR_IO;
}

eos_result_t eos_fs_sync(eos_file_t file)
{
    if (!s_mounted || file == EOS_FILE_INVALID)
    {
        return EOS_ERR_IO;
    }
    return lfs_file_sync(&s_lfs, file) == 0 ? EOS_OK : EOS_ERR_IO;
}
