#include "sedona.h"
#ifndef ESP32SIM
#include "esp32_pins.h"
#include "driver/ledc.h"
static void* owners[2];
static int pins[2];
static int timerReady;
static int writePwm(void* owner,int pin,int channel,int duty)
{
  if(channel<0 || channel>=2 || duty<0 || duty>4095)return -1;
  if(owners[channel] && owners[channel]!=owner)return -3;
  int r=sedona_pin_claim(owner,pin,1);if(r)return r;
  if(!timerReady) {
    ledc_timer_config_t cfg={.speed_mode=LEDC_LOW_SPEED_MODE,.duty_resolution=LEDC_TIMER_12_BIT,
      .timer_num=LEDC_TIMER_0,.freq_hz=10000,.clk_cfg=LEDC_AUTO_CLK};
    if(ledc_timer_config(&cfg)!=ESP_OK)goto error;
    timerReady=1;
  }
  if(!owners[channel]) {
    ledc_channel_config_t cfg={.gpio_num=pin,.speed_mode=LEDC_LOW_SPEED_MODE,
      .channel=channel,.intr_type=LEDC_INTR_DISABLE,.timer_sel=LEDC_TIMER_0,.duty=0,.hpoint=0};
    if(ledc_channel_config(&cfg)!=ESP_OK)goto error;
    owners[channel]=owner;pins[channel]=pin;
  }
  if(pins[channel]!=pin){sedona_pin_release(owner,pin);return -3;}
  if(ledc_set_duty(LEDC_LOW_SPEED_MODE,channel,duty)!=ESP_OK ||
     ledc_update_duty(LEDC_LOW_SPEED_MODE,channel)!=ESP_OK)goto error;
  return 0;
error:
  if(owners[channel]==owner){ledc_stop(LEDC_LOW_SPEED_MODE,channel,0);owners[channel]=NULL;}
  sedona_pin_release(owner,pin);return -5;
}
#endif
Cell bclimateEsp32_PwmOutput_write(SedonaVM* vm,Cell* p) {
  Cell r;
#ifdef ESP32SIM
  r.ival=-7;
#else
  r.ival=writePwm(p[0].aval,p[1].ival,p[2].ival,p[3].ival);
#endif
  return r;
}
Cell bclimateEsp32_PwmOutput_close(SedonaVM* vm,Cell* p) {
#ifndef ESP32SIM
  int ch=p[2].ival,pin=p[1].ival;
  if(ch>=0 && ch<2 && owners[ch]==p[0].aval && pins[ch]==pin){
    ledc_stop(LEDC_LOW_SPEED_MODE,ch,0);owners[ch]=NULL;sedona_pin_release(p[0].aval,pin);
  }
#endif
  return nullCell;
}
