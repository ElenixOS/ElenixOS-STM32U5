/* ElenixOS allocation hooks for LittleFS. */
#ifndef EOS_LITTLEFS_CONFIG_H
#define EOS_LITTLEFS_CONFIG_H

#include "eos_mem.h"

#define LFS_MALLOC(size) eos_malloc(size)
#define LFS_FREE(ptr) eos_free(ptr)
#define LFS_NO_DEBUG
#define LFS_NO_WARN
#define LFS_NO_TRACE

#endif /* EOS_LITTLEFS_CONFIG_H */
