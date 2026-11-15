#include "sedona.h"
#ifndef ESP32SIM
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <sys/time.h>
static struct {char url[256],cert[2048];int phase,percent,fault;} ota;
static portMUX_TYPE otaLock=portMUX_INITIALIZER_UNLOCKED;
static void otaState(int phase,int percent,int fault)
{portENTER_CRITICAL(&otaLock);ota.phase=phase;ota.percent=percent;ota.fault=fault;portEXIT_CRITICAL(&otaLock);}
static void download(void* arg)
{
  esp_http_client_config_t http={.url=ota.url,.cert_pem=ota.cert[0]?ota.cert:NULL,
    .crt_bundle_attach=ota.cert[0]?NULL:esp_crt_bundle_attach,
    .timeout_ms=15000,.buffer_size=4096,.keep_alive_enable=true};
  esp_https_ota_config_t cfg={.http_config=&http};
  esp_https_ota_handle_t handle=NULL;
  esp_err_t r=esp_https_ota_begin(&cfg,&handle);
  if(r!=ESP_OK)goto error;
  do {
    r=esp_https_ota_perform(handle);
    int size=esp_https_ota_get_image_size(handle),read=esp_https_ota_get_image_len_read(handle);
    otaState(1,size>0?(int)((int64_t)read*100/size):0,0);
    vTaskDelay(1);
  } while(r==ESP_ERR_HTTPS_OTA_IN_PROGRESS);
  if(r!=ESP_OK || !esp_https_ota_is_complete_data_received(handle))goto error;
  r=esp_https_ota_finish(handle);handle=NULL;
  if(r!=ESP_OK)goto error;
  otaState(2,100,0);vTaskDelay(pdMS_TO_TICKS(200));esp_restart();
error:
  if(handle)esp_https_ota_abort(handle);
  otaState(3,0,-5);vTaskDelete(NULL);
}
#endif
Cell bclimateEsp32_OtaService_begin(SedonaVM* vm,Cell* p){
  Cell r;r.ival=-7;
#ifndef ESP32SIM
  const char* url=p[0].aval;const char* ca=p[1].aval;
  if(!url || !ca || strnlen(url,256)>=256 || strnlen(ca,2048)>=2048 || strncmp(url,"https://",8)){r.ival=-1;return r;}
  if(!esp_ota_get_next_update_partition(NULL)){r.ival=-14;return r;}
  const esp_partition_t* running=esp_ota_get_running_partition();esp_ota_img_states_t state;
  if(esp_ota_get_state_partition(running,&state)==ESP_OK && state==ESP_OTA_IMG_PENDING_VERIFY){r.ival=-9;return r;}
  portENTER_CRITICAL(&otaLock);
  if(ota.phase==1 || ota.phase==2){portEXIT_CRITICAL(&otaLock);r.ival=-9;return r;}
  strcpy(ota.url,url);strcpy(ota.cert,ca);ota.phase=1;ota.percent=0;ota.fault=0;
  portEXIT_CRITICAL(&otaLock);
  if(xTaskCreate(download,"sedona_ota",8192,NULL,3,NULL)!=pdPASS){otaState(3,0,-5);r.ival=-5;return r;}
  r.ival=0;
#endif
  return r;
}
Cell bclimateEsp32_OtaService_status(SedonaVM* vm,Cell* p){
  Cell r;r.ival=-7;
#ifndef ESP32SIM
  portENTER_CRITICAL(&otaLock);r.ival=p[0].ival==0?ota.phase:p[0].ival==1?ota.percent:ota.fault;portEXIT_CRITICAL(&otaLock);
#endif
  return r;
}

Cell bclimateEsp32_OtaService_setTime(SedonaVM* vm,Cell* p){
  Cell r;r.ival=-7;
#ifndef ESP32SIM
  int64_t seconds;memcpy(&seconds,p,sizeof(seconds));
  if(seconds<1704067200LL || seconds>4102444800LL){r.ival=-1;return r;}
  struct timeval tv={.tv_sec=seconds,.tv_usec=0};r.ival=settimeofday(&tv,NULL)==0?0:-5;
#endif
  return r;
}
