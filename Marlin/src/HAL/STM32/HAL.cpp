/**
 * Marlin 3D Printer Firmware
 *
 * Copyright (c) 2020 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
 * Copyright (c) 2016 Bob Cousins bobcousins42@googlemail.com
 * Copyright (c) 2015-2016 Nico Tonnhofer wurstnase.reprap@gmail.com
 * Copyright (c) 2017 Victor Perez
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */
#if defined(ARDUINO_ARCH_STM32) && !defined(STM32GENERIC)

#include "HAL.h"
#include "usb_serial.h"

#include "../../inc/MarlinConfig.h"
#include "../shared/Delay.h"

#if ENABLED(SRAM_EEPROM_EMULATION)
  #if STM32F7xx
    #include <stm32f7xx_ll_pwr.h>
  #elif STM32F4xx
    #include <stm32f4xx_ll_pwr.h>
  #else
    #error "SRAM_EEPROM_EMULATION is currently only supported for STM32F4xx and STM32F7xx"
  #endif
#endif

// ------------------------
// Public Variables
// ------------------------

uint16_t HAL_adc_result;

// ------------------------
// Public functions
// ------------------------

// Needed for DELAY_NS() / DELAY_US() on CORTEX-M7
#if (defined(__arm__) || defined(__thumb__)) && __CORTEX_M == 7
  // HAL pre-initialization task
  // Force the preinit function to run between the premain() and main() function
  // of the STM32 arduino core
  __attribute__((constructor (102)))
  void HAL_preinit() {
    enableCycleCounter();
  }
#endif

// HAL initialization task
void HAL_init() {
  FastIO_init();

  #if ENABLED(SDSUPPORT) && DISABLED(SDIO_SUPPORT) && (defined(SDSS) && SDSS != -1)
    OUT_WRITE(SDSS, HIGH); // Try to set SDSS inactive before any other SPI users start up
  #endif

  #if PIN_EXISTS(LED)
    OUT_WRITE(LED_PIN, LOW);
  #endif

  #if PIN_EXISTS(AUTO_LEVEL_TX)
    OUT_WRITE(AUTO_LEVEL_TX_PIN, HIGH);
  #endif

  #if ENABLED(SRAM_EEPROM_EMULATION)
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWR_EnableBkUpAccess();           // Enable access to backup SRAM
    __HAL_RCC_BKPSRAM_CLK_ENABLE();
    LL_PWR_EnableBkUpRegulator();         // Enable backup regulator
    while (!LL_PWR_IsActiveFlag_BRR());   // Wait until backup regulator is initialized
  #endif

  SetTimerInterruptPriorities();

  #if ENABLED(EMERGENCY_PARSER) && USBD_USE_CDC
    USB_Hook_init();
  #endif
}

void HAL_clear_reset_source() { __HAL_RCC_CLEAR_RESET_FLAGS(); }

uint8_t HAL_get_reset_source() {
  return
    #ifdef RCC_FLAG_IWDGRST // Some sources may not exist...
      RESET != __HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST)  ? RST_WATCHDOG :
    #endif
    #ifdef RCC_FLAG_IWDG1RST
      RESET != __HAL_RCC_GET_FLAG(RCC_FLAG_IWDG1RST) ? RST_WATCHDOG :
    #endif
    #ifdef RCC_FLAG_IWDG2RST
      RESET != __HAL_RCC_GET_FLAG(RCC_FLAG_IWDG2RST) ? RST_WATCHDOG :
    #endif
    #ifdef RCC_FLAG_SFTRST
      RESET != __HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST)   ? RST_SOFTWARE :
    #endif
    #ifdef RCC_FLAG_PINRST
      RESET != __HAL_RCC_GET_FLAG(RCC_FLAG_PINRST)   ? RST_EXTERNAL :
    #endif
    #ifdef RCC_FLAG_PORRST
      RESET != __HAL_RCC_GET_FLAG(RCC_FLAG_PORRST)   ? RST_POWER_ON :
    #endif
    0
  ;
}

void _delay_ms(const int delay_ms) { delay(delay_ms); }

extern "C" {
  extern unsigned int _ebss; // end of bss section
}

// ------------------------
// ADC
// ------------------------
ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

uint32_t AD_DMA[3];
void HAL_adc_init()
{
  ADC_ChannelConfTypeDef sConfig = {0};
  hadc1.Instance = ADC1;
  hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
  hadc1.Init.ContinuousConvMode = ENABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 3;
  if (HAL_ADC_DeInit(&hadc1) != HAL_OK) {
    SERIAL_ECHOLNPAIR("HAL_ADC_DeInit failed @ line: ", __LINE__);
  }
  if (HAL_ADC_Init(&hadc1) != HAL_OK) {
    SERIAL_ECHOLNPAIR("HAL_ADC_Init failed @ line: ", __LINE__);
  }
  sConfig.Channel = ADC_CHANNEL_14;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    SERIAL_ECHOLNPAIR("HAL_ADC_ConfigChannel failed @ line: ", __LINE__);
  }
  sConfig.Channel = ADC_CHANNEL_8;
  sConfig.Rank = ADC_REGULAR_RANK_2;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    SERIAL_ECHOLNPAIR("HAL_ADC_ConfigChannel failed @ line: ", __LINE__);
  }
  sConfig.Channel = ADC_CHANNEL_6;
  sConfig.Rank = ADC_REGULAR_RANK_3;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    SERIAL_ECHOLNPAIR("HAL_ADC_ConfigChannel failed @ line: ", __LINE__);
  }
  HAL_ADCEx_Calibration_Start(&hadc1);
  HAL_ADC_Start_DMA(&hadc1, (uint32_t *)&AD_DMA, 3);
}
// TODO: Make sure this doesn't cause any delay
void HAL_adc_start_conversion(const uint8_t adc_pin) {} // { HAL_adc_result = analogRead(adc_pin); }
uint16_t HAL_adc_get_result() {return 1000;} // { return HAL_adc_result; }

// Reset the system (to initiate a firmware flash)
void flashFirmware(const int16_t) { NVIC_SystemReset(); }

// Maple Compatibility
volatile uint32_t systick_uptime_millis = 0;
systickCallback_t systick_user_callback;
void systick_attach_callback(systickCallback_t cb) { systick_user_callback = cb; }
void HAL_SYSTICK_Callback() {
  systick_uptime_millis++;
  if (systick_user_callback) systick_user_callback();
}

#endif // ARDUINO_ARCH_STM32 && !STM32GENERIC
