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

// Ham dieu khien bom 
static void pump_set(uint8_t on)
{
    Relay_SetPump(on);
	  g_pumpOn = on;// cap nhat co Bom on hien thi len Oled :Pump on (app_state.c)
}

// nhan du lieu cam bien de dieu khien thiet bi 
static void ControlTask(void *argument)
{
    SensorData_t sensor; //Ban sao du lieu cam bien moi nhat
    AppConfig_t cfg;     // Ban sao cau hinh hoat dong hien tai
    uint8_t haveSensor = 0U;
	  uint8_t pumpMode = 0U; /* 0 off, 1 soil-auto, 2 scheduled, 3 manual:Phân biet bom dang chay theo dat, lich, bang tay hay dang OFF*/
    TickType_t pumpStarted = 0U; // Thoi diem bat dau chu ky bom 
    TickType_t lastAutoEnd = 0U; // thoi diem ket thuc lan tuoi tu dong 
    uint32_t lastScheduleKey = 0xFFFFFFFFUL;
    uint8_t fanState = 0U; // trang thai quat
    uint8_t lampState = 0U; // trang thai den 
    uint8_t previousTankLow = 0U; // trang thai be 
    uint8_t buzzerPulseActive = 0U;  // trang thai coi hoat dong 
    TickType_t buzzerPulseStart = 0U; // trang thai coi bat dau hoat dong 
    TickType_t lastWake = xTaskGetTickCount();
    (void)argument;
    memset(&sensor, 0, sizeof(sensor));// xoa het nhung du lieu trong sensor 
    lastAutoEnd = xTaskGetTickCount() - pdMS_TO_TICKS(SOIL_WATER_COOLDOWN_SEC * 1000U);
    pump_set(0U);
    Relay_SetFan(0U);
    Relay_SetLamp(0U);
    for (;;)// Lap lien tuc 
    {
        TickType_t now = xTaskGetTickCount();// same bien mill o ESP32 lay thoi gian hien tai 
        uint8_t thermalDryBlock;
        uint8_t tankOK;
        uint8_t scheduleDue = 0U;
        uint8_t soilDry;
        uint8_t cooldownDone;
        uint32_t minuteOfDay = 0U;
        uint32_t scheduleKey = 0U;
        AppHeartbeat_Increment(APP_HB_CONTROL);// fution dem de task Health ktra task con dang hoat dong hay ko 
			
			  // app_state.c de hieu chi tiet ve g_sensorQueue
			   /* 1. Lay snapshot cam bien moi nhat */
        if (xQueuePeek(g_sensorQueue, &sensor, 0U) == pdTRUE) haveSensor = 1U;/*lay du lieu tu cam bien trong g_sensorQueue và luu vo sensor 
			                                                                        va khong xoa du lieu trong queue de cac task khac con dung de cap nhat  */
        
			  /* 2. Lay cau hình dang có hieu luc */
			   AppConfig_GetActive(&cfg); // Nguong dung de so sanh tu du lieu thu duoc tu cam bien de dua ra quyet dinh dieu khien 

        if (haveSensor && sensor.rtcValid)// neu co du lieu cam bien va rtc hop le 
        {
					  minuteOfDay = ((uint32_t)sensor.hour * 60UL) + sensor.minute;// doi gio phut hien tai sang phut tinh tu dau ngay vd: hien tai la 6:30 => 6*60+30 =390 <=7h*60=420 
            scheduleKey = (sensor.dayKey * 1440UL) + minuteOfDay;// ma ngay hien tai 
            if (!g_manualMode && (sensor.second <= 5U) && (scheduleKey != lastScheduleKey) &&
                ((minuteOfDay == cfg.schedule1Minute) || (minuteOfDay == cfg.schedule2Minute)))// neu dang o mode tu dong , va ma ngay hom nay khac ma ngay trc do(neu trung thi ko tuoi nx ),thoi gian trung 1 trong 2 lich tuoi 
            {
                scheduleDue = 1U;// bat trang thai bom dung lich tuoi roi 
                lastScheduleKey = scheduleKey; /* cap nhat ma ngay hien tai */
            }
        }

        /* Interlocks: chi con 2 dieu kien khoa bom
         *  - het nuoc (cam bien muc nuoc PA2 duoi nguong): chong chay kho
         *  - nong va dat kho (chi xet khi DHT11 doc duoc)
         * DHT11 loi KHONG con khoa bom nua; khi cam bien loi chi khong ap dung khoa nhiet. */
        tankOK = (haveSensor && sensor.waterOK) ? 1U : 0U;// du nuoc 
        thermalDryBlock = (haveSensor && sensor.temperatureValid &&
                           (sensor.temperatureC >= (int16_t)cfg.waterBlockTempC) &&
                           (sensor.soilPercent <= (uint8_t)cfg.soilOnPercent)) ? 1U : 0U;//gia tri nhiet do hop le ,nhiet do >> nguong nhiet do chan tuoi , do am <= nguong do am de tuoi => chan bom 
        soilDry = (haveSensor && (sensor.soilPercent <= (uint8_t)cfg.soilOnPercent)) ? 1U : 0U;// do am dat < do am nguong can tuoi 
        cooldownDone = ((now - lastAutoEnd) >= pdMS_TO_TICKS(SOIL_WATER_COOLDOWN_SEC * 1000U)) ? 1U : 0U;// 1 chu ky nghi tuoi 2s sau moi lan bom 10s hoac bom du do am 
        /*bao trang thai het nuoc bat coi */
        g_tankLowAlarm = (haveSensor && !tankOK) ? 1U : 0U;


				/*pumpMode	Ý nghia
						0	       Bom tat
						1	      Tuoi tu dong theo do am dat
						2	      Tuoi tu dong theo lich
						3	      Tuoi thu công*/
        if (scheduleDue)// dung lich tuoi 
        {
            if (tankOK && !thermalDryBlock)//du nuoc va nhiet do phu hop 
            {
                pumpMode = 2U;// mode che do tuoi theo lich 
                pumpStarted = now;// luu thoi diem bat dau chay bom 
                pump_set(1U);// bat bom 
            }
            else
            {
                /* Marked as attempted, so it will not repeatedly start in this minute. */
                pumpMode = 0U;
                pump_set(0U);
            }
        }
      // Khi bom chay chuong trinh luon kiem tra dieu kien dung 
        if (pumpMode != 0U)//khac 0 la dang tuoi theo lich/do am dat/thu cong 
        {
            uint8_t mustStop = 0U;// bien bao hieu phai dung bom ngay 
            if (!tankOK || thermalDryBlock) mustStop = 1U;//het nuoc va nhiet do nong,dat kho 
            if ((pumpMode == 1U) && (sensor.soilPercent >= (uint8_t)cfg.soilOffPercent)) mustStop = 1U;//tuoi theo do am va do am hien tai > nguong do am cho phep => dung bom 
            if ((pumpMode == 3U) && !g_manualPumpRequest) mustStop = 1U;// tuoi thu cong va ko dc yeu cau tuoi  => tat bom 
            if ((now - pumpStarted) >= pdMS_TO_TICKS(WATERING_PULSE_SECONDS * 1000U)) mustStop = 1U;//>10s thi dung bom 
            if (mustStop)
            {
                pump_set(0U);
                if (pumpMode == 1U || pumpMode == 2U) lastAutoEnd = now;
                if (pumpMode == 3U) g_manualPumpRequest = 0U; /* manual pump is one 10 s pulse */
                pumpMode = 0U;
            }
        }

				
				// Xet hoat dong cac che do 
        if (pumpMode == 0U)// khi bom dang tat thi xet dk duoi day de bat bom hay ko bat bom 
        {
					  //Không an toàn
            if (!tankOK || thermalDryBlock)// het nuoc | nhiet do nong qua 
            {
                pump_set(0U);//tat bom 
                if (g_manualMode) g_manualPumpRequest = 0U;
            }
						
						//Che do thu công: neu có yêu cau bat bom, bat dau chay che do 3.
            else if (g_manualMode && g_manualPumpRequest)//che do thu cong va dc yeu cau bat bom tu nguoi  dung 
            {
                pumpMode = 3U;//mode tuoi thu cong 
                pumpStarted = now;// thoi diem bat dau bom 
                pump_set(1U);// bat bom 
            }
						//Che do tu dong theo do am:
            else if (!g_manualMode && !scheduleDue && soilDry && cooldownDone)// dang o auto và ko p lich tuoi tu dong, do am kho,het t/g nghi 2s  
            {
                pumpMode = 1U;
                pumpStarted = now;
                pump_set(1U);
            }
        }
 
        /* Trang thai bom de hien thi tren OLED. */
        if (g_pumpOn)               g_pumpStatus = (pumpMode == 3U) ? PUMP_ST_MANUAL : PUMP_ST_WATERING;/*g_pumpOn == 1 (Bom bat) và pumpMode == 3 => dang tuoi o che do thu cong 
				                                                                                                  g_pumpOn == 1 (Bom bat) và pumpMode != 3 => dang tuoi o che do khac   */
        else if (!tankOK)           g_pumpStatus = PUMP_ST_TANK_LOW;//Bom tat TankOk=0 => Be thieu nc 
        else if (thermalDryBlock)   g_pumpStatus = PUMP_ST_HOT_DRY;// Bom tat thermalDryBlock=1 => Tat bom vi nuoc qua nong  
        else if (!g_manualMode && soilDry && !cooldownDone) g_pumpStatus = PUMP_ST_COOLDOWN;// dang auto , dat kho , chx het chu ky nghi => chx bat bom dang doi het chu ky nghi 
        else                        g_pumpStatus = PUMP_ST_IDLE;// Bom o trang thai cho 

        /* Dieu khien quat va den  */
        if (g_manualMode) // dang o thu cong 
        {
            fanState = g_manualFanRequest;// nhan yeu cau tu nguoi dung 
            lampState = g_manualLampRequest;// nhan yeu cau tu nguoi dung 
        }
        else// dang o auto 
        {
            if (haveSensor && sensor.temperatureValid)// co data cam bien va du lieu nhiet do hop le 
            {
                if (sensor.temperatureC >= (int16_t)cfg.fanOnTempC) fanState = 1U;// Nhiet do > nguong => bat quat 
                else if (sensor.temperatureC <= ((int16_t)cfg.fanOnTempC - 2)) fanState = 0U;//// Nhiet do < nguong => tat quat
            }
            if (haveSensor && sensor.lightValid)//co data cam bien va du lieu anh sang  hop le 
            {
                if (sensor.lightLux < cfg.lightOnLux) lampState = 1U;// Anh sang  < nguong => bat den  
                else if (sensor.lightLux > (cfg.lightOnLux + 20U)) lampState = 0U;// ngc lai tat den 
            }
        }
				
				// Dieu khien phan cung theo trang thai co Fan va Lamp 
        Relay_SetFan(fanState);
        Relay_SetLamp(lampState);
				
				// cap nhat trang thai quat va den hien tai cho cac task khac sd VD: oled 
        g_fanOn = fanState;
        g_lampOn = lampState;
				
        /* Phat coi khi be o trang thai thieu nuoc */
        if (g_tankLowAlarm && !previousTankLow)
        {
            buzzerPulseActive = 1U;
            buzzerPulseStart = now;
        }
        if (buzzerPulseActive &&((now - buzzerPulseStart) >= pdMS_TO_TICKS(TANK_LOW_BEEP_MS)))// bao >5s thi tat bom 
        {
            buzzerPulseActive = 0U;
        }
        Buzzer_Set(buzzerPulseActive);// bat coi theo state buzzerPulseActive
        previousTankLow = g_tankLowAlarm;// cap nhat trang thai bao het nuoc hien tai 
        vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(CONTROL_TASK_PERIOD_MS));// Ket thuc vong lap cua Control Task cho 100ms tiep de lap lai  
    }
}

