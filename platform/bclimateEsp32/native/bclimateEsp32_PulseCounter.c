#include "sedona.h"
#ifndef ESP32SIM
#include "esp32_pins.h"
#include "driver/pulse_cnt.h"
static pcnt_unit_handle_t counter;
static pcnt_channel_handle_t input;
static void* owner;
static int activePin=-1;
static uint32_t previous;
static int64_t total;
static void closeCounter(void* who)
{
  if(owner!=who)return;
  if(counter){pcnt_unit_stop(counter);pcnt_unit_disable(counter);}
  if(input){pcnt_del_channel(input);input=NULL;}
  if(counter){pcnt_del_unit(counter);counter=NULL;}
  sedona_pin_release(owner,activePin);owner=NULL;activePin=-1;
}
static int openCounter(void* who,int pin)
{
  if(owner && owner!=who)return -3;
  if(owner==who && counter && pin==activePin)return 0;
  if(owner==who)closeCounter(who);
  int r=sedona_pin_claim(who,pin,0);if(r)return r;
  owner=who;activePin=pin;previous=0;total=0;
  pcnt_unit_config_t cfg={.low_limit=-30000,.high_limit=30000,.flags.accum_count=1};
  if(pcnt_new_unit(&cfg,&counter)!=ESP_OK)goto error;
  pcnt_chan_config_t ch={.edge_gpio_num=pin,.level_gpio_num=-1};
  if(pcnt_new_channel(counter,&ch,&input)!=ESP_OK)goto error;
  if(pcnt_channel_set_edge_action(input,PCNT_CHANNEL_EDGE_ACTION_INCREASE,PCNT_CHANNEL_EDGE_ACTION_HOLD)!=ESP_OK)goto error;
  pcnt_glitch_filter_config_t filter={.max_glitch_ns=1000};
  if(pcnt_unit_set_glitch_filter(counter,&filter)!=ESP_OK ||
     pcnt_unit_add_watch_point(counter,-30000)!=ESP_OK || pcnt_unit_add_watch_point(counter,30000)!=ESP_OK ||
     pcnt_unit_enable(counter)!=ESP_OK || pcnt_unit_clear_count(counter)!=ESP_OK ||
     pcnt_unit_start(counter)!=ESP_OK)goto error;
  return 0;
error:closeCounter(who);return -5;
}
#endif
Cell bclimateEsp32_PulseCounter_open(SedonaVM* vm,Cell* p) {
  Cell r;
#ifdef ESP32SIM
  r.ival=-7;
#else
  r.ival=openCounter(p[0].aval,p[1].ival);
#endif
  return r;
}
int64_t bclimateEsp32_PulseCounter_read(SedonaVM* vm,Cell* p) {
#ifdef ESP32SIM
  return -7;
#else
  int count;
  if(owner!=p[0].aval || !counter)return -4;
  if(pcnt_unit_get_count(counter,&count)!=ESP_OK)return -5;
  uint32_t now=(uint32_t)count;total+=(uint32_t)(now-previous);previous=now;
  return total;
#endif
}
Cell bclimateEsp32_PulseCounter_clear(SedonaVM* vm,Cell* p) {
  Cell r;
#ifdef ESP32SIM
  r.ival=-7;
#else
  r.ival=owner!=p[0].aval || !counter?-4:pcnt_unit_clear_count(counter)==ESP_OK?0:-5;
  if(r.ival==0){previous=0;total=0;}
#endif
  return r;
}
Cell bclimateEsp32_PulseCounter_close(SedonaVM* vm,Cell* p) {
#ifndef ESP32SIM
  closeCounter(p[0].aval);
#endif
  return nullCell;
}
