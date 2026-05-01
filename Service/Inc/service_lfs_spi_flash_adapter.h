#ifndef SERVICE_LFS_SPI_FLASH_ADAPTER_H
#define SERVICE_LFS_SPI_FLASH_ADAPTER_H

#include "lfs.h"
#include "platform_config.h"

#define SPI_FLASH_BLOCK_SIZE (W25Q128_SECTOR_SIZE)
#define SPI_FLASH_BLOCK_COUNT ((W25Q128_TOTAL_SIZE) / (W25Q128_SECTOR_SIZE))
#define SPI_FLASH_PROG_SIZE (W25Q128_PAGE_SIZE)
#define SPI_FLASH_READ_SIZE 1

extern const struct lfs_config lfs_spi_flash_cfg;
extern struct lfs lfs_instance;

int lfs_spi_flash_init(void);
int lfs_spi_flash_mount(struct lfs *lfs);
int lfs_spi_flash_unmount(struct lfs *lfs);
int lfs_spi_flash_format(struct lfs *lfs);

#endif
