/**
 * @file    alarm.c
 * @brief   Application entry point.
 */
#include <stdio.h>
#include "board.h"
#include "peripherals.h"
#include "pin_mux.h"
#include "clock_config.h"
#include "MK66F18.h"
#include "fsl_debug_console.h"
/* TODO: insert other include files here. */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include "event_groups.h"

/* TODO: insert other definitions and declarations here. */

#define BIT_SECONDS (1 << 0)
#define BIT_MINUTES (1 << 1)
#define BIT_HOURS (1 << 2)

#define INITIAL_SEC 55
#define INITIAL_MIN 59
#define INITIAL_HR 23


typedef enum
{
    seconds_type,
    minutes_type,
    hours_type
} time_types_t;

typedef struct
{
    time_types_t time_type;
    uint8_t value;
} time_msg_t;

typedef struct
{
    uint8_t hours;
    uint8_t minutes;
    uint8_t seconds;
} alarm_t;

static void second_task(void *parameters);
static void minute_task(void *parameters);
static void hour_task(void *parameters);

static void alarm_task(void *parameters);
static void print_task(void *parameters);

SemaphoreHandle_t minutes_semaphore;
SemaphoreHandle_t hours_semaphore;

QueueHandle_t time_queue;

EventGroupHandle_t alarm_event;
SemaphoreHandle_t print_mutex;

alarm_t alarm;

int main(void)
{
	alarm.seconds = 5;
	alarm.minutes = 0;
	alarm.hours = 0;

    /* Init board hardware. */
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitBootPeripherals();
    BOARD_InitDebugConsole();

    xTaskCreate(second_task, "Seconds", configMINIMAL_STACK_SIZE, NULL,
    		configMAX_PRIORITIES - 4, NULL);
    xTaskCreate(minute_task, "Minutes", configMINIMAL_STACK_SIZE, NULL,
            configMAX_PRIORITIES - 4, NULL);
    xTaskCreate(hour_task, "Hours", configMINIMAL_STACK_SIZE, NULL,
            configMAX_PRIORITIES - 4, NULL);
    xTaskCreate(alarm_task, "Alarm", configMINIMAL_STACK_SIZE, NULL,
            configMAX_PRIORITIES - 4, NULL);
    xTaskCreate(print_task, "Print", configMINIMAL_STACK_SIZE, NULL,
            configMAX_PRIORITIES - 4, NULL);

    /* Force the counter to be placed into memory. */
    volatile static int i = 0 ;
    /* Enter an infinite loop, just incrementing a counter. */
    minutes_semaphore = xSemaphoreCreateBinary();
    hours_semaphore = xSemaphoreCreateBinary();

    time_queue = xQueueCreate(3, sizeof(time_msg_t));
    print_mutex = xSemaphoreCreateMutex();
    alarm_event = xEventGroupCreate();

    vTaskStartScheduler();

   while(1){

   }

    return 0 ;
}

static void alarm_task(void *parameters)
{

    const EventBits_t xBitsOfAlarm = BIT_SECONDS | BIT_MINUTES | BIT_HOURS;


    for(;;)
    {
        xEventGroupWaitBits(alarm_event,
                xBitsOfAlarm,
                pdTRUE, pdTRUE,
                portMAX_DELAY);
        xSemaphoreTake(print_mutex, portMAX_DELAY);
        PRINTF("\r\nAlarm");
        xSemaphoreGive(print_mutex);
    }
}

static void print_task(void *parameters)
{
    static uint8_t local_secs = INITIAL_SEC;
    static uint8_t local_mins = INITIAL_MIN;
    static uint8_t local_hrs = INITIAL_HR;

    for(;;)
    {
        time_msg_t received_msg_from_queue;
        xQueueReceive(time_queue,
                      &received_msg_from_queue,
                      portMAX_DELAY);
        time_types_t time_type_received = received_msg_from_queue.time_type;
        switch (time_type_received)
        {
            case seconds_type:
                local_secs = received_msg_from_queue.value;
                break;
            case minutes_type:
                local_mins = received_msg_from_queue.value;
                break;
            case hours_type:
                local_hrs = received_msg_from_queue.value;
                break;
        }
        xSemaphoreTake(print_mutex, portMAX_DELAY);
        PRINTF("\e[1;1H\e[2J");
        PRINTF("%02d:%02d:%02d", local_hrs, local_mins, local_secs);
        xSemaphoreGive(print_mutex);
    }
}


static void second_task(void *parameters)
{
    static uint8_t counter_seconds = INITIAL_SEC;

    for(;;)
    {
        ++counter_seconds;

        if(counter_seconds >= 60)
        {
            counter_seconds = 0;
            xSemaphoreGive(minutes_semaphore);
        }

        static time_msg_t secs_msg;
        secs_msg.time_type = seconds_type;
        secs_msg.value = counter_seconds;
        xQueueSendToBack(time_queue, &secs_msg, 0);

        TickType_t  xLastWakeTime = xTaskGetTickCount();
        TickType_t   xfactor = pdMS_TO_TICKS(1000);

        if (counter_seconds == alarm.seconds)
        {
        	xEventGroupSetBits(alarm_event, BIT_SECONDS);
        }

        vTaskDelayUntil(&xLastWakeTime, xfactor);
    }
}
static void minute_task(void *parameters)
{
    static uint8_t counter_minutes = INITIAL_MIN;

    while(true)
    {
        /* PortMAX_Delay so it doesent time out*/
        xSemaphoreTake(minutes_semaphore, portMAX_DELAY);
        ++counter_minutes;

        if(counter_minutes >= 60)
        {
            counter_minutes = 0;
            xSemaphoreGive(hours_semaphore);
        }

        static time_msg_t mins_msg;
        mins_msg.time_type = minutes_type;
        mins_msg.value = counter_minutes;
        xQueueSendToBack(time_queue, &mins_msg, 0);

        if (counter_minutes == alarm.minutes)
        {
        	xEventGroupSetBits(alarm_event, BIT_MINUTES);
        }
    }
}
static void hour_task(void *parameters)
{
    static uint8_t counter_hours = INITIAL_HR;

    while(true)
    {
        xSemaphoreTake(hours_semaphore, portMAX_DELAY);
        ++counter_hours;

        if(counter_hours >= 24)
        {
            counter_hours = 0;
        }

        static time_msg_t hrs_msg;
        hrs_msg.time_type = hours_type;
        hrs_msg.value = counter_hours;
        xQueueSendToBack(time_queue, &hrs_msg, 0);

        if (counter_hours == alarm.hours)
        {
        	xEventGroupSetBits(alarm_event, BIT_HOURS);
        }
    }
}
