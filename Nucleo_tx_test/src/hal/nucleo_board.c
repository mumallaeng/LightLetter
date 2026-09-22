/* NUCLEO-F411RE: PA0/A0 = TIM2_CH1, PA2/PA3 = USART2 ST-LINK VCP.
 * 입력은 3.3V FPGA 디지털 신호. UART는 main 문맥에서만 사용한다.
 */
#include "nucleo_board.h"
#include "stm32f4xx_hal.h"
#include <string.h>

#define CAPTURE_SIZE 2048U
static TIM_HandleTypeDef capture_timer;
static UART_HandleTypeDef log_uart;
static volatile uint32_t captures[CAPTURE_SIZE];
static volatile uint32_t head, tail, losses, edges;

static void fatal(void)
{
    __disable_irq();
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_SET);
    for (;;) { }
}

static void clock_init(void)
{
    RCC_OscInitTypeDef osc = {0};
    RCC_ClkInitTypeDef clocks = {0};
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState = RCC_PLL_ON;
    osc.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    osc.PLL.PLLM = 16;
    osc.PLL.PLLN = 200;
    osc.PLL.PLLP = RCC_PLLP_DIV2;
    osc.PLL.PLLQ = 4;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) fatal();
    clocks.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                       RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clocks.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clocks.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clocks.APB1CLKDivider = RCC_HCLK_DIV2;
    clocks.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clocks, FLASH_LATENCY_3) != HAL_OK) fatal();
}

void board_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    TIM_IC_InitTypeDef input = {0};
    HAL_Init();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_5;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);
    clock_init();

    __HAL_RCC_USART2_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_2 | GPIO_PIN_3;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &gpio);
    log_uart.Instance = USART2;
    log_uart.Init.BaudRate = 115200;
    log_uart.Init.WordLength = UART_WORDLENGTH_8B;
    log_uart.Init.StopBits = UART_STOPBITS_1;
    log_uart.Init.Parity = UART_PARITY_NONE;
    log_uart.Init.Mode = UART_MODE_TX_RX;
    log_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    log_uart.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&log_uart) != HAL_OK) fatal();

    __HAL_RCC_TIM2_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_0;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Alternate = GPIO_AF1_TIM2;
    HAL_GPIO_Init(GPIOA, &gpio);
    capture_timer.Instance = TIM2;
    /* APB1=50 MHz, APB1 timer=100 MHz. PSC=99 => 1 MHz (1 us/tick). */
    capture_timer.Init.Prescaler = 99;
    capture_timer.Init.CounterMode = TIM_COUNTERMODE_UP;
    capture_timer.Init.Period = 0xFFFFFFFFU;
    capture_timer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    capture_timer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_IC_Init(&capture_timer) != HAL_OK) fatal();
    input.ICPolarity = TIM_INPUTCHANNELPOLARITY_RISING;
    input.ICSelection = TIM_ICSELECTION_DIRECTTI;
    input.ICPrescaler = TIM_ICPSC_DIV1;
    input.ICFilter = 0;
    if (HAL_TIM_IC_ConfigChannel(&capture_timer, &input, TIM_CHANNEL_1) != HAL_OK) fatal();
    HAL_NVIC_SetPriority(TIM2_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
    if (HAL_TIM_IC_Start_IT(&capture_timer, TIM_CHANNEL_1) != HAL_OK) fatal();
}

void SysTick_Handler(void)
{
    HAL_IncTick();
}

void TIM2_IRQHandler(void)
{
    if (__HAL_TIM_GET_FLAG(&capture_timer, TIM_FLAG_CC1OF) != RESET) {
        __HAL_TIM_CLEAR_FLAG(&capture_timer, TIM_FLAG_CC1OF);
        ++losses;
    }
    HAL_TIM_IRQHandler(&capture_timer);
}

void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *timer)
{
    uint32_t next, timestamp;
    if (timer->Instance != TIM2 || timer->Channel != HAL_TIM_ACTIVE_CHANNEL_1) return;
    timestamp = HAL_TIM_ReadCapturedValue(timer, TIM_CHANNEL_1);
    ++edges;
    next = (head + 1U) & (CAPTURE_SIZE - 1U);
    if (next == tail) {
        ++losses;
        return;
    }
    captures[head] = timestamp;
    __DMB();
    head = next;
}

int board_capture_pop(uint32_t *timestamp)
{
    if (tail == head) return 0;
    __DMB();
    *timestamp = captures[tail];
    __DMB();
    tail = (tail + 1U) & (CAPTURE_SIZE - 1U);
    return 1;
}

void board_capture_discard(void)
{
    uint32_t irq_state = __get_PRIMASK();
    __disable_irq();
    tail = head;
    __DMB();
    __set_PRIMASK(irq_state);
}

uint32_t board_capture_losses(void) { return losses; }
uint32_t board_capture_edges(void) { return edges; }
uint32_t board_millis(void) { return HAL_GetTick(); }
void board_toggle_led(void) { HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5); }

void board_write(const char *text)
{
    if (HAL_UART_Transmit(&log_uart, (uint8_t *)text,
                         (uint16_t)strlen(text), 100U) != HAL_OK) fatal();
}
