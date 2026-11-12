/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : CS43L22 1 kHz tone on STM32F4DISCOVERY
  ******************************************************************************
  */
/* USER CODE END Header */

#include "main.h"
#include "usb_host.h"
#include <math.h>
#include <stdint.h>

/* Private defines -----------------------------------------------------------*/
#define CS43L22_I2C_ADDR       (0x94U)  /* HAL expects 8-bit shifted address */
#define AUDIO_SAMPLE_RATE     (48000U)
#define TONE_FREQUENCY        (1000U)
#define TONE_FRAMES           (96U)    /* 2 cycles of 1 kHz at 48 kHz */
#define I2S_HALFWORDS         (TONE_FRAMES * 2U) /* Left + right per frame */
#define TONE_AMPLITUDE        (200.0f)

#define PI_F                  (3.14159265358979323846f)

/* Peripheral handles --------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;
I2S_HandleTypeDef hi2s3;
SPI_HandleTypeDef hspi1;

/* Audio buffer: interleaved L/R 16-bit samples ------------------------------*/
static int16_t audio_buffer[I2S_HALFWORDS];

/* Function prototypes -------------------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_I2S3_Init(void);
static void MX_SPI1_Init(void);
void MX_USB_HOST_Process(void);

static HAL_StatusTypeDef CS43L22_WriteReg(uint8_t reg, uint8_t value);
static HAL_StatusTypeDef CS43L22_Init(void);
static void Generate_Tone(void);
void Error_Handler(void);

/* Main ----------------------------------------------------------------------*/
int main(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_I2C1_Init();
    MX_I2S3_Init();
    MX_SPI1_Init();
    MX_USB_HOST_Init();

    /* Reset the CS43L22 using the board's PD4 reset line, then configure it. */
    if (CS43L22_Init() != HAL_OK)
    {
        Error_Handler();
    }

    Generate_Tone();

    /*Use to make delay*/
    uint32_t ledTick = HAL_GetTick();

    /*
     * Blocking I2S playback in short chunks is used here to keep this
     * standalone main.c from requiring a DMA channel configuration.
     * The buffer is 96 stereo frames = 2 ms at 48 kHz.
     */
    while (1)
    {
        if (HAL_I2S_Transmit(&hi2s3,
                             (uint16_t *)audio_buffer,
                             I2S_HALFWORDS,
                             100U) != HAL_OK)
        {
            Error_Handler();
        }

        /* Blink LEDs every 1 second */
        if ((HAL_GetTick() - ledTick) >= 1000U)
        {
            ledTick = HAL_GetTick();

            HAL_GPIO_TogglePin(GPIOD,
                               GPIO_PIN_12 |
                               GPIO_PIN_13 |
                               GPIO_PIN_14 |
                               GPIO_PIN_15);
        }

        MX_USB_HOST_Process();

    }
}

/* Generate a continuous 1 kHz sine wave, stereo, at a nominal 48 kHz. */
static void Generate_Tone(void)
{
    for (uint32_t i = 0; i < TONE_FRAMES; i++)
    {
        float angle = (2.0f * PI_F * (float)TONE_FREQUENCY * (float)i) /
                      (float)AUDIO_SAMPLE_RATE;
        int16_t sample = (int16_t)(TONE_AMPLITUDE * sinf(angle));

        audio_buffer[2U * i]      = sample; /* Left */
        audio_buffer[2U * i + 1U] = sample; /* Right */
    }
}

/* Write one CS43L22 register. */
static HAL_StatusTypeDef CS43L22_WriteReg(uint8_t reg, uint8_t value)
{
    uint8_t tx[2] = { reg, value };

    return HAL_I2C_Master_Transmit(&hi2c1,
                                   CS43L22_I2C_ADDR,
                                   tx,
                                   sizeof(tx),
                                   100U);
}

/*
 * Initialize CS43L22 using the codec unlock/power-up sequence used by
 * the STM32F4DISCOVERY board. CN4 is the board's audio output jack.
 */
static HAL_StatusTypeDef CS43L22_Init(void)
{
    HAL_StatusTypeDef status;

    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_4, GPIO_PIN_RESET);
    HAL_Delay(10U);
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_4, GPIO_PIN_SET);
    HAL_Delay(10U);