static void ButtonTask(void *argument)
{
	  // Input pulldown 
    uint8_t previousRaw[4] = {0U,0U,0U,0U};// trang thai button cua 4 nut trc do 
    uint8_t stableState[4] = {0U,0U,0U,0U};// cap nhat trang thai hien tai cua  4 nut 
    uint8_t count[4] = {0U,0U,0U,0U};
    uint8_t i;
    (void)argument;
    for (;;)
    {
        AppHeartbeat_Increment(APP_HB_BUTTON);// Kiem tra hoat dong cua Task Button 
        for (i = 0U; i < 4U; i++)// quet 4 nut 
        {
            uint8_t raw = Button_IsPressed(i);// Doc trang thai nut nhan hien tai 
            if (raw == previousRaw[i])// if lan nhan hien tai same lan nhan trc do 
            {
                if (count[i] < 3U) count[i]++;// chong doi 
                if ((count[i] >= 3U) && (stableState[i] != raw)) // button on dinh va stable phai khac raw neu trung raw thi ko thuc hien gui lai du lieu button so nx 
                {
                    stableState[i] = raw;
                    if (raw) // raw =1 thi moi gui su kien button la khi nhat nut 
                    {
                        ButtonEvent_t ev = (ButtonEvent_t)i;// luu du lieu ung voi moi su kien button 
                        (void)xQueueSend(g_buttonQueue, &ev, 0U);// gui du lieu button vao g_buttonQueue vd UI co the nhan de xu ly tren Oled 
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

//Luu cau hình xuong Flash
//Task xu lý yêu cau luu các nguong dã duoc nguoi dùng chinh trên menu,  do am dat, nhiet do bat quat và thoi diem tuoi.
static void ConfigTask(void *argument)
{
    AppConfig_t cfg;
    (void)argument;
    for (;;)
    {
        AppHeartbeat_Increment(APP_HB_CONFIG);// Kiem tra hoat dong cua Task Config 
        if (xQueueReceive(g_configSaveQueue, &cfg, pdMS_TO_TICKS(500U)) == pdTRUE)// Cho nhan yeu cau luu cau hinh 
        {
					// if nhan dc cau hinh 
            g_configSaveState = 1U;// trang thai dang luu 
            if (ConfigStore_Save(&cfg)) g_configSaveState = 2U;// if luu thanh cong thi g_configSaveState =2 
            else g_configSaveState = 3U;// neu ko luu thanh cong g_configSaveState=3 
            vTaskDelay(pdMS_TO_TICKS(1500U));
            g_configSaveState = 0U;// bat dau xet lai 
        }
    }
}


//HealthTask là phát hien truong hop mot Task bi treo hoac không còn thuc thi, sau dó dua thiet bi ve trang thái an toàn và reset STM32
static void HealthTask(void *argument)
{
    uint32_t previous[APP_HB_COUNT];  //Luu giá tri heartbeat cua tung Task o lan kiem tra truoc
    uint8_t stale[APP_HB_COUNT] = {0U};//Ðem so lan liên tiep heartbeat không thay doi
    uint8_t i;//Duyet qua các bo dem heartbeat
    (void)argument;
		//APP_HB_COUNT =6 task 
    for (i = 0U; i < APP_HB_COUNT; i++) previous[i] = AppHeartbeat_Get((AppHeartbeatId_t)i);// luu gia tri heartbeat cua tung task de so sanh 
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(1000U));// delay 1s r kiem tra xem heartbeat hien tai co tang ko 
        for (i = 0U; i < APP_HB_COUNT; i++)//6 task 
        {
            uint32_t current = AppHeartbeat_Get((AppHeartbeatId_t)i);// lay gia tri heartbeat hien tai cua cac task 
            if (current == previous[i])// neu sau 1s ma heartbeat hien tai = heartbeat trc 1s do thi tang stale[i] if stale[i]>3 thi reset toan bo he thong 
            {
                if (stale[i] < 3U) stale[i]++;
            }
            else stale[i] = 0U;//ko cap nhat stable 
            previous[i] = current;// cap nhat heartbeat hien tai 
            if (stale[i] >= 3U)// neu stable >3 
            {
                Relay_AllOff();// tat het relay 
                Buzzer_Set(1U);
                Board_DelayMs(100U);
                Buzzer_Set(0U);
                NVIC_SystemReset();// Khoi dong lai Stm32 
                for (;;) { Relay_AllOff(); }
            }
        }
    }
}
uint8_t TaskN3_InitShared(void)
{
    if (!AppState_Init())// neu khoi tao that bai thi tat het moi thu 
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
    if (xTaskCreate(ControlTask, "Control", 448U, NULL, 4U, NULL) != pdPASS) return pdFAIL;// nhan du lieu cam bien de dieu khien thiet bi
    if (xTaskCreate(ButtonTask, "Buttons", 192U, NULL, 2U, NULL) != pdPASS) return pdFAIL;
    if (xTaskCreate(ConfigTask, "Config", 320U, NULL, 2U, NULL) != pdPASS) return pdFAIL;
    if (xTaskCreate(HealthTask, "Health", 192U, NULL, 4U, NULL) != pdPASS) return pdFAIL;
    return pdPASS;
}
