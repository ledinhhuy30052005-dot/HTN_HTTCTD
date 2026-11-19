#include "taskN1.h"
#include "app_state.h"
#include "sensors.h"
#include "oled.h"
#include "ds3231.h"
#include "i2c_bus.h"
#include "app_types.h"
#include "app_config.h"
#include "app_state.h"
#include "board.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "config_store.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define MENU_ITEM_COUNT 13U
static uint8_t s_menuIndex = 0U;
static uint8_t s_editing = 0U;
static uint8_t s_editingRtc = 0U;
static AppConfig_t s_editConfig;
static DS3231_Time_t s_editRtc;

static void format_time(uint32_t minuteValue, char *out, uint32_t len)
{
    uint32_t h = minuteValue / 60UL;
    uint32_t m = minuteValue % 60UL;
    (void)snprintf(out, len, "%02lu:%02lu", (unsigned long)h, (unsigned long)m);
}

static void edit_adjust(int8_t direction)
{
    if (s_editingRtc)
    {
        if (s_menuIndex == 8U)
        {
            if (direction > 0) s_editRtc.hour = (uint8_t)((s_editRtc.hour + 1U) % 24U);
            else s_editRtc.hour = (s_editRtc.hour == 0U) ? 23U : (uint8_t)(s_editRtc.hour - 1U);
        }
        else if (s_menuIndex == 9U)
        {
            if (direction > 0) s_editRtc.minute = (uint8_t)((s_editRtc.minute + 1U) % 60U);
            else s_editRtc.minute = (s_editRtc.minute == 0U) ? 59U : (uint8_t)(s_editRtc.minute - 1U);
            s_editRtc.second = 0U;
        }
        return;
    }
    switch (s_menuIndex)
    {
        case 1U:
            if (direction > 0 && s_editConfig.soilOnPercent < 70U) s_editConfig.soilOnPercent++;
            if (direction < 0 && s_editConfig.soilOnPercent > 5U) s_editConfig.soilOnPercent--;
            if (s_editConfig.soilOnPercent >= s_editConfig.soilOffPercent)
                s_editConfig.soilOnPercent = s_editConfig.soilOffPercent - 1U;
            break;
        case 2U:
            if (direction > 0 && s_editConfig.soilOffPercent < 95U) s_editConfig.soilOffPercent++;
            if (direction < 0 && s_editConfig.soilOffPercent > 10U) s_editConfig.soilOffPercent--;
            if (s_editConfig.soilOffPercent <= s_editConfig.soilOnPercent)
                s_editConfig.soilOffPercent = s_editConfig.soilOnPercent + 1U;
            break;
        case 3U:
            if (direction > 0 && s_editConfig.fanOnTempC < 50U) s_editConfig.fanOnTempC++;
            if (direction < 0 && s_editConfig.fanOnTempC > 15U) s_editConfig.fanOnTempC--;
            break;
        case 4U:
            if (direction > 0 && s_editConfig.waterBlockTempC < 60U) s_editConfig.waterBlockTempC++;
            if (direction < 0 && s_editConfig.waterBlockTempC > 20U) s_editConfig.waterBlockTempC--;
            break;
        case 5U:
            if (direction > 0 && s_editConfig.lightOnLux < 2000U) s_editConfig.lightOnLux += 10U;
            if (direction < 0 && s_editConfig.lightOnLux >= 10U) s_editConfig.lightOnLux -= 10U;
            break;
        case 6U:
            if (direction > 0) s_editConfig.schedule1Minute = (s_editConfig.schedule1Minute + 1U) % 1440U;
            else s_editConfig.schedule1Minute = (s_editConfig.schedule1Minute == 0U) ? 1439U : s_editConfig.schedule1Minute - 1U;
            break;
        case 7U:
            if (direction > 0) s_editConfig.schedule2Minute = (s_editConfig.schedule2Minute + 1U) % 1440U;
            else s_editConfig.schedule2Minute = (s_editConfig.schedule2Minute == 0U) ? 1439U : s_editConfig.schedule2Minute - 1U;
            break;
        default: break;
    }
}

/* OLED 128x64 layout:
 * - no sensor faults: use all 21 characters, close to the requested mock-up;
 * - with sensor faults: reserve a fixed 5-character column on the right.
 * Sidebar text never overwrites/truncates the live data area. */
static void oled_status_line(uint8_t page, const char *left, const char *right,
                             const SensorData_t *sensor)
{
    if ((sensor != 0) && (sensor->faultFlags != 0U))
        OLED_WriteSplitLine(page, left, (right != 0) ? right : "");
    else
        OLED_WriteLine(page, left);
}

