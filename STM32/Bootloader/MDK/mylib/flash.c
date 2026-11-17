#include "flash.h"

void flash_unlock(void)
{
    FLASH_Unlock();
    /* Xoa cac co loi cu de tranh lam hong lan thao tac dau tien */
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
}

void flash_lock(void)
{
    FLASH_Lock();
}

uint8_t flash_erase_fw(void)
{
    uint32_t page;
    uint32_t addr;

    for (page = FW_START_PAGE; page <= FW_END_PAGE; page++)
    {
        addr = FLASH_BASE_ADDR + page * FW_PAGE_SIZE;
        if (FLASH_ErasePage(addr) != FLASH_COMPLETE)
        {
            return FLASH_ERR;
        }
    }
    return FLASH_OK;
}

uint8_t flash_erase_page(uint32_t page_addr)
{
    /* Phai la dia chi dau trang va nam trong vung firmware */
    if ((page_addr % FW_PAGE_SIZE) != 0 ||
        page_addr < FW_START_ADDR || page_addr >= FW_END_ADDR)
    {
        return FLASH_ERR;
    }
    return (FLASH_ErasePage(page_addr) == FLASH_COMPLETE) ? FLASH_OK : FLASH_ERR;
}

uint8_t flash_write_fw(uint32_t addr, const uint8_t *data, uint16_t len)
{
    uint16_t i;
    uint16_t halfword;

    /* STM32F1 chi ghi theo half-word => dia chi phai chan */
    if ((addr & 0x01UL) != 0 || data == 0)
    {
        return FLASH_ERR;
    }
    /* Chi cho phep ghi trong vung firmware */
    if (addr < FW_START_ADDR || (addr + len) > FW_END_ADDR)
    {
        return FLASH_ERR;
    }

    for (i = 0; i < len; i += 2)
    {
        halfword = data[i];                               /* byte thap */
        if ((uint16_t)(i + 1) < len)
        {
            halfword |= (uint16_t)data[i + 1] << 8;       /* byte cao  */
        }
        else
        {
            halfword |= 0xFF00;                           /* dem 0xFF neu len le */
        }

        if (FLASH_ProgramHalfWord(addr + i, halfword) != FLASH_COMPLETE)
        {
            return FLASH_ERR;
        }
        if (*(volatile uint16_t *)(addr + i) != halfword) /* doc lai kiem tra */
        {
            return FLASH_ERR;
        }
    }
    return FLASH_OK;
}