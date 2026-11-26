
#include "stm32f10x.h"
#include "FreeRTOS.h"
#include "task.h"

#include "app_config.h"
#include "app_state.h"
#include "board.h"
#include "analog_input.h"
#include "taskN2.h"

// Hàm xử lý khi khởi tạo hệ thống thất bại
static void Error_Handler(void)
{
    __disable_irq();

    while (1)
    {
        // Dừng hệ thống để kiểm tra lỗi
    }
}

int main(void)
{
    // Đặt địa chỉ Vector Table của chương trình ứng dụng
    // Project sử dụng địa chỉ bắt đầu APP_START_ADDRESS
    SCB->VTOR = APP_START_ADDRESS;

    // Cập nhật tần số xung nhịp hệ thống
    SystemCoreClockUpdate();

    // Khởi tạo phần cứng:
    // GPIO, Relay, Buzzer, Button, DHT11 và Timer TIM2
    // TIM2 được sử dụng để tạo thời gian micro giây khi đọc DHT11
    Board_Init();

    // Khởi tạo ADC1 và DMA
    // PA0: Cảm biến độ ẩm đất
    // PA2: Cảm biến mực nước
    if (AnalogInput_Init() != 1U)
    {
        Error_Handler();
    }

    // Khởi tạo Queue, Mutex và trạng thái cảm biến
    // Tạo g_sensorQueue để lưu dữ liệu cảm biến
    // Tạo g_sensorDataMutex để bảo vệ dữ liệu dùng chung
    // Gọi Sensors_Init() để khởi tạo trạng thái các cảm biến
    if (AppState_Init() != 1U)
    {
        Error_Handler();
    }

    // Tạo duy nhất TaskN2
    // TaskN2 thực hiện đọc cảm biến đất, nước và DHT11
    if (TaskN2_Create() != pdPASS)
    {
        Error_Handler();
    }

    // Khởi động bộ lập lịch FreeRTOS
    // Từ đây TaskN2 bắt đầu được thực thi
    vTaskStartScheduler();

    // Nếu Scheduler không khởi động được thì dừng hệ thống
    Error_Handler();

    return 0;
}