static void oled_status_header(uint8_t page, const char *title, const SensorData_t *sensor)
{
    if ((sensor != 0) && (sensor->faultFlags != 0U))
        OLED_WriteSplitLine(page, title, "ERR");
    else
        OLED_WriteCenteredLine(page, title);
}

/* Return the Nth fault tag in a stable order, for the dedicated right sidebar. */
static const char *fault_tag_at(uint8_t flags, uint8_t index)
{
    static const uint8_t masks[5] = {
        SENSOR_FAULT_ADC, SENSOR_FAULT_SOIL, SENSOR_FAULT_DHT11,
        SENSOR_FAULT_BH1750, SENSOR_FAULT_RTC
    };
    static const char *const tags[5] = { "ADC!", "SOIL!", "DHT11", "BH175", "RTC!" };
    uint8_t i;
    uint8_t found = 0U;
    for (i = 0U; i < 5U; i++)
    {
        if (flags & masks[i])
        {
            if (found == index) return tags[i];
            found++;
        }
    }
    return "";
}

static void make_menu_entry(uint8_t index, const AppConfig_t *cfg,
                            const SensorData_t *sensor, char *out, uint32_t outLen)
{
    char timeText[8];
    switch (index)
    {
        case 0U: (void)snprintf(out, outLen, "Mode:%s", g_manualMode ? "MANUAL" : "AUTO"); break;
        case 1U: (void)snprintf(out, outLen, "Soil ON:%lu%%", (unsigned long)cfg->soilOnPercent); break;
        case 2U: (void)snprintf(out, outLen, "Soil OFF:%lu%%", (unsigned long)cfg->soilOffPercent); break;
        case 3U: (void)snprintf(out, outLen, "Fan ON:%luC", (unsigned long)cfg->fanOnTempC); break;
        case 4U: (void)snprintf(out, outLen, "Hot blk:%luC", (unsigned long)cfg->waterBlockTempC); break;
        case 5U: (void)snprintf(out, outLen, "Light:%lulx", (unsigned long)cfg->lightOnLux); break;
        case 6U:
            format_time(cfg->schedule1Minute, timeText, sizeof(timeText));
            (void)snprintf(out, outLen, "Sched1:%s", timeText); break;
        case 7U:
            format_time(cfg->schedule2Minute, timeText, sizeof(timeText));
            (void)snprintf(out, outLen, "Sched2:%s", timeText); break;
        case 8U: (void)snprintf(out, outLen, "RTC Hour:%02u", (unsigned)(sensor->rtcValid ? sensor->hour : 0U)); break;
        case 9U: (void)snprintf(out, outLen, "RTC Min:%02u", (unsigned)(sensor->rtcValid ? sensor->minute : 0U)); break;
        case 10U: (void)snprintf(out, outLen, "Man Fan:%s", g_manualFanRequest ? "ON" : "OFF"); break;
        case 11U: (void)snprintf(out, outLen, "Man Lamp:%s", g_manualLampRequest ? "ON" : "OFF"); break;
        default: (void)snprintf(out, outLen, "Man Pump:%s", g_manualPumpRequest ? "ON" : "OFF"); break;
    }
}

static void show_menu_screen(uint8_t selected, const AppConfig_t *cfg, const SensorData_t *sensor)
{
    uint8_t start = (selected > 4U) ? (uint8_t)(selected - 4U) : 0U;
    uint8_t row;
    char entry[32];
    char line[32];
    oled_status_header(0U, "MAIN MENU", sensor);
    for (row = 0U; row < 5U; row++)
    {
        uint8_t index = (uint8_t)(start + row);
        if (index < MENU_ITEM_COUNT)
        {
            make_menu_entry(index, cfg, sensor, entry, sizeof(entry));
            (void)snprintf(line, sizeof(line), "%c%s", (index == selected) ? '>' : ' ', entry);
            oled_status_line((uint8_t)(row + 1U), line,
                             fault_tag_at(sensor->faultFlags, row), sensor);
        }
        else oled_status_line((uint8_t)(row + 1U), "", "", sensor);
    }
    oled_status_line(6U, "UP/DN: MOVE", fault_tag_at(sensor->faultFlags, 5U), sensor);
    OLED_WriteLine(7U, "OK:SELECT BACK:EXIT");
}

