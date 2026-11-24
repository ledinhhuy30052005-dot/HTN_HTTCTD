#include "oled.h"
#include "i2c_bus.h"
#include <string.h>

#define OLED_ADDR_1       0x3CU
#define OLED_ADDR_2       0x3DU
#define OLED_COLUMN_OFFSET 0U
#define OLED_LINE_BYTES   128U
#define OLED_LEFT_CHARS   15U
#define OLED_RIGHT_CHARS  5U
#define OLED_SPLIT_X      93U
#define OLED_RIGHT_X      96U

static uint8_t s_addr = OLED_ADDR_1;
static uint8_t s_ready = 0U;
static uint8_t s_failCount = 0U;
static char s_cache[8][24];
static uint8_t s_cacheValid[8];

/* ASCII 0x20..0x5A, 5x7 font. SSD1306 is monochrome; text colour is set by the panel. */
static const uint8_t s_font[59][5] = {
 {0,0,0,0,0}, {0,0,0x5F,0,0}, {0,7,0,7,0}, {0x14,0x7F,0x14,0x7F,0x14},
 {0x24,0x2A,0x7F,0x2A,0x12}, {0x23,0x13,8,0x64,0x62}, {0x36,0x49,0x55,0x22,0x50},
 {0,5,3,0,0}, {0,0x1C,0x22,0x41,0}, {0,0x41,0x22,0x1C,0}, {0x14,8,0x3E,8,0x14},
 {8,8,0x3E,8,8}, {0,0x50,0x30,0,0}, {8,8,8,8,8}, {0,0x60,0x60,0,0}, {0x20,0x10,8,4,2},
 {0x3E,0x51,0x49,0x45,0x3E}, {0,0x42,0x7F,0x40,0}, {0x42,0x61,0x51,0x49,0x46},
 {0x21,0x41,0x45,0x4B,0x31}, {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39},
 {0x3C,0x4A,0x49,0x49,0x30}, {1,0x71,9,5,3}, {0x36,0x49,0x49,0x49,0x36},
 {6,0x49,0x49,0x29,0x1E}, {0,0x36,0x36,0,0}, {0,0x56,0x36,0,0}, {8,0x14,0x22,0x41,0},
 {0x14,0x14,0x14,0x14,0x14}, {0,0x41,0x22,0x14,8}, {2,1,0x51,9,6},
 {0x32,0x49,0x79,0x41,0x3E}, {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36},
 {0x3E,0x41,0x41,0x41,0x22}, {0x7F,0x41,0x41,0x22,0x1C}, {0x7F,0x49,0x49,0x49,0x41},
 {0x7F,9,9,9,1}, {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,8,8,8,0x7F}, {0,0x41,0x7F,0x41,0},
 {0x20,0x40,0x41,0x3F,1}, {0x7F,8,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
 {0x7F,2,0x0C,2,0x7F}, {0x7F,4,8,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E},
 {0x7F,9,9,9,6}, {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,9,0x19,0x29,0x46},
 {0x46,0x49,0x49,0x49,0x31}, {1,1,0x7F,1,1}, {0x3F,0x40,0x40,0x40,0x3F},
 {0x1F,0x20,0x40,0x20,0x1F}, {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,8,0x14,0x63},
 {7,8,0x70,8,7}, {0x61,0x51,0x49,0x45,0x43}
};

/* Lowercase 5x7 glyphs so the OLED can show labels like Soil, Temp and Pump
 * in mixed case instead of forcing every character to uppercase. */
static const uint8_t s_lowerFont[26][5] = {
    {0,0x20,0x54,0x54,0x78}, {0,0x7F,0x48,0x44,0x38}, {0,0x38,0x44,0x44,0x28},
    {0,0x38,0x44,0x48,0x7F}, {0,0x38,0x54,0x54,0x18}, {0,0x08,0x7E,0x09,0x02},
    {0x0C,0x52,0x52,0x52,0x3E}, {0x7F,0x08,0x04,0x04,0x78}, {0,0x44,0x7D,0x40,0},
    {0x20,0x40,0x44,0x3D,0}, {0x7F,0x10,0x28,0x44,0}, {0,0x41,0x7F,0x40,0},
    {0x7C,0x04,0x18,0x04,0x78}, {0x7C,0x08,0x04,0x04,0x78}, {0x38,0x44,0x44,0x44,0x38},
    {0x7C,0x14,0x14,0x14,0x08}, {0x08,0x14,0x14,0x18,0x7C}, {0x7C,0x08,0x04,0x04,0x08},
    {0x48,0x54,0x54,0x54,0x20}, {0x04,0x3F,0x44,0x40,0x20}, {0x3C,0x40,0x40,0x20,0x7C},
    {0x1C,0x20,0x40,0x20,0x1C}, {0x3C,0x40,0x30,0x40,0x3C}, {0x44,0x28,0x10,0x28,0x44},
    {0x0C,0x50,0x50,0x50,0x3C}, {0x44,0x64,0x54,0x4C,0x44}
};

static const uint8_t *font_for(char c)
{
    if (c >= 'a' && c <= 'z') return s_lowerFont[(uint8_t)c - (uint8_t)'a'];
    if (c < ' ' || c > 'Z') return s_font[0];
    return s_font[(uint8_t)c - 0x20U];
}

static uint8_t oled_send(const uint8_t *data, uint16_t len)
{
    if (!s_ready) return 0U;
    if (I2C1_WriteBytes(s_addr, data, len)) { s_failCount = 0U; return 1U; }
    if (++s_failCount >= 3U) s_ready = 0U;
    return 0U;
}

static uint8_t oled_cmds(const uint8_t *cmds, uint8_t n)
{
    uint8_t packet[32];
    uint8_t i;
    if (n > 30U) n = 30U;
    packet[0] = 0x00U;
    for (i = 0U; i < n; i++) packet[1U + i] = cmds[i];
    return oled_send(packet, (uint16_t)(n + 1U));
}

static uint8_t oled_probe(uint8_t addr)
{
    uint8_t packet[2];
    packet[0] = 0x00U;
    packet[1] = 0xAEU;
    return I2C1_WriteBytes(addr, packet, 2U);
}

static void draw_glyph(uint8_t *line, uint8_t x, char c)
{
    uint8_t k;
    const uint8_t *glyph = font_for(c);
    if ((uint16_t)x + 5U > OLED_LINE_BYTES) return;
    for (k = 0U; k < 5U; k++) line[x + k] = glyph[k];
    if ((uint16_t)x + 5U < OLED_LINE_BYTES) line[x + 5U] = 0U;
}

static uint8_t write_page(uint8_t page, const uint8_t *pixels, const char *cacheKey)
{
    uint8_t cmds[3];
    uint8_t packet[1U + OLED_LINE_BYTES];
    if (!s_ready || page > 7U) return 0U;
    if (s_cacheValid[page] && strcmp(cacheKey, s_cache[page]) == 0) return 1U;
    cmds[0] = (uint8_t)(0xB0U | page);
    cmds[1] = (uint8_t)(OLED_COLUMN_OFFSET & 0x0FU);
    cmds[2] = (uint8_t)(0x10U | (OLED_COLUMN_OFFSET >> 4));
    if (!oled_cmds(cmds, 3U)) return 0U;
    packet[0] = 0x40U;
    memcpy(&packet[1], pixels, OLED_LINE_BYTES);
    if (oled_send(packet, (uint16_t)sizeof(packet)))
    {
        strncpy(s_cache[page], cacheKey, sizeof(s_cache[page]) - 1U);
        s_cache[page][sizeof(s_cache[page]) - 1U] = '\0';
        s_cacheValid[page] = 1U;
        return 1U;
    }
    return 0U;
}

uint8_t OLED_IsReady(void) { return s_ready; }

uint8_t OLED_Init(void)
{
    static const uint8_t initCmds[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x02, 0xA1, 0xC8, 0xDA, 0x12,
        0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF
    };
    uint8_t i;
    s_ready = 0U;
    s_failCount = 0U;
    for (i = 0U; i < 8U; i++) { s_cacheValid[i] = 0U; s_cache[i][0] = '\0'; }
    if (oled_probe(OLED_ADDR_1)) s_addr = OLED_ADDR_1;
    else if (oled_probe(OLED_ADDR_2)) s_addr = OLED_ADDR_2;
    else return 0U;
    s_ready = 1U;
    if (!oled_cmds(initCmds, (uint8_t)sizeof(initCmds))) return 0U;
    OLED_Clear();
    return s_ready;
}

void OLED_WriteLine(uint8_t page, const char *text)
{
    uint8_t pixels[OLED_LINE_BYTES];
    char padded[OLED_COLUMNS + 1U];
    char key[24];
    uint8_t i;
    if (!s_ready || page > 7U || text == 0) return;
    memset(pixels, 0, sizeof(pixels));
    for (i = 0U; i < OLED_COLUMNS; i++)
    {
        char c = text[i];
        if (c == '\0') { while (i < OLED_COLUMNS) padded[i++] = ' '; break; }
        padded[i] = c;
    }
    if (i >= OLED_COLUMNS) { /* all columns copied */ }
    padded[OLED_COLUMNS] = '\0';
    pixels[0] = 0U;
    for (i = 0U; i < OLED_COLUMNS; i++) draw_glyph(pixels, (uint8_t)(1U + (i * 6U)), padded[i]);
    key[0] = 'F';
    memcpy(&key[1], padded, OLED_COLUMNS);
    key[1U + OLED_COLUMNS] = '\0';
    (void)write_page(page, pixels, key);
}


void OLED_WriteCenteredLine(uint8_t page, const char *text)
{
    uint8_t pixels[OLED_LINE_BYTES];
    char padded[OLED_COLUMNS + 1U];
    char key[24];
    uint8_t length = 0U;
    uint8_t start;
    uint8_t i;
    if (!s_ready || page > 7U || text == 0) return;
    while (text[length] != '\0' && length < OLED_COLUMNS) length++;
    memset(padded, ' ', OLED_COLUMNS);
    padded[OLED_COLUMNS] = '\0';
    start = (uint8_t)((OLED_COLUMNS - length) / 2U);
    memcpy(&padded[start], text, length);
    memset(pixels, 0, sizeof(pixels));
    for (i = 0U; i < OLED_COLUMNS; i++) draw_glyph(pixels, (uint8_t)(1U + (i * 6U)), padded[i]);
    if (page == 0U)
        for (i = 0U; i < OLED_LINE_BYTES; i++) pixels[i] |= 0x80U; /* thin line under title */
    key[0] = 'C';
    memcpy(&key[1], padded, OLED_COLUMNS);
    key[1U + OLED_COLUMNS] = '\0';
    (void)write_page(page, pixels, key);
}

void OLED_WriteSplitLine(uint8_t page, const char *left, const char *right)
{
    uint8_t pixels[OLED_LINE_BYTES];
    char lbuf[OLED_LEFT_CHARS + 1U];
    char rbuf[OLED_RIGHT_CHARS + 1U];
    char key[24];
    uint8_t i;
    if (!s_ready || page > 7U || left == 0 || right == 0) return;
    memset(pixels, 0, sizeof(pixels));
    for (i = 0U; i < OLED_LEFT_CHARS; i++)
    {
        char c = left[i];
        if (c == '\0') { while (i < OLED_LEFT_CHARS) lbuf[i++] = ' '; break; }
        lbuf[i] = c;
    }
    lbuf[OLED_LEFT_CHARS] = '\0';
    for (i = 0U; i < OLED_RIGHT_CHARS; i++)
    {
        char c = right[i];
        if (c == '\0') { while (i < OLED_RIGHT_CHARS) rbuf[i++] = ' '; break; }
        rbuf[i] = c;
    }
    rbuf[OLED_RIGHT_CHARS] = '\0';
    for (i = 0U; i < OLED_LEFT_CHARS; i++) draw_glyph(pixels, (uint8_t)(1U + i * 6U), lbuf[i]);
    pixels[OLED_SPLIT_X] = 0xFFU; /* divider between the UI and the fault list */
    for (i = 0U; i < OLED_RIGHT_CHARS; i++) draw_glyph(pixels, (uint8_t)(OLED_RIGHT_X + i * 6U), rbuf[i]);
    if (page == 0U)
        for (i = 0U; i < OLED_LINE_BYTES; i++) pixels[i] |= 0x80U; /* title underline */
    key[0] = 'S';
    memcpy(&key[1], lbuf, OLED_LEFT_CHARS);
    key[1U + OLED_LEFT_CHARS] = '|';
    memcpy(&key[2U + OLED_LEFT_CHARS], rbuf, OLED_RIGHT_CHARS);
    key[2U + OLED_LEFT_CHARS + OLED_RIGHT_CHARS] = '\0';
    (void)write_page(page, pixels, key);
}

void OLED_Clear(void)
{
    uint8_t page;
    for (page = 0U; page < 8U; page++) OLED_WriteLine(page, "");
}
