/**
 ******************************************************************************
 * @file    main_hal.c  —  HAL 裸机骨架（CubeMX 对照 / 手写工程起点）
 ******************************************************************************
 * 板级假设 : STM32F103C8T6 @ 72MHz（HSE 8MHz ×9）、APB1 36MHz / APB2 72MHz
 * 工作流   : CubeMX(MDK-ARM) 生成工程后，业务代码只写在 USER CODE 区间内；
 *            区间外改动会在重新生成代码时被删除（hal-basics.md §3）。
 *            本文件同时是手工 HAL 工程的完整起点（含 SystemClock_Config/MspInit）。
 * 功能     : PC13 LED 闪烁 + USART1(PA9/PA10, 115200) 打印版本号与 uptime。
 * 纪律     : 回调=中断上下文禁 HAL_Delay（铁律二）；HAL 返回值必须检查。
 * 提示     : 单独在 VS Code 打开时报"无法打开源文件 stm32f1xx_hal.h"属预期——
 *            该头文件在 HAL 驱动包/工程目录中，不影响 Keil 编译。
 * 交付声明 : 未经编译验证。验证路径：CubeMX 生成 F103C8 空工程 → 按本骨架合并
 *            USER CODE 区 → Keil 编译 → MicroLIB 勾选。
 ******************************************************************************
 */
#include "stm32f1xx_hal.h"
#include <stdio.h>

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
void Error_Handler(void);

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */
static uint32_t boot_tick;   /* 仅 main 上下文读写，不跨中断，无需 volatile */
/* USER CODE END PV */

/* printf 重定向（Keil + MicroLIB；GCC 工具链请改用 _write） */
int fputc(int ch, FILE *f)
{
    (void)f;
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, 10);   /* 10ms 超时，失败静默丢弃 */
    return ch;
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_USART1_UART_Init();

    /* USER CODE BEGIN 2 */
    boot_tick = HAL_GetTick();
    printf("\r\n[HAL skeleton] build " __DATE__ " " __TIME__ "\r\n");
    /* USER CODE END 2 */

    while (1)
    {
        /* USER CODE BEGIN 3 */
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        printf("uptime %lu s\r\n", (unsigned long)((HAL_GetTick() - boot_tick) / 1000U));
        HAL_Delay(500);
        /* USER CODE END 3 */
    }
}

/**
 * @brief  72MHz 时钟配置：HSE 8M → PREDIV/1 → PLL×9 → SYSCLK=72MHz
 *         AHB /1，APB1 /2 = 36MHz（定时器×2=72MHz），APB2 /1 = 72MHz
 */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
    {
        Error_Handler();
    }
    __HAL_FLASH_PREFETCH_BUFFER_ENABLE();
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();          /* 时钟最先开 */

    /* PC13 = 板载 LED（低电平点亮），上电先灭 */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    GPIO_InitStruct.Pin = GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
}

static void MX_USART1_UART_Init(void)
{
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart1) != HAL_OK)   /* MspInit（时钟+引脚）在其内部先执行 */
    {
        Error_Handler();
    }
}

/* HAL 约定：外设 MSP 初始化（时钟 + GPIO）写在 MspInit，由 HAL_xxx_Init 自动调用 */
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    if (huart->Instance == USART1)
    {
        __HAL_RCC_USART1_CLK_ENABLE();
        __HAL_RCC_GPIOA_CLK_ENABLE();

        GPIO_InitStruct.Pin = GPIO_PIN_9;               /* PA9 TX：复用推挽 */
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

        GPIO_InitStruct.Pin = GPIO_PIN_10;              /* PA10 RX：浮空输入 */
        GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    }
}

void Error_Handler(void)
{
    /* 生产代码建议：关闭中断 + 诊断输出 + 复位/看门狗自愈。
       调试期可在此打断点定位首个失败的外设初始化。 */
    __disable_irq();
    while (1)
    {
    }
}
