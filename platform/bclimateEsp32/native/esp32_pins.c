#ifndef ESP32SIM
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "esp32_pins.h"
#include <stdlib.h>
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
static portMUX_TYPE pinsLock=portMUX_INITIALIZER_UNLOCKED;
#define LOCK() portENTER_CRITICAL(&pinsLock)
#define UNLOCK() portEXIT_CRITICAL(&pinsLock)
#else
#define LOCK()
#define UNLOCK()
#endif
static void* pinOwners[GPIO_NUM_MAX];
static int allowed(int pin)
{
  const char* p=CONFIG_SEDONA_GPIO_ALLOWED_PINS;
#if CONFIG_IDF_TARGET_ESP32P4
  /* Console and the active board's RMII cannot be claimed by application IO. */
  if(pin==37 || pin==38) return 0;
#if CONFIG_SEDONA_NET_P4_ETH
  if(pin==28 || pin==29 || pin==30 || pin==31 || pin==34 || pin==35 ||
     pin==49 || pin==50 || pin==51 || pin==52) return 0;
#endif
#endif
  while(*p) {
    char* end; long n=strtol(p,&end,10);
    if(end==p) return 0;
    if(n==pin) return 1;
    p=end;while(*p==' ')++p;
    if(*p==',')++p;else if(*p)return 0;
  }
  return 0;
}
int sedona_pin_claim(void* owner,int pin,int output)
{
  if(!owner || pin<0 || pin>=GPIO_NUM_MAX || !GPIO_IS_VALID_GPIO(pin) ||
     (output && !GPIO_IS_VALID_OUTPUT_GPIO(pin)))return -1;
  if(!allowed(pin))return -2;
  LOCK();
  if(pinOwners[pin] && pinOwners[pin]!=owner){UNLOCK();return -3;}
  pinOwners[pin]=owner;UNLOCK();return 0;
}
int sedona_pin_owned(void* owner,int pin)
{return owner && pin>=0 && pin<GPIO_NUM_MAX && pinOwners[pin]==owner;}
void sedona_pin_release(void* owner,int pin)
{LOCK();if(sedona_pin_owned(owner,pin)){gpio_reset_pin(pin);pinOwners[pin]=NULL;}UNLOCK();}
#endif
