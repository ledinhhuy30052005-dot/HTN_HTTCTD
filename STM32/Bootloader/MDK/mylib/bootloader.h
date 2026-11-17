#ifndef __BOOTLOADER_H
#define __BOOTLOADER_H
 
#include "stm32f10x.h"
#include "flash.h"          /* FW_START_ADDR, FW_END_ADDR, FLASH_OK ... */
 
/* Vung Application (trung voi flash.h: trang 50, 0x0800C800) */
#define ADDR_APP_PROGRAM    FW_START_ADDR
 
/* Giao thuc voi ESP32 */
#define START_BYTE          0xAA
#define ACK                 0x79
#define NACK                0x1F
#define BLOCK_SIZE          256
 
/* Sau reset: cho ESP32 gui byte 0xAA (lenh cap nhat) trong khoang nay (ms).
 * Het thoi gian ma khong co lenh -> chay App cu. */
#define BOOT_WAIT_MS        10000UL
 
/* LED bao nap firmware thanh cong (cung chan LED voi App: PA0, muc 1 = sang).
 * Neu LED cua ban noi kieu active-low thi doi LED_ON_LEVEL thanh 0. */
#define LED_GPIO            GPIOC
#define LED_PIN             GPIO_Pin_13
#define LED_RCC             RCC_APB2Periph_GPIOC
#define LED_ON_LEVEL        1
#define LED_SUCCESS_MS      2000UL
 
void    bootloader_timer_init(void);                  /* SysTick 1ms, dung polling, khong ngat */
void    bootloader_led_init(void);                    /* PA0 output push-pull, LED tat          */
void    bootloader_led_success(void);                 /* LED sang LED_SUCCESS_MS (2s) roi tat   */
uint8_t bootloader_wait_request(uint32_t timeout_ms); /* 1 = nhan duoc byte 0xAA                */
uint8_t bootloader_loop(void);                        /* nhan firmware va ghi flash; 1 = thanh cong.
                                                         Chi thoat khi nhan xong goi ket thuc    */
void    enter_to_application(void);                   /* nhay vao App, khong quay lai           */
 
#endif /* __BOOTLOADER_H */