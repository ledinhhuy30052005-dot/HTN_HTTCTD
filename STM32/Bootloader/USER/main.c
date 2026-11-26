#include "stm32f10x.h"
#include "uart_lib.h"
#include "bootloader.h"

int main(void)
{
    /* SystemInit() da chay trong startup -> SYSCLK = 72MHz (uart_lib gia dinh 72MHz) */
    UART_configure(USART1, 115200);
    bootloader_timer_init();
    bootloader_led_init();

    /* Sau reset: cho lenh cap nhat (byte 0xAA) tu ESP32 trong BOOT_WAIT_MS (10s).
     *  - Co lenh: nhan firmware moi, KHONG thoat cho den khi nap thanh cong.
     *  - Khong co lenh: chay luon firmware cu. */
    if (bootloader_wait_request(BOOT_WAIT_MS))
    {
        if (bootloader_loop())
        {
            bootloader_led_success();       /* nap thanh cong: LED sang 2s */
        }
    }

    enter_to_application();

    while (1) {}
}