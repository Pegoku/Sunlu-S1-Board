/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32c0xx_hal_gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <math.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

SPI_HandleTypeDef hspi1;

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim17;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_TIM1_Init(void);
static void MX_TIM17_Init(void);
static void MX_SPI1_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

// NTC parameters (typical 100K NTC thermistor)
#define THERMISTOR_NOMINAL 100000.0f  // Resistance at 25°C
#define TEMPERATURE_NOMINAL 25.0f      // Temperature for nominal resistance
#define B_COEFFICIENT 3950.0f          // Beta coefficient
#define SERIES_RESISTOR 100000.0f      // Value of the series resistor
#define ADC_MAX_VALUE 4095.0f          // 12-bit ADC
#define VREF 3.3f                       // Reference voltage

// Font for '0'-'9' and '.' (5x7 pixels)
static const uint8_t Font5x7[11][5] = {
    {0x3E, 0x45, 0x49, 0x51, 0x3E}, // 0
    {0x00, 0x21, 0x7F, 0x01, 0x00}, // 1
    {0x21, 0x43, 0x45, 0x49, 0x31}, // 2
    {0x42, 0x41, 0x51, 0x69, 0x46}, // 3
    {0x0C, 0x14, 0x24, 0x7F, 0x04}, // 4
    {0x72, 0x51, 0x51, 0x51, 0x4E}, // 5
    {0x1E, 0x29, 0x49, 0x49, 0x06}, // 6
    {0x40, 0x47, 0x48, 0x50, 0x60}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x30, 0x49, 0x49, 0x4A, 0x3C}, // 9
    {0x00, 0x00, 0x03, 0x00, 0x00}  // . (decimal point)
};



static void ST7735_Select(void)
{
  HAL_GPIO_WritePin(ST7735_CS_GPIO_Port, ST7735_CS_Pin, GPIO_PIN_RESET);
}

static void ST7735_Unselect(void)
{
  HAL_GPIO_WritePin(ST7735_CS_GPIO_Port, ST7735_CS_Pin, GPIO_PIN_SET);
}

static void ST7735_Write(uint8_t data, uint8_t is_data)
{
  HAL_GPIO_WritePin(ST7735_DC_GPIO_Port, ST7735_DC_Pin,
                    is_data ? GPIO_PIN_SET : GPIO_PIN_RESET);

  HAL_SPI_Transmit(&hspi1, &data, 1, HAL_MAX_DELAY);
}

static void ST7735_SetWindow(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
  ST7735_Write(0x2A, 0); // Column Address Set
  ST7735_Write(0x00, 1);
  ST7735_Write(x0, 1);
  ST7735_Write(0x00, 1);
  ST7735_Write(x1, 1);
  ST7735_Write(0x2B, 0); // Row Address Set
  ST7735_Write(0x00, 1);
  ST7735_Write(y0, 1);
  ST7735_Write(0x00, 1);
  ST7735_Write(y1, 1);
  ST7735_Write(0x2C, 0); // RAM Write
}

static void DrawDigit(uint8_t x, uint8_t y, char digit, uint16_t color)
{
    uint8_t index;
    if (digit == '.')
    {
        index = 10;  // Decimal point is at index 10
    }
    else
    {
        index = (uint8_t)(digit - '0');
        if (index > 9) return;  // Invalid character
    }

    ST7735_Select();
    ST7735_SetWindow(x, y, (uint8_t)(x + 4), (uint8_t)(y + 6));

    // ST7735 expects pixels in row-major order: left->right, then next row.
    for (int row = 0; row < 7; row++)
    {
        for (int col = 0; col < 5; col++)
        {
            uint8_t line = Font5x7[index][col];          // column byte
            uint16_t p = (line & (1U << (6 - row))) ? color : 0x0000U;

            ST7735_Write((uint8_t)(p >> 8), 1);
            ST7735_Write((uint8_t)(p & 0xFF), 1);
        }
    }

    ST7735_Unselect();
}

static void DrawString(uint8_t x, uint8_t y, const char *str, uint16_t color)
{
    uint8_t offset = 0;
    while (*str != '\0')
    {
        DrawDigit((uint8_t)(x + offset), y, *str, color);
        str++;
        offset += 7;  // 5 pixels + 2 spacing
    }
}

