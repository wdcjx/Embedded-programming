/**
  ******************************************************************************
  * @file    FreeRTOSConfig.h
  * @brief   STM32F103 中密度（C8T6, 20K RAM）+ FreeRTOS-Kernel（ARM_CM3 port）配置模板
  * @note    适用 FreeRTOS-Kernel 10.x ~ 11.x；升级内核大版本需复核本文件。
  *          放置：与 main.c 同目录，并加入 Keil include path。
  *          内存口径：heap_4，所有任务栈/TCB/队列都从 configTOTAL_HEAP_SIZE 出；
  *          C8T6 总 RAM 20K，heap ≤ 10K；RTOS 工程优先选 RCT6/ZET6。
  ******************************************************************************
  */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>
extern uint32_t SystemCoreClock;

/* ---------------- 基础调度 ---------------- */
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configCPU_CLOCK_HZ                      (SystemCoreClock)
#define configTICK_RATE_HZ                      1000                /* 1ms tick */
#define configMAX_PRIORITIES                    7
#define configMINIMAL_STACK_SIZE                ((uint16_t)128)     /* 单位：字(4B)；128字=512B */
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_TASK_NOTIFICATIONS            1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_QUEUE_SETS                    0
#define configUSE_TIMERS                        0                   /* 不用软件定时器→timers.c 可不加 */
#define configUSE_NEWLIB_REENTRANT              0
#define configENABLE_BACKWARD_COMPATIBILITY     1
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 0
#define configSTACK_DEPTH_TYPE                  uint16_t

/* ---------------- 内存（heap_4） ---------------- */
#define configSUPPORT_STATIC_ALLOCATION         0
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configTOTAL_HEAP_SIZE                   ((size_t)(8 * 1024)) /* C8T6：≤10K */

/* ---------------- 钩子与调试 ---------------- */
#define configCHECK_FOR_STACK_OVERFLOW          2                    /* 必开：抓任务栈溢出 */
#define configUSE_MALLOC_FAILED_HOOK            1                    /* 必开：抓堆耗尽 */
#define configUSE_TRACE_FACILITY                0
#define configUSE_STATS_FORMATTING_FUNCTIONS    0
#define configGENERATE_RUN_TIME_STATS           0

/* ---------------- Cortex-M3 中断优先级 ---------------- */
#ifdef __NVIC_PRIO_BITS
  #define configPRIO_BITS __NVIC_PRIO_BITS                      /* F1 = 4 */
#else
  #define configPRIO_BITS 4
#endif
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY         15  /* 最低（数字最大） */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    5   /* 抢占优先级数字 ≥5 的中断才可调 FromISR API */
#define configKERNEL_INTERRUPT_PRIORITY \
        ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY \
        ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )

/* ---------------- 向量接管（不改启动文件） ----------------
 * port.c 编译时把内核三个异常处理函数重命名成启动文件的弱符号名。
 * 注意：stm32f10x_it.c / 用户代码中不得再定义这三个函数。 */
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

/* ---------------- API 裁剪（按需减小 Flash） ---------------- */
#define INCLUDE_vTaskPrioritySet            1
#define INCLUDE_uxTaskPriorityGet           1
#define INCLUDE_vTaskDelete                 1
#define INCLUDE_vTaskSuspend                1
#define INCLUDE_vTaskDelay                  1
#define INCLUDE_vTaskDelayUntil             1
#define INCLUDE_xTaskGetSchedulerState      1
#define INCLUDE_xTaskGetCurrentTaskHandle   1
#define INCLUDE_xTaskGetIdleTaskHandle      0
#define INCLUDE_eTaskGetState               0
#define INCLUDE_xTimerPendFunctionCall      0

/* ---------------- 断言 ---------------- */
extern void vAssertCalled(const char *pcFile, int pcLine);
#define configASSERT(x) if ((x) == 0) vAssertCalled(__FILE__, __LINE__)

#endif /* FREERTOS_CONFIG_H */
