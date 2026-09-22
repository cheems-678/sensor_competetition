#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

#include "fan_pwm.h"
#include "lora.h"
#include "master_queues.h"
#include "master_runtime.h"

static uint32_t defaultTaskStack[256];
static osStaticThreadDef_t defaultTaskControl;
static uint32_t loRaTaskStack[256];
static osStaticThreadDef_t loRaTaskControl;

osThreadId defaultTaskHandle;
osThreadId LoRaTaskHandle;

static StaticTask_t xIdleTaskTCBBuffer;
static StackType_t xIdleStack[configMINIMAL_STACK_SIZE];

void StartDefaultTask(void const *argument);
void StartLoRaTask(void const *argument);

void vApplicationGetIdleTaskMemory(StaticTask_t **tcb,
                                   StackType_t **stack,
                                   uint32_t *stack_size)
{
    *tcb = &xIdleTaskTCBBuffer;
    *stack = &xIdleStack[0];
    *stack_size = configMINIMAL_STACK_SIZE;
}

void MX_FREERTOS_Init(void)
{
    if (MasterQueues_Init() == 0U)
    {
        Error_Handler();
    }

    FanPwm_Init();

    osThreadStaticDef(defaultTask, StartDefaultTask, osPriorityNormal, 0, 256,
                      defaultTaskStack, &defaultTaskControl);
    defaultTaskHandle = osThreadCreate(osThread(defaultTask), NULL);

    osThreadStaticDef(LoRaTask, StartLoRaTask, osPriorityAboveNormal, 0, 256,
                      loRaTaskStack, &loRaTaskControl);
    LoRaTaskHandle = osThreadCreate(osThread(LoRaTask), NULL);

    if ((defaultTaskHandle == NULL) || (LoRaTaskHandle == NULL))
    {
        Error_Handler();
    }
}

void StartDefaultTask(void const *argument)
{
    (void)argument;
    MasterRuntime_Init();
    for (;;)
    {
        MasterRuntime_ProcessOne(HAL_GetTick(), pdMS_TO_TICKS(10U));
    }
}

void StartLoRaTask(void const *argument)
{
    (void)argument;
    for (;;)
    {
        LoraP2PRX();
        LoraP2PTX();
        osDelay(1U);
    }
}
