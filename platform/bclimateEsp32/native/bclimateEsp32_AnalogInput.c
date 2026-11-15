#include "sedona.h"
#ifndef ESP32SIM
#include "esp32_pins.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
static adc_oneshot_unit_handle_t units[2];
static adc_cali_handle_t calibration[2];
static int samples[GPIO_NUM_MAX];
static unsigned char configured[GPIO_NUM_MAX];
static int sample(void* owner,int pin)
{
  adc_unit_t unit;adc_channel_t channel;
  if(adc_oneshot_io_to_channel(pin,&unit,&channel)!=ESP_OK)return -1;
  int r=sedona_pin_claim(owner,pin,0);if(r)return r;
  if(!units[unit]) {
    adc_oneshot_unit_init_cfg_t cfg={.unit_id=unit,.ulp_mode=ADC_ULP_MODE_DISABLE};
    if(adc_oneshot_new_unit(&cfg,&units[unit])!=ESP_OK)goto error;
  }
  if(!calibration[unit]) {
    adc_cali_curve_fitting_config_t cfg={.unit_id=unit,.atten=ADC_ATTEN_DB_12,.bitwidth=ADC_BITWIDTH_DEFAULT};
    if(adc_cali_create_scheme_curve_fitting(&cfg,&calibration[unit])!=ESP_OK){sedona_pin_release(owner,pin);return -10;}
  }
  if(!configured[pin]) {
    adc_oneshot_chan_cfg_t cfg={.atten=ADC_ATTEN_DB_12,.bitwidth=ADC_BITWIDTH_DEFAULT};
    if(adc_oneshot_config_channel(units[unit],channel,&cfg)!=ESP_OK)goto error;
    configured[pin]=1;
  }
  int raw,mv;
  if(adc_oneshot_read(units[unit],channel,&raw)!=ESP_OK ||
     adc_cali_raw_to_voltage(calibration[unit],raw,&mv)!=ESP_OK)goto error;
  samples[pin]=raw;return mv;
error:
  configured[pin]=0;sedona_pin_release(owner,pin);return -5;
}
#endif
Cell bclimateEsp32_AnalogInput_sample(SedonaVM* vm,Cell* p) {
  Cell r;
#ifdef ESP32SIM
  r.ival=-7;
#else
  r.ival=sample(p[0].aval,p[1].ival);
#endif
  return r;
}
Cell bclimateEsp32_AnalogInput_lastRaw(SedonaVM* vm,Cell* p) {
  Cell r;
#ifdef ESP32SIM
  r.ival=-7;
#else
  int pin=p[1].ival;r.ival=sedona_pin_owned(p[0].aval,pin)?samples[pin]:-4;
#endif
  return r;
}
Cell bclimateEsp32_AnalogInput_close(SedonaVM* vm,Cell* p) {
#ifndef ESP32SIM
  int pin=p[1].ival;
  if(sedona_pin_owned(p[0].aval,pin)){configured[pin]=0;sedona_pin_release(p[0].aval,pin);}
#endif
  return nullCell;
}
