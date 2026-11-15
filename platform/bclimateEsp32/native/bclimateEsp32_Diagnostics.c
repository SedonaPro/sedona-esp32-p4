#include "sedona.h"
#ifndef ESP32SIM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include <string.h>
extern int64_t svm_diag_metric(int which);
static int cpu[2],status=-6;
static int64_t previousTime;
static uint64_t previousIdle[2];
static void sampleCpu(void)
{
  int64_t now=esp_timer_get_time();
  if(previousTime && now-previousTime<1000000)return;
#if CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS
  TaskStatus_t tasks[40];configRUN_TIME_COUNTER_TYPE total;
  UBaseType_t n=uxTaskGetSystemState(tasks,40,&total);
  if(!n){status=-11;return;}
  uint64_t idle[2]={0,0};int found[2]={0,0};
  for(int i=0;i<n;i++)for(int core=0;core<2;core++){
    if(tasks[i].xHandle==xTaskGetIdleTaskHandleForCore(core)){
      idle[core]=tasks[i].ulRunTimeCounter;found[core]=1;
    }
  }
  if(!found[0] || !found[1]){status=-11;return;}
  if(previousTime)for(int core=0;core<2;core++){
    int64_t busy=10000-(int64_t)((idle[core]-previousIdle[core])*10000/(now-previousTime));
    cpu[core]=busy<0?0:busy>10000?10000:busy;
  }
  previousTime=now;memcpy(previousIdle,idle,sizeof(idle));status=0;
#else
  status=-7;previousTime=now;
#endif
}
#endif
int64_t bclimateEsp32_Diagnostics_metric(SedonaVM* vm,Cell* p)
{
#ifdef ESP32SIM
  return -7;
#else
  int which=p[0].ival;
  if(which==5)return esp_timer_get_time()/1000000;
  sampleCpu();
  switch(which){
    case 0:return cpu[0];case 1:return cpu[1];
    case 2:return esp_get_free_heap_size();case 3:return esp_get_minimum_free_heap_size();
    case 4:return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    case 12:return uxTaskGetStackHighWaterMark(NULL);case 13:return status;
    default:return svm_diag_metric(which);
  }
#endif
}
