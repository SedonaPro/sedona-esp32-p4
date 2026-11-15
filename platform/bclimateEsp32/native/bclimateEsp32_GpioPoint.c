// GPIO access is owned by Sedona components, not by pin number alone.
#include "sedona.h"
#ifndef ESP32SIM
#include "sdkconfig.h"
#include "esp32_pins.h"
#include "driver/gpio.h"
#include <stdlib.h>

static void* owners[GPIO_NUM_MAX];
static unsigned char outputMode[GPIO_NUM_MAX], pullMode[GPIO_NUM_MAX];

static int valid(int pin)
{
  return pin >= 0 && pin < GPIO_NUM_MAX && GPIO_IS_VALID_GPIO(pin);
}

static int configure(void* owner, int pin, int output, int pull, int initial)
{
  gpio_config_t cfg = {0};
  int fresh;
  if (!owner || !valid(pin) || pull < 0 || pull > 2 ||
      (output && !GPIO_IS_VALID_OUTPUT_GPIO(pin))) return -1;
  int claim = sedona_pin_claim(owner, pin, output);
  if (claim) return claim;
  if (owners[pin] && owners[pin] != owner) return -3;
  fresh = owners[pin] == NULL;
  if (!fresh && outputMode[pin] == output && pullMode[pin] == pull) return 0;
  // Output latch is set before the output driver is enabled.
  if (output && fresh && gpio_set_level(pin, initial != 0) != ESP_OK) { sedona_pin_release(owner,pin); return -5; }
  cfg.pin_bit_mask = 1ULL << pin;
  cfg.mode = output ? GPIO_MODE_INPUT_OUTPUT : GPIO_MODE_INPUT;
  cfg.pull_up_en = pull == 1 ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
  cfg.pull_down_en = pull == 2 ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
  cfg.intr_type = GPIO_INTR_DISABLE;
  if (gpio_config(&cfg) != ESP_OK) { if(fresh) sedona_pin_release(owner,pin); return -5; }
  owners[pin] = owner;
  outputMode[pin] = output;
  pullMode[pin] = pull;
  return 0;
}
#endif

Cell bclimateEsp32_GpioPoint_configure(SedonaVM* vm, Cell* p)
{
  Cell r;
#ifdef ESP32SIM
  r.ival = -7;
#else
  r.ival = configure(p[0].aval, p[1].ival, p[2].ival != 0, p[3].ival, p[4].ival);
#endif
  return r;
}

Cell bclimateEsp32_GpioPoint_readPin(SedonaVM* vm, Cell* p)
{
  Cell r;
#ifdef ESP32SIM
  r.ival = -7;
#else
  int pin = p[1].ival;
  r.ival = !valid(pin) ? -1 : !p[0].aval || owners[pin] != p[0].aval ? -4 : gpio_get_level(pin);
#endif
  return r;
}

Cell bclimateEsp32_GpioPoint_writePin(SedonaVM* vm, Cell* p)
{
  Cell r;
#ifdef ESP32SIM
  r.ival = -7;
#else
  int pin = p[1].ival;
  if (!valid(pin)) r.ival = -1;
  else if (!p[0].aval || owners[pin] != p[0].aval || !outputMode[pin]) r.ival = -4;
  else r.ival = gpio_set_level(pin, p[2].ival != 0) == ESP_OK ? 0 : -5;
#endif
  return r;
}

Cell bclimateEsp32_GpioPoint_release(SedonaVM* vm, Cell* p)
{
#ifndef ESP32SIM
  int pin = p[1].ival;
  if (valid(pin) && p[0].aval && owners[pin] == p[0].aval)
  {
    sedona_pin_release(p[0].aval,pin); // release shared ownership and reset line
    owners[pin] = NULL;
  }
#endif
  return nullCell;
}
