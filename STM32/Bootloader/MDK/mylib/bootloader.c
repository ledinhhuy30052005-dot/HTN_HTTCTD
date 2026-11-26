#include "bootloader.h"
#include "uart_lib.h"
#include "flash.h"

typedef void (*pFunction)(void);

/* ===================== Timer ms (SysTick, polling) =====================
 * uart_lib khong co timeout nen bootloader tu do COUNTFLAG cua SysTick.
 * SysTick chay clock HCLK, khong bat ngat. */
void bootloader_timer_init(void)
{
    SysTick->CTRL = 0;
    SysTick->LOAD = (SystemCoreClock / 1000UL) - 1UL;   /* 1ms */
    SysTick->VAL  = 0;
    SysTick->CTRL = (1UL << 2) | (1UL << 0);            /* CLKSOURCE = HCLK, ENABLE */
}

/* Cho ms mili-giay (polling COUNTFLAG cua SysTick) */
static void delay_ms(uint32_t ms)
{
    (void)SysTick->CTRL;                                /* doc de xoa COUNTFLAG */
    while (ms != 0)
    {
        if (SysTick->CTRL & (1UL << 16))
        {
            ms--;
        }
    }
}

/* ===================== LED bao hieu ==================================== */
static void led_set(uint8_t on)
{
    if (on == LED_ON_LEVEL)
    {
        GPIO_SetBits(LED_GPIO, LED_PIN);
    }
    else
    {
        GPIO_ResetBits(LED_GPIO, LED_PIN);
    }
}

void bootloader_led_init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(LED_RCC, ENABLE);

    gpio.GPIO_Pin   = LED_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LED_GPIO, &gpio);

    led_set(LED_ON_LEVEL);                             /* LED tat */
}

void bootloader_led_success(void)
{
    led_set(!LED_ON_LEVEL);                              /* sang */
    delay_ms(LED_SUCCESS_MS);                           /* 2 giay */
    led_set(LED_ON_LEVEL);                             /* tat */
}

/* ===================== UART (dung uart_lib + ham phu) ================== */
static void send_byte(uint8_t byte)
{
    UART_SendChar(USART1, (char)byte);
    while (!(USART1->SR & USART_SR_TC)) {}              /* gui xong han moi di tiep */
}

/* Tra ve 0 = OK, 1 = timeout (timeout tinh cho TUNG byte, ms) */
static uint8_t receive_byte(uint8_t *byte, uint32_t timeout_ms)
{
    (void)SysTick->CTRL;                                /* doc de xoa COUNTFLAG */

    while (1)
    {
        if (USART1->SR & USART_SR_RXNE)
        {
            *byte = (uint8_t)USART1->DR;                /* doc SR roi DR: xoa luon co ORE */
            return 0;
        }
        if (SysTick->CTRL & (1UL << 16))                /* COUNTFLAG: troi qua 1ms */
        {
            if (timeout_ms == 0)
            {
                return 1;
            }
            timeout_ms--;
        }
    }
}

static uint8_t receive_buf(uint8_t *buf, uint16_t len, uint32_t timeout_ms)
{
    uint16_t i;
    for (i = 0; i < len; i++)
    {
        if (receive_byte(&buf[i], timeout_ms) != 0)
        {
            return 1;
        }
    }
    return 0;
}

/* Bo het byte rac cho den khi duong truyen im lang idle_ms */
static void flush_rx(uint32_t idle_ms)
{
    uint8_t dummy;
    while (receive_byte(&dummy, idle_ms) == 0) {}
}

/* ===================== Nhay vao Application ============================ */
void enter_to_application(void)
{
    uint32_t  app_sp    = *(__IO uint32_t *)ADDR_APP_PROGRAM;
    pFunction app_entry = (pFunction)(*(__IO uint32_t *)(ADDR_APP_PROGRAM + 4));

    /* Tra ngoai vi dung ve trang thai reset (thay HAL_DeInit) */
    RCC->APB2RSTR |=  (RCC_APB2RSTR_USART1RST | RCC_APB2RSTR_IOPARST);
    RCC->APB2RSTR &= ~(RCC_APB2RSTR_USART1RST | RCC_APB2RSTR_IOPARST);

    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;
    SCB->ICSR     = (1UL << 25);                        /* PENDSTCLR */

    RCC_DeInit();                                       /* thay HAL_RCC_DeInit */

    /* Tat UsageFault / BusFault / MemManage: bit 18, 17, 16 cua SCB->SHCSR */
    SCB->SHCSR &= ~(7UL << 16);

    __set_MSP(app_sp);
    app_entry();

    while (1) {}
}