static float ReadNTC(uint32_t adc_channel)
{
    ADC_ChannelConfTypeDef sConfig = {0};
    
    // Configure the ADC channel
    sConfig.Channel = adc_channel;
    sConfig.Rank = ADC_RANK_CHANNEL_NUMBER;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
    {
        Error_Handler();
    }
    
    // Start ADC conversion
    HAL_ADC_Start(&hadc1);
    
    // Wait for conversion to complete
    if (HAL_ADC_PollForConversion(&hadc1, 100) == HAL_OK)
    {
        uint32_t adc_value = HAL_ADC_GetValue(&hadc1);
        HAL_ADC_Stop(&hadc1);
        
        // Calculate resistance
        float voltage = ((float)adc_value / ADC_MAX_VALUE) * VREF;
        float resistance = SERIES_RESISTOR * voltage / (VREF - voltage);
        
        // Steinhart-Hart equation (simplified Beta parameter equation)
        float steinhart;
        steinhart = resistance / THERMISTOR_NOMINAL;           // (R/Ro)
        steinhart = logf(steinhart);                           // ln(R/Ro)
        steinhart /= B_COEFFICIENT;                            // 1/B * ln(R/Ro)
        steinhart += 1.0f / (TEMPERATURE_NOMINAL + 273.15f);   // + (1/To)
        steinhart = 1.0f / steinhart;                          // Invert
        steinhart -= 273.15f;                                  // Convert to Celsius
        
        return steinhart;
    }
    
    HAL_ADC_Stop(&hadc1);
    return -999.0f; // Error value
}

static void FloatToString(float value, char *buffer, uint8_t decimals)
{
    int32_t int_part = (int32_t)value;
    float frac_part = value - (float)int_part;
    
    // Handle negative numbers
    uint8_t idx = 0;
    if (int_part < 0)
    {
        buffer[idx++] = '-';
        int_part = -int_part;
        frac_part = -frac_part;
    }
    
    // Simple conversion for 0-99
    if (int_part >= 10)
    {
        buffer[idx++] = '0' + (char)(int_part / 10);
    }
    buffer[idx++] = '0' + (char)(int_part % 10);
    
    // Add decimal point and fractional part
    if (decimals > 0)
    {
        buffer[idx++] = '.';
        int32_t digit = (int32_t)(frac_part * 10.0f);
        buffer[idx++] = '0' + (char)(digit % 10);
    }
    
    buffer[idx] = '\0';
}