static void show_edit_screen(uint8_t index, const AppConfig_t *cfg,
                             const SensorData_t *sensor, uint8_t editingRtc,
                             const DS3231_Time_t *editRtc)
{
    char value[32];
    if (editingRtc && index == 8U)
        (void)snprintf(value, sizeof(value), "RTC HOUR:%02u", (unsigned)editRtc->hour);
    else if (editingRtc && index == 9U)
        (void)snprintf(value, sizeof(value), "RTC MIN:%02u", (unsigned)editRtc->minute);
    else make_menu_entry(index, cfg, sensor, value, sizeof(value));

    oled_status_header(0U, "EDIT SETTINGS", sensor);
    oled_status_line(1U, value, fault_tag_at(sensor->faultFlags, 0U), sensor);
    oled_status_line(2U, "UP/DN: CHANGE", fault_tag_at(sensor->faultFlags, 1U), sensor);
    oled_status_line(3U, "OK: SAVE/APPLY", fault_tag_at(sensor->faultFlags, 2U), sensor);
    oled_status_line(4U, "BACK: CANCEL", fault_tag_at(sensor->faultFlags, 3U), sensor);
    if (g_configSaveState == 1U) oled_status_line(5U, "FLASH: SAVING", fault_tag_at(sensor->faultFlags, 4U), sensor);
    else if (g_configSaveState == 2U) oled_status_line(5U, "FLASH: SAVED", fault_tag_at(sensor->faultFlags, 4U), sensor);
    else if (g_configSaveState == 3U) oled_status_line(5U, "FLASH: ERROR", fault_tag_at(sensor->faultFlags, 4U), sensor);
    else oled_status_line(5U, "VALUE NOT SAVED", fault_tag_at(sensor->faultFlags, 4U), sensor);
    oled_status_line(6U, g_manualMode ? "MODE: MANUAL" : "MODE: AUTO", "", sensor);
    OLED_WriteLine(7U, "OK:SAVE  BACK:CANCEL");
}