/* ===================== Giao thuc nhan firmware ========================= */
static uint8_t  header[3];
static uint8_t  data[BLOCK_SIZE];
static uint8_t  crc_buf[2];
static uint16_t crc_calc;
static uint16_t crc_recv;

/* Giu nguyen thuat toan cua ESP32: cong don cac byte (checksum 16 bit) */
static uint16_t simpleCRC(uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0;
    uint16_t i;
    for (i = 0; i < len; i++)
    {
        crc += buf[i];
    }
    return crc;
}

/* Byte 0xAA dau tien (lenh cap nhat) bi lay o wait_request, bootloader_loop dung lai
 * no lam byte dau cua goi dau tien -> goi dau khong bi mat. */
static uint8_t s_have_start;

/* Cho 0xAA lam lenh cap nhat. Tra ve 1 neu nhan duoc, 0 neu het timeout. */
uint8_t bootloader_wait_request(uint32_t timeout_ms)
{
    uint8_t  b;
    uint32_t t = 0;

    while (t < timeout_ms)
    {
        if (receive_byte(&b, 0) == 0)                   /* timeout 0 = toi da ~1ms */
        {
            if (b == START_BYTE)
            {
                s_have_start = 1;
                return 1;
            }
        }
        else
        {
            t++;
        }
    }
    return 0;
}

/* Nhan firmware tu ESP32 va ghi vao flash.
 *
 *  - Bat dau khi da nhan duoc lenh cap nhat (0xAA). Firmware cu bi xoa dan: moi page duoc
 *    xoa ngay truoc khi ghi vao no -> goi dau duoc ACK ngay, ESP32 khong phai cho/gui lai.
 *  - Goi loi (timeout, sai do dai, sai checksum, loi ghi flash): xa byte rac, gui NACK
 *    va TIEP TUC cho ESP32 gui lai goi do. Khong thoat ra ngoai.
 *  - Chi thoat khi nhan goi ket thuc (AA 00 00) -> gui ACK -> tra ve 1 (thanh cong).
 *    Neu chua tung ghi goi nao thi tra ve 0 (chua dung vao firmware cu).
 */
uint8_t bootloader_loop(void)
{
    uint32_t app_address = ADDR_APP_PROGRAM;
    uint32_t erased_end  = ADDR_APP_PROGRAM;            /* dia chi ke tiep chua xoa */
    uint16_t len;

    header[0] = 0;
    header[1] = 0;
    header[2] = 0;

    flash_unlock();

    while (1)
    {
        if (s_have_start)                               /* 0xAA da nhan o buoc cho lenh */
        {
            header[0]    = START_BYTE;
            s_have_start = 0;
        }
        else if (receive_byte(&header[0], 5000) != 0 || header[0] != START_BYTE)
        {
            continue;                                   /* van o trang thai cho ESP32 */
        }

        if (receive_buf(&header[1], 2, 1000) != 0)
        {
            flush_rx(20);
            send_byte(NACK);
            continue;
        }

        len = ((uint16_t)header[1] << 8) | header[2];
        if (len > BLOCK_SIZE)
        {
            flush_rx(20);
            send_byte(NACK);
            continue;
        }

        if (len == 0)                                   /* goi ket thuc: AA 00 00 */
        {
            send_byte(ACK);
            break;
        }

        if (receive_buf(data, len, 1000) != 0)
        {
            flush_rx(20);
            send_byte(NACK);
            continue;
        }

        if (receive_buf(crc_buf, 2, 500) != 0)
        {
            flush_rx(20);
            send_byte(NACK);
            continue;
        }

        crc_recv = ((uint16_t)crc_buf[0] << 8) | crc_buf[1];
        crc_calc = simpleCRC(data, len);
        if (crc_recv != crc_calc)
        {
            send_byte(NACK);
            continue;
        }

        /* Vuot qua vung firmware */
        if ((app_address + len) > FW_END_ADDR)
        {
            send_byte(NACK);
            continue;
        }

        /* Xoa firmware cu tung page, ngay truoc khi ghi vao page do */
        while ((app_address + len) > erased_end)
        {
            if (flash_erase_page(erased_end) != FLASH_OK)
            {
                break;
            }
            erased_end += FW_PAGE_SIZE;
        }
        if ((app_address + len) > erased_end)
        {
            send_byte(NACK);
            continue;
        }

        if (flash_write_fw(app_address, data, len) != FLASH_OK)
        {
            send_byte(NACK);
            continue;
        }
        app_address += len;

        send_byte(ACK);
    }

    flash_lock();

    return (app_address > ADDR_APP_PROGRAM) ? 1 : 0;
}