static void ST7735_Init(void)
{
  HAL_Delay(10);
  ST7735_Select();

  ST7735_Write(0x01, 0); // SW Reset
  HAL_Delay(150);
  ST7735_Write(0x11, 0); // Sleep Out
  HAL_Delay(150);

  ST7735_Write(0x3A, 0); // COLMOD
  ST7735_Write(0x05, 1); // 16-bit color

  ST7735_Write(0x36, 0); // MADCTL
  ST7735_Write(0x00, 1); // Row/Column order

  ST7735_Write(0x21, 0); // INVON (many ST7735 need inversion)

  ST7735_Write(0x29, 0); // Display On
  HAL_Delay(10);

  ST7735_Unselect();
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_TIM1_Init();
  MX_TIM17_Init();
  MX_SPI1_Init();
  /* USER CODE BEGIN 2 */

  ST7735_Init();

  HAL_GPIO_WritePin(BL_GPIO_Port, BL_Pin, GPIO_PIN_SET);

  // Clear screen to black
  ST7735_Select();
  ST7735_SetWindow(0, 0, 127, 159);
  for (uint32_t i = 0; i < 128 * 160; i++)
  {
    ST7735_Write(0x00, 1);
    ST7735_Write(0x00, 1);
  }
  ST7735_Unselect();

  // Display labels
  DrawString(10, 20, "Air", 0xFFFF);   // White text
  DrawString(10, 50, "Heat", 0xFFFF);  // White text
  
  // Start PWM for heater control (TIM1 Channel 1 on PA8)
  HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    // Read button 1 state (PA4 - b1_Pin)
    GPIO_PinState button1_state = HAL_GPIO_ReadPin(b1_GPIO_Port, b1_Pin);
    
    // Control heater based on button state
    // Button is active LOW (pressed = GPIO_PIN_RESET with pull-up)
    if (button1_state == GPIO_PIN_RESET)
    {
        // Button pressed - enable heater at full power
        __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 65535);  // Full duty cycle
        HAL_GPIO_WritePin(led_GPIO_Port, led_Pin, GPIO_PIN_SET);  // Turn on LED
    }
    else
    {
        // Button released - disable heater
        __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);  // 0% duty cycle
        HAL_GPIO_WritePin(led_GPIO_Port, led_Pin, GPIO_PIN_RESET);  // Turn off LED
    }
    
    // Read NTC temperatures
    float air_temp = ReadNTC(ADC_CHANNEL_11);   // airNTC
    float heat_temp = ReadNTC(ADC_CHANNEL_12);  // heatNTC
    
    // Convert to strings
    char air_str[16];
    char heat_str[16];
    FloatToString(air_temp, air_str, 1);
    FloatToString(heat_temp, heat_str, 1);
    
    // Clear temperature display areas (overwrite with black background)
    ST7735_Select();
    ST7735_SetWindow(50, 20, 120, 30);
    for (uint32_t i = 0; i < 71 * 11; i++)
    {
        ST7735_Write(0x00, 1);
        ST7735_Write(0x00, 1);
    }
    ST7735_SetWindow(50, 50, 120, 60);
    for (uint32_t i = 0; i < 71 * 11; i++)
    {
        ST7735_Write(0x00, 1);
        ST7735_Write(0x00, 1);
    }
    ST7735_Unselect();
    
    // Display temperatures (in cyan/green color)
    DrawString(50, 20, air_str, 0x07FF);   // Cyan
    DrawString(50, 50, heat_str, 0x07E0);  // Green

    HAL_Delay(500);

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  __HAL_FLASH_SET_LATENCY(FLASH_LATENCY_0);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSIDiv = RCC_HSI_DIV4;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV1;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode = ADC_SCAN_SEQ_FIXED;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait = DISABLE;
  hadc1.Init.LowPowerAutoPowerOff = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.Overrun = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.SamplingTimeCommon1 = ADC_SAMPLETIME_1CYCLE_5;
  hadc1.Init.OversamplingMode = DISABLE;
  hadc1.Init.TriggerFrequencyMode = ADC_TRIGGER_FREQ_HIGH;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_11;
  sConfig.Rank = ADC_RANK_CHANNEL_NUMBER;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_12;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_1LINE;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 7;
  hspi1.Init.CRCLength = SPI_CRC_LENGTH_DATASIZE;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 0;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 65535;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterOutputTrigger2 = TIM_TRGO2_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.BreakFilter = 0;
  sBreakDeadTimeConfig.BreakAFMode = TIM_BREAK_AFMODE_INPUT;
  sBreakDeadTimeConfig.Break2State = TIM_BREAK2_DISABLE;
  sBreakDeadTimeConfig.Break2Polarity = TIM_BREAK2POLARITY_HIGH;
  sBreakDeadTimeConfig.Break2Filter = 0;
  sBreakDeadTimeConfig.Break2AFMode = TIM_BREAK_AFMODE_INPUT;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */
  HAL_TIM_MspPostInit(&htim1);

}

/**
  * @brief TIM17 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM17_Init(void)
{

  /* USER CODE BEGIN TIM17_Init 0 */

  /* USER CODE END TIM17_Init 0 */

  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM17_Init 1 */

  /* USER CODE END TIM17_Init 1 */
  htim17.Instance = TIM17;
  htim17.Init.Prescaler = 0;
  htim17.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim17.Init.Period = 65535;
  htim17.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim17.Init.RepetitionCounter = 0;
  htim17.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim17) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim17) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim17, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.BreakFilter = 0;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim17, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM17_Init 2 */

  /* USER CODE END TIM17_Init 2 */
  HAL_TIM_MspPostInit(&htim17);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, led_Pin|BL_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : led_Pin BL_Pin */
  GPIO_InitStruct.Pin = led_Pin|BL_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : b1_Pin b2_Pin */
  GPIO_InitStruct.Pin = b1_Pin|b2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