static void show_home(const SensorData_t *s, uint8_t page)
{
    AppConfig_t cfg;
    char line[32];
    AppConfig_GetActive(&cfg);

    if (page == 0U)
    {
        /* Normal screen uses the full width. If there are faults, reserve the
         * rightmost 5 characters and place fault tags there without overlap. */
        if ((s != 0) && (s->faultFlags != 0U))
        {
            oled_status_header(0U, "GREENHOUSE", s);
            if (s->temperatureValid)
                (void)snprintf(line, sizeof(line), "Soil:%u%%T:%dC", (unsigned)s->soilPercent, (int)s->temperatureC);
            else
                (void)snprintf(line, sizeof(line), "Soil:%u%%T:--", (unsigned)s->soilPercent);
            oled_status_line(1U, line, fault_tag_at(s->faultFlags, 0U), s);
            if (s->dhtValid) (void)snprintf(line, sizeof(line), "Air:%u%%", (unsigned)s->humidityPercent);
            else (void)snprintf(line, sizeof(line), "Air:--%%");
            oled_status_line(2U, line, fault_tag_at(s->faultFlags, 1U), s);
            if (s->lightValid) (void)snprintf(line, sizeof(line), "Light:%ulx", (unsigned)s->lightLux);
            else (void)snprintf(line, sizeof(line), "Light:--lx");
            oled_status_line(3U, line, fault_tag_at(s->faultFlags, 2U), s);
            if (s->waterOK) (void)snprintf(line, sizeof(line), "Tank:OK W:%u", (unsigned)s->waterRaw);
            else (void)snprintf(line, sizeof(line), "Tank:LOW W:%u", (unsigned)s->waterRaw);
            oled_status_line(4U, line, fault_tag_at(s->faultFlags, 3U), s);
            if (g_pumpOn) (void)snprintf(line, sizeof(line), "Pump:ON");
            else if (!s->waterOK) (void)snprintf(line, sizeof(line), "Pump:OFF TANK!");
            else if (g_pumpStatus == PUMP_ST_HOT_DRY) (void)snprintf(line, sizeof(line), "Pump:OFF HOT!");
            else (void)snprintf(line, sizeof(line), "Pump:OFF");
            oled_status_line(5U, line, fault_tag_at(s->faultFlags, 4U), s);
            (void)snprintf(line, sizeof(line), "Fan:%s L:%s", g_fanOn ? "ON" : "OFF", g_lampOn ? "ON" : "OFF");
            oled_status_line(6U, line, "", s);
            OLED_WriteLine(7U, g_manualMode ? "MANUAL OK:MENU UP/DN" : "AUTO OK:MENU UP/DN");
        }
        else
        {
            OLED_WriteCenteredLine(0U, "GREENHOUSE SYSTEM");
            if (s->temperatureValid)
                (void)snprintf(line, sizeof(line), "Soil:%u%% Temp:%dC", (unsigned)s->soilPercent, (int)s->temperatureC);
            else
                (void)snprintf(line, sizeof(line), "Soil:%u%% Temp:--", (unsigned)s->soilPercent);
            OLED_WriteLine(1U, line);
            if (s->dhtValid) (void)snprintf(line, sizeof(line), "Air:%u%% Lux:%ulx", (unsigned)s->humidityPercent, (unsigned)s->lightLux);
            else (void)snprintf(line, sizeof(line), "Air:--%% Lux:%ulx", (unsigned)s->lightLux);
            OLED_WriteLine(2U, line);
            if (s->waterOK) (void)snprintf(line, sizeof(line), "Tank:OK W:%u", (unsigned)s->waterRaw);
            else (void)snprintf(line, sizeof(line), "Tank:LOW W:%u", (unsigned)s->waterRaw);
            OLED_WriteLine(3U, line);
            (void)snprintf(line, sizeof(line), "Pump:%s Fan:%s", g_pumpOn ? "ON" : "OFF", g_fanOn ? "ON" : "OFF");
            OLED_WriteLine(4U, line);
            (void)snprintf(line, sizeof(line), "Lamp:%s Mode:%s", g_lampOn ? "ON" : "OFF", g_manualMode ? "MANUAL" : "AUTO");
            OLED_WriteLine(5U, line);
            OLED_WriteLine(6U, "UP/DOWN: PAGE");
            OLED_WriteLine(7U, "OK: MENU BACK:HOME");
        }
    }
    else
    {
        oled_status_header(0U, "SETTINGS", s);
        (void)snprintf(line, sizeof(line), "Soil:%lu/%lu%%", (unsigned long)cfg.soilOnPercent, (unsigned long)cfg.soilOffPercent);
        oled_status_line(1U, line, fault_tag_at(s->faultFlags, 0U), s);
        (void)snprintf(line, sizeof(line), "Fan ON:%luC", (unsigned long)cfg.fanOnTempC);
        oled_status_line(2U, line, fault_tag_at(s->faultFlags, 1U), s);
        (void)snprintf(line, sizeof(line), "Hot block:%luC", (unsigned long)cfg.waterBlockTempC);
        oled_status_line(3U, line, fault_tag_at(s->faultFlags, 2U), s);
        (void)snprintf(line, sizeof(line), "Light:%lulx", (unsigned long)cfg.lightOnLux);
        oled_status_line(4U, line, fault_tag_at(s->faultFlags, 3U), s);
        (void)snprintf(line, sizeof(line), "Sched1:%02lu:%02lu", (unsigned long)(cfg.schedule1Minute / 60U), (unsigned long)(cfg.schedule1Minute % 60U));
        oled_status_line(5U, line, fault_tag_at(s->faultFlags, 4U), s);
        (void)snprintf(line, sizeof(line), "Sched2:%02lu:%02lu", (unsigned long)(cfg.schedule2Minute / 60U), (unsigned long)(cfg.schedule2Minute % 60U));
        oled_status_line(6U, line, "", s);
        OLED_WriteLine(7U, "UP/DN:PAGE OK:MENU");
    }
}

static void TaskN1_I2CSensorTask(void *argument)
{
    SensorData_t update;
    TickType_t lastWake = xTaskGetTickCount();
    TickType_t lastLight = lastWake;
    TickType_t lastRtc = lastWake;
    (void)argument;
    (void)Sensors_InitLight();
    for (;;)
    {
        TickType_t now = xTaskGetTickCount();
        if ((now - lastLight) >= pdMS_TO_TICKS(BH1750_PERIOD_MS))
        {
            AppSensor_GetSnapshot(&update);
            Sensors_ReadLight(&update);
            AppSensor_Update(&update, SENSOR_UPDATE_LIGHT);
            lastLight = now;
        }
        if ((now - lastRtc) >= pdMS_TO_TICKS(RTC_READ_PERIOD_MS))
        {
            AppSensor_GetSnapshot(&update);
            Sensors_ReadRTC(&update);
            AppSensor_Update(&update, SENSOR_UPDATE_RTC);
            lastRtc = now;
        }
        AppHeartbeat_Increment(APP_HB_I2C_SENSOR);
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(25U));
    }
}