#define CS43L22_WRITE_REG(r, v) do { \
    status = CS43L22_WriteReg((r), (v)); \
    if (status != HAL_OK) return status; \
} while (0)

    /* Required codec initialization/unlock sequence. */
    CS43L22_WRITE_REG(0x00U, 0x99U);
    CS43L22_WRITE_REG(0x47U, 0x80U);
    CS43L22_WRITE_REG(0x32U, 0x80U);
    CS43L22_WRITE_REG(0x32U, 0x00U);
    CS43L22_WRITE_REG(0x00U, 0x00U);

    /* Power down while configuring. */
    CS43L22_WRITE_REG(0x02U, 0x01U);

    /* Enable headphone outputs and automatic clock detection. */
    CS43L22_WRITE_REG(0x04U, 0xAFU);
    CS43L22_WRITE_REG(0x05U, 0x81U);

    /* Philips I2S format, 16-bit data. */
    CS43L22_WRITE_REG(0x06U, 0x07U);

    /* Disable analog passthrough; set conservative output volumes. */
    CS43L22_WRITE_REG(0x0AU, 0x00U);
    CS43L22_WRITE_REG(0x20U, 0x18U); /* Headphone A volume */
    CS43L22_WRITE_REG(0x21U, 0x18U); /* Headphone B volume */
    CS43L22_WRITE_REG(0x22U, 0x00U); /* PCM A volume */
    CS43L22_WRITE_REG(0x23U, 0x00U); /* PCM B volume */

    /* Power up DAC and output stages. */
    CS43L22_WRITE_REG(0x02U, 0x9EU);
    HAL_Delay(100U);

#undef CS43L22_WRITE_REG
    return HAL_OK;
}

/* System Clock Configuration -----------------------------------------------*/
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 8;
    RCC_OscInitStruct.PLL.PLLN = 336;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = 7;

    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    /* I2S PLL settings; verify the exact sample-rate divider on your clock tree. */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_I2S;
    PeriphClkInitStruct.PLLI2S.PLLI2SN = 192;
    PeriphClkInitStruct.PLLI2S.PLLI2SR = 2;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK |
                                  RCC_CLOCKTYPE_SYSCLK |
                                  RCC_CLOCKTYPE_PCLK1 |
                                  RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
    {
        Error_Handler();
    }
}

/* I2C1 Initialization -------------------------------------------------------*/
static void MX_I2C1_Init(void)
{
    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 100000;
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.OwnAddress2 = 0;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&hi2c1) != HAL_OK)
    {
        Error_Handler();
    }
}

/* I2S3 Initialization -------------------------------------------------------*/
static void MX_I2S3_Init(void)
{
    hi2s3.Instance = SPI3;
    hi2s3.Init.Mode = I2S_MODE_MASTER_TX;
    hi2s3.Init.Standard = I2S_STANDARD_PHILIPS;
    hi2s3.Init.DataFormat = I2S_DATAFORMAT_16B;
    hi2s3.Init.MCLKOutput = I2S_MCLKOUTPUT_ENABLE;
    hi2s3.Init.AudioFreq = I2S_AUDIOFREQ_48K;
    hi2s3.Init.CPOL = I2S_CPOL_LOW;
    hi2s3.Init.ClockSource = I2S_CLOCK_PLL;
    hi2s3.Init.FullDuplexMode = I2S_FULLDUPLEXMODE_DISABLE;

    if (HAL_I2S_Init(&hi2s3) != HAL_OK)
    {
        Error_Handler();
    }
}

/* SPI1 Initialization (retained from your original project) ----------------*/
static void MX_SPI1_Init(void)
{
    hspi1.Instance = SPI1;
    hspi1.Init.Mode = SPI_MODE_MASTER;
    hspi1.Init.Direction = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi1.Init.NSS = SPI_NSS_SOFT;
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial = 10;

    if (HAL_SPI_Init(&hspi1) != HAL_OK)
    {
        Error_Handler();
    }
}

/* GPIO Initialization -------------------------------------------------------*/
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    HAL_GPIO_WritePin(CS_I2C_SPI_GPIO_Port, CS_I2C_SPI_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(OTG_FS_PowerSwitchOn_GPIO_Port,
                      OTG_FS_PowerSwitchOn_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOD,
                      LD4_Pin | LD3_Pin | LD5_Pin | LD6_Pin | Audio_RST_Pin,
                      GPIO_PIN_RESET);

    GPIO_InitStruct.Pin = CS_I2C_SPI_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(CS_I2C_SPI_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = OTG_FS_PowerSwitchOn_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(OTG_FS_PowerSwitchOn_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = PDM_OUT_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(PDM_OUT_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = B1_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_EVT_RISING;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = BOOT1_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(BOOT1_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = CLK_IN_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI2;
    HAL_GPIO_Init(CLK_IN_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = LD4_Pin | LD3_Pin | LD5_Pin | LD6_Pin | Audio_RST_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = OTG_FS_OverCurrent_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(OTG_FS_OverCurrent_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = MEMS_INT2_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_EVT_RISING;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(MEMS_INT2_GPIO_Port, &GPIO_InitStruct);

    /* CS43L22 hardware reset pin: PD4. */
    GPIO_InitStruct.Pin = GPIO_PIN_4;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_4, GPIO_PIN_RESET);
}

/* Error Handler -------------------------------------------------------------*/
void Error_Handler(void)
{
    __disable_irq();
    while (1)
    {
        /* Stay here if a peripheral initialization or I2C/I2S transfer fails. */
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
}
#endif
