#include "taskN3.h"
#include "button.h"
#include "relay.h"
#include "buzzer.h"
#include "sensors.h"
#include "config_store.h"
#include "app_state.h"
#include "app_config.h"
#include "app_types.h"
#include "i2c_bus.h"
#include "ds3231.h"
#include "taskN1.h"
#include "taskN2.h"
#include "board.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <string.h>
#include <stdint.h>

static void pump_set(uint8_t on)
{
    Relay_SetPump(on);
    g_pumpOn = on;
}

static void ControlTask(void *argument)
{
    SensorData_t sensor;
    AppConfig_t cfg;
    uint8_t haveSensor = 0U;
    uint8_t pumpMode = 0U; /* 0 off, 1 soil-auto, 2 scheduled, 3 manual */
    TickType_t pumpStarted = 0U;
    TickType_t lastAutoEnd = 0U;
    uint32_t lastScheduleKey = 0xFFFFFFFFUL;
    uint8_t fanState = 0U;
    uint8_t lampState = 0U;
    uint8_t previousTankLow = 0U;
    uint8_t buzzerPulseActive = 0U;
    TickType_t buzzerPulseStart = 0U;
    TickType_t lastWake = xTaskGetTickCount();
    (void)argument;
    memset(&sensor, 0, sizeof(sensor));
    lastAutoEnd = xTaskGetTickCount() - pdMS_TO_TICKS(SOIL_WATER_COOLDOWN_SEC * 1000U);
    pump_set(0U);
    Relay_SetFan(0U);
    Relay_SetLamp(0U);
    for (;;)
    {
        TickType_t now = xTaskGetTickCount();
        uint8_t thermalDryBlock;
        uint8_t tankOK;
        uint8_t scheduleDue = 0U;
        uint8_t soilDry;
        uint8_t cooldownDone;
        uint32_t minuteOfDay = 0U;
        uint32_t scheduleKey = 0U;
        AppHeartbeat_Increment(APP_HB_CONTROL);
        if (xQueuePeek(g_sensorQueue, &sensor, 0U) == pdTRUE) haveSensor = 1U;
        AppConfig_GetActive(&cfg);

        if (haveSensor && sensor.rtcValid)
        {
            minuteOfDay = ((uint32_t)sensor.hour * 60UL) + sensor.minute;
            scheduleKey = (sensor.dayKey * 1440UL) + minuteOfDay;
            if (!g_manualMode && (sensor.second <= 5U) && (scheduleKey != lastScheduleKey) &&
                ((minuteOfDay == cfg.schedule1Minute) || (minuteOfDay == cfg.schedule2Minute)))
            {
                scheduleDue = 1U;
                lastScheduleKey = scheduleKey; /* one attempt per scheduled minute */
            }
        }

        /* Interlocks: chi con 2 dieu kien khoa bom
         *  - het nuoc (cam bien muc nuoc PA2 duoi nguong): chong chay kho
         *  - nong va dat kho (chi xet khi DHT11 doc duoc)
         * DHT11 loi KHONG con khoa bom nua; khi cam bien loi chi khong ap dung khoa nhiet. */
        tankOK = (haveSensor && sensor.waterOK) ? 1U : 0U;
        thermalDryBlock = (haveSensor && sensor.temperatureValid &&
                           (sensor.temperatureC >= (int16_t)cfg.waterBlockTempC) &&
                           (sensor.soilPercent <= (uint8_t)cfg.soilOnPercent)) ? 1U : 0U;
        soilDry = (haveSensor && (sensor.soilPercent <= (uint8_t)cfg.soilOnPercent)) ? 1U : 0U;
        cooldownDone = ((now - lastAutoEnd) >= pdMS_TO_TICKS(SOIL_WATER_COOLDOWN_SEC * 1000U)) ? 1U : 0U;
        /* Do not announce tank-low until at least one sensor snapshot exists. */
        g_tankLowAlarm = (haveSensor && !tankOK) ? 1U : 0U;

        /* Schedule has priority over the ordinary soil-based cycle. It ignores soil
         * moisture, but tank-low and hot+dry interlocks still stop the pump for safety. */
        if (scheduleDue)
        {
            if (tankOK && !thermalDryBlock)
            {
                pumpMode = 2U;
                pumpStarted = now;
                pump_set(1U);
            }
            else
            {
                /* Marked as attempted, so it will not repeatedly start in this minute. */
                pumpMode = 0U;
                pump_set(0U);
            }
        }

        if (pumpMode != 0U)
        {
            uint8_t mustStop = 0U;
            if (!tankOK || thermalDryBlock) mustStop = 1U;
            if ((pumpMode == 1U) && (sensor.soilPercent >= (uint8_t)cfg.soilOffPercent)) mustStop = 1U;
            if ((pumpMode == 3U) && !g_manualPumpRequest) mustStop = 1U;
            if ((now - pumpStarted) >= pdMS_TO_TICKS(WATERING_PULSE_SECONDS * 1000U)) mustStop = 1U;
            if (mustStop)
            {
                pump_set(0U);
                /* All automatic watering pulses observe the short soak interval, so a
                 * scheduled pulse cannot immediately chain into another soil pulse. */
                if (pumpMode == 1U || pumpMode == 2U) lastAutoEnd = now;
                if (pumpMode == 3U) g_manualPumpRequest = 0U; /* manual pump is one 10 s pulse */
                pumpMode = 0U;
            }
        }

        if (pumpMode == 0U)
        {
            if (!tankOK || thermalDryBlock)
            {
                pump_set(0U);
                if (g_manualMode) g_manualPumpRequest = 0U;
            }
            else if (g_manualMode && g_manualPumpRequest)
            {
                pumpMode = 3U;
                pumpStarted = now;
                pump_set(1U);
            }
            else if (!g_manualMode && !scheduleDue && soilDry && cooldownDone)
            {
                pumpMode = 1U;
                pumpStarted = now;
                pump_set(1U);
            }
        }

        /* Trang thai bom de hien thi tren OLED. */
        if (g_pumpOn)               g_pumpStatus = (pumpMode == 3U) ? PUMP_ST_MANUAL : PUMP_ST_WATERING;
        else if (!tankOK)           g_pumpStatus = PUMP_ST_TANK_LOW;
        else if (thermalDryBlock)   g_pumpStatus = PUMP_ST_HOT_DRY;
        else if (!g_manualMode && soilDry && !cooldownDone) g_pumpStatus = PUMP_ST_COOLDOWN;
        else                        g_pumpStatus = PUMP_ST_IDLE;

        /* Fan automatic control uses a 2 C hysteresis to avoid relay chatter. */
        if (g_manualMode)
        {
            fanState = g_manualFanRequest;
            lampState = g_manualLampRequest;
        }
        else
        {
            if (haveSensor && sensor.temperatureValid)
            {
                if (sensor.temperatureC >= (int16_t)cfg.fanOnTempC) fanState = 1U;
                else if (sensor.temperatureC <= ((int16_t)cfg.fanOnTempC - 2)) fanState = 0U;
            }
            if (haveSensor && sensor.lightValid)
            {
                if (sensor.lightLux < cfg.lightOnLux) lampState = 1U;
                else if (sensor.lightLux > (cfg.lightOnLux + 20U)) lampState = 0U;
            }
        }
        Relay_SetFan(fanState);
        Relay_SetLamp(lampState);
        g_fanOn = fanState;
        g_lampOn = lampState;
        /* Buzzer: a single 5-second alarm on the transition to tank-low.
         * PC13 is deliberately not used by the application; bootloader owns it for OTA success. */
        if (g_tankLowAlarm && !previousTankLow)
        {
            buzzerPulseActive = 1U;
            buzzerPulseStart = now;
        }
        if (buzzerPulseActive &&
            ((now - buzzerPulseStart) >= pdMS_TO_TICKS(TANK_LOW_BEEP_MS)))
        {
            buzzerPulseActive = 0U;
        }
        Buzzer_Set(buzzerPulseActive);
        previousTankLow = g_tankLowAlarm;
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(CONTROL_TASK_PERIOD_MS));
    }
}

