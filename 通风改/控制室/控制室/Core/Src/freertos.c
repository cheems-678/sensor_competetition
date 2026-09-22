#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

#include "lora.h"

osThreadId defaultTaskHandle;
static StaticTask_t g_idle_tcb;
static StackType_t g_idle_stack[configMINIMAL_STACK_SIZE];

void StartDefaultTask(void const *argument);

void vApplicationGetIdleTaskMemory(StaticTask_t **tcb,
                                   StackType_t **stack,
                                   uint32_t *stack_size)
{
    *tcb = &g_idle_tcb;
    *stack = g_idle_stack;
    *stack_size = configMINIMAL_STACK_SIZE;
}

void MX_FREERTOS_Init(void)
{
    osThreadDef(defaultTask, StartDefaultTask, osPriorityNormal, 0, 256);
    defaultTaskHandle = osThreadCreate(osThread(defaultTask), NULL);
    if (defaultTaskHandle == NULL)
    {
        Error_Handler();
    }
}

void StartDefaultTask(void const *argument)
{
    (void)argument;
    for (;;)
    {
        LoraP2PRX();
        osDelay(1U);
    }
}