static void UITask(void *argument)
{
    ButtonEvent_t event;
    SensorData_t sensor;
    AppConfig_t active;
    uint8_t inMenu = 0U;
    uint8_t homePage = 0U;
    TickType_t lastWake;
    TickType_t lastOledTry;
    (void)argument;
    memset(&sensor, 0, sizeof(sensor));
    vTaskDelay(pdMS_TO_TICKS(200U));   /* cho OLED on dinh nguon truoc khi gui lenh */
    (void)OLED_Init();
    lastWake = xTaskGetTickCount();
    lastOledTry = lastWake;
    for (;;)
    {
        AppHeartbeat_Increment(APP_HB_UI);
        if (!OLED_IsReady() && ((xTaskGetTickCount() - lastOledTry) >= pdMS_TO_TICKS(1000U)))
        {
            (void)OLED_Init();          /* chua co OLED / mat ket noi: thu lai moi giay */
            lastOledTry = xTaskGetTickCount();
        }
        AppSensor_GetSnapshot(&sensor);
        if (xQueueReceive(g_buttonQueue, &event, 0U) == pdTRUE)
        {
            if (!inMenu)
            {
                if (event == BUTTON_OK) { inMenu = 1U; s_menuIndex = 0U; s_editing = 0U; AppConfig_GetActive(&s_editConfig); }
                else if (event == BUTTON_UP || event == BUTTON_DOWN) homePage ^= 1U;
                else if (event == BUTTON_BACK) homePage = 0U;
            }
            else if (s_editing)
            {
                if (event == BUTTON_UP) edit_adjust(1);
                else if (event == BUTTON_DOWN) edit_adjust(-1);
                else if (event == BUTTON_BACK)
                {
                    s_editing = 0U;
                    s_editingRtc = 0U;
                    AppConfig_GetActive(&s_editConfig);
                }
                else if (event == BUTTON_OK)
                {
                    if (s_editingRtc)
                    {
                        if (DS3231_SetTime(&s_editRtc)) g_configSaveState = 2U;
                        else g_configSaveState = 3U;
                        s_editingRtc = 0U;
                    }
                    else if (AppConfig_Validate(&s_editConfig))
                    {
                        AppConfig_SetActive(&s_editConfig);
                        (void)xQueueOverwrite(g_configSaveQueue, &s_editConfig);
                    }
                    s_editing = 0U;
                }
            }
            else
            {
                if (event == BUTTON_UP) s_menuIndex = (uint8_t)((s_menuIndex + MENU_ITEM_COUNT - 1U) % MENU_ITEM_COUNT);
                else if (event == BUTTON_DOWN) s_menuIndex = (uint8_t)((s_menuIndex + 1U) % MENU_ITEM_COUNT);
                else if (event == BUTTON_BACK) inMenu = 0U;
                else if (event == BUTTON_OK)
                {
                    if (s_menuIndex == 0U) g_manualMode = (uint8_t)!g_manualMode;
                    else if (s_menuIndex >= 1U && s_menuIndex <= 7U)
                    {
                        AppConfig_GetActive(&s_editConfig);
                        s_editingRtc = 0U;
                        s_editing = 1U;
                    }
                    else if ((s_menuIndex == 8U || s_menuIndex == 9U) && sensor.rtcValid)
                    {
                        s_editRtc.second = 0U;
                        s_editRtc.minute = sensor.minute;
                        s_editRtc.hour = sensor.hour;
                        s_editRtc.day = sensor.day;
                        s_editRtc.month = sensor.month;
                        s_editRtc.year = sensor.year;
                        s_editingRtc = 1U;
                        s_editing = 1U;
                    }
                    else if (s_menuIndex == 10U && g_manualMode) g_manualFanRequest = (uint8_t)!g_manualFanRequest;
                    else if (s_menuIndex == 11U && g_manualMode) g_manualLampRequest = (uint8_t)!g_manualLampRequest;
                    else if (s_menuIndex == 12U && g_manualMode) g_manualPumpRequest = (uint8_t)!g_manualPumpRequest;
                }
            }
        }
        if (inMenu)
        {
            AppConfig_GetActive(&active);
            if (s_editing) show_edit_screen(s_menuIndex, s_editingRtc ? &active : &s_editConfig, &sensor, s_editingRtc, &s_editRtc);
            else show_menu_screen(s_menuIndex, &active, &sensor);
            if (g_manualMode == 0U) { g_manualPumpRequest = 0U; g_manualFanRequest = 0U; g_manualLampRequest = 0U; }
        }
        else
        {
            show_home(&sensor, homePage);
        }
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(DISPLAY_REFRESH_MS));
    }
}
BaseType_t TaskN1_Create(void)
{
    if (xTaskCreate(TaskN1_I2CSensorTask, "I2CSensors", 320U, NULL, 2U, NULL) != pdPASS) return pdFAIL;
    if (xTaskCreate(UITask, "OLED_Menu", 640U, NULL, 1U, NULL) != pdPASS) return pdFAIL;
    return pdPASS;
}
