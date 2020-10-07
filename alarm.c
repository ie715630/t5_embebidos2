/*
 * Copyright 2016-2020 NXP
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * o Redistributions of source code must retain the above copyright notice, this list
 *   of conditions and the following disclaimer.
 *
 * o Redistributions in binary form must reproduce the above copyright notice, this
 *   list of conditions and the following disclaimer in the documentation and/or
 *   other materials provided with the distribution.
 *
 * o Neither the name of NXP Semiconductor, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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

void second_task(void *parameters);
void minute_task(void *parameters);
void hour_task(void *parameters);

void alarm_task(void *parameters);
void print_task(void *parameters);

SemaphoreHandle_t minutes_semaphore;
SemaphoreHandle_t hours_semaphore;

QueueHandle_t time_queue;

EventGroupHandle_t alarm_event;
SemaphoreHandle_t print_mutex;

int main(void)
{
    alarm_t alarm;

    alarm.seconds = 0;
    alarm.minutes = 0;
    alarm.hours = 0;

    /* Init board hardware. */
    BOARD_InitBootPins();
    BOARD_InitBootClocks();
    BOARD_InitBootPeripherals();
#ifndef BOARD_INIT_DEBUG_CONSOLE_PERIPHERAL
    /* Init FSL debug console. */
    BOARD_InitDebugConsole();
#endif

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

    while(1) {
        i++ ;
        /* 'Dummy' NOP to allow source level single stepping of
           tight while() loop */
        __asm volatile ("nop");
    }
    return 0 ;
}

void alarm_task(void *parameters)
{

    const EventBits_t xBitsOfAlarm = BIT_SECONDS | BIT_MINUTES | BIT_HOURS;

    for(;;)
    {
        xEventGroupWaitBits(alarm_event,
                xBitsOfAlarm,
                pdTRUE, pdTRUE,
                portMAX_DELAY);
        xSemaphoreTake(print_mutex, portMAX_DELAY);
        PRINTF("Alarm\n");
        xSemaphoreGive(print_mutex);
    }
}

void print_task(void *parameters)
{
    static uint8_t local_secs = 0;
    static uint8_t local_mins = 0;
    static uint8_t local_hrs = 0;

    for(;;)
    {
        xSemaphoreTake(print_mutex, portMAX_DELAY);
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
        PRINTF("%02d:%02d:%02d", local_hrs, local_mins, local_secs);
        xSemaphoreGive(print_mutex);
    }
}


void second_task(void *parameters)
{
    static uint8_t counter_seconds = 0;

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
        vTaskDelayUntil(&xLastWakeTime, xfactor);
    }
}
void minute_task(void *parameters)
{
    static uint8_t counter_minutes = 0;

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
    }
}
void hour_task(void *parameters)
{
    static uint8_t counter_hours = 0;

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
    }
}