static void ButtonTask(void *argument)
{
    uint8_t previousRaw[4] = {0U,0U,0U,0U};
    uint8_t stableState[4] = {0U,0U,0U,0U};
    uint8_t count[4] = {0U,0U,0U,0U};
    uint8_t i;
    (void)argument;
    for (;;)
    {
        AppHeartbeat_Increment(APP_HB_BUTTON);
        for (i = 0U; i < 4U; i++)
        {
            uint8_t raw = Button_IsPressed(i);
            if (raw == previousRaw[i])
            {
                if (count[i] < 3U) count[i]++;
                if ((count[i] >= 3U) && (stableState[i] != raw))
                {
                    stableState[i] = raw;
                    if (raw)
                    {
                        ButtonEvent_t ev = (ButtonEvent_t)i;
                        (void)xQueueSend(g_buttonQueue, &ev, 0U);
                    }
                }
            }
            else
            {
                previousRaw[i] = raw;
                count[i] = 0U;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(BUTTON_SCAN_PERIOD_MS));
    }
}

static void ConfigTask(void *argument)
{
    AppConfig_t cfg;
    (void)argument;
    for (;;)
    {
        AppHeartbeat_Increment(APP_HB_CONFIG);
        if (xQueueReceive(g_configSaveQueue, &cfg, pdMS_TO_TICKS(500U)) == pdTRUE)
        {
            g_configSaveState = 1U;
            if (ConfigStore_Save(&cfg)) g_configSaveState = 2U;
            else g_configSaveState = 3U;
            vTaskDelay(pdMS_TO_TICKS(1500U));
            g_configSaveState = 0U;
        }
    }
}

static void HealthTask(void *argument)
{
    uint32_t previous[APP_HB_COUNT];
    uint8_t stale[APP_HB_COUNT] = {0U};
    uint8_t i;
    (void)argument;
    for (i = 0U; i < APP_HB_COUNT; i++) previous[i] = AppHeartbeat_Get((AppHeartbeatId_t)i);
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(1000U));
        for (i = 0U; i < APP_HB_COUNT; i++)
        {
            uint32_t current = AppHeartbeat_Get((AppHeartbeatId_t)i);
            if (current == previous[i])
            {
                if (stale[i] < 3U) stale[i]++;
            }
            else stale[i] = 0U;
            previous[i] = current;
            if (stale[i] >= 3U)
            {
                Relay_AllOff();
                Buzzer_Set(1U);
                Board_DelayMs(100U);
                Buzzer_Set(0U);
                NVIC_SystemReset();
                for (;;) { Relay_AllOff(); }
            }
        }
    }
}
uint8_t TaskN3_InitShared(void)
{
    if (!AppState_Init())
    {
        Relay_AllOff();
        Buzzer_Set(1U);
        Board_DelayMs(100U);
        Buzzer_Set(0U);
        return 0U;
    }
    return 1U;
}

BaseType_t TaskN3_Create(void)
{
    if (xTaskCreate(ControlTask, "Control", 448U, NULL, 4U, NULL) != pdPASS) return pdFAIL;
    if (xTaskCreate(ButtonTask, "Buttons", 192U, NULL, 2U, NULL) != pdPASS) return pdFAIL;
    if (xTaskCreate(ConfigTask, "Config", 320U, NULL, 2U, NULL) != pdPASS) return pdFAIL;
    if (xTaskCreate(HealthTask, "Health", 192U, NULL, 4U, NULL) != pdPASS) return pdFAIL;
    return pdPASS;
}
