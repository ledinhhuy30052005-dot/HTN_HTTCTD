#ifndef __FLASH_H
#define __FLASH_H

#include "stm32f10x.h"
#include "stm32f10x_flash.h"

/* ---- Bo nho (STM32F103, medium density, 1 page = 1KB) --------------
 * Bootloader : 0x08000000 .. 0x0800C7FF  (trang 0..49)
 * Firmware   : 0x0800C800 .. cuoi flash   (trang 50..127)
 * ------------------------------------------------------------------- */
#define FLASH_BASE_ADDR     0x08000000UL
#define FW_PAGE_SIZE        1024UL
#define FW_START_PAGE       50UL
#define FW_END_PAGE         125UL
#define FW_START_ADDR       (FLASH_BASE_ADDR + FW_START_PAGE * FW_PAGE_SIZE) //50*1024 = 51200 dec=> hex = 0xC800/* 0x0800C800 */ page 50 
#define FW_END_ADDR         (FLASH_BASE_ADDR + (FW_END_PAGE + 1UL) * FW_PAGE_SIZE)  /* khong gom */

/* Ma tra ve */
#define FLASH_OK            0
#define FLASH_ERR           1

void    flash_unlock(void);
void    flash_lock(void);

/* Xoa toan bo vung firmware (trang FW_START_PAGE .. FW_END_PAGE).
 * Tra ve FLASH_OK / FLASH_ERR. Phai goi flash_unlock() truoc. */
uint8_t flash_erase_fw(void);

/* Xoa 1 trang 1KB trong vung firmware. page_addr phai la dia chi dau trang.
 * Tra ve FLASH_OK / FLASH_ERR. Phai goi flash_unlock() truoc. */
uint8_t flash_erase_page(uint32_t page_addr);

/* Ghi len byte tu data vao flash tai dia chi addr (addr phai chan, nam trong vung firmware).
 * Neu len le, byte cuoi duoc dem 0xFF. Ghi xong doc lai kiem tra.
 * Tra ve FLASH_OK / FLASH_ERR. Phai goi flash_unlock() truoc va vung ghi phai da duoc xoa. */
uint8_t flash_write_fw(uint32_t addr, const uint8_t *data, uint16_t len);

#endif /* __FLASH_H */