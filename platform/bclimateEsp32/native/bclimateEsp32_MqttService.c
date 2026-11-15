#include "sedona.h"
#ifndef ESP32SIM
#include "mqtt_client.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
typedef struct {char topic[128];unsigned char payload[256];int topicLen,len;} Message;
static struct {
  void* owner;esp_mqtt_client_handle_t client;int closing,connected,fault,qos,dropped,lastTopic;
  char uri[192],clientId[64],user[64],password[128],filter[128];
  Message queue[8],partial;int head,tail,count,partialId,partialBytes;
} ctx;
static portMUX_TYPE mqttLock=portMUX_INITIALIZER_UNLOCKED;
static void event(void* arg,esp_event_base_t base,int32_t id,void* data)
{
  esp_mqtt_event_handle_t e=data;
  if(id==MQTT_EVENT_CONNECTED){
    portENTER_CRITICAL(&mqttLock);ctx.connected=1;ctx.fault=0;portEXIT_CRITICAL(&mqttLock);
    if(ctx.filter[0])esp_mqtt_client_subscribe(ctx.client,ctx.filter,ctx.qos);
  } else if(id==MQTT_EVENT_DISCONNECTED || id==MQTT_EVENT_ERROR){
    portENTER_CRITICAL(&mqttLock);ctx.connected=0;ctx.fault=-12;portEXIT_CRITICAL(&mqttLock);
  } else if(id==MQTT_EVENT_DATA){
    if(e->current_data_offset==0){
      ctx.partialBytes=0;ctx.partialId=e->msg_id;
      ctx.partial.len=e->total_data_len;ctx.partial.topicLen=e->topic_len;
      if(e->topic_len>0 && e->topic_len<128)memcpy(ctx.partial.topic,e->topic,e->topic_len);
    }
    if(ctx.partial.len>256 || ctx.partial.topicLen<1 || ctx.partial.topicLen>=128 ||
       ctx.partialId!=e->msg_id || ctx.partialBytes!=e->current_data_offset ||
       e->data_len<0 || e->data_len>256-ctx.partialBytes){
      portENTER_CRITICAL(&mqttLock);ctx.dropped++;portEXIT_CRITICAL(&mqttLock);return;
    }
    memcpy(ctx.partial.payload+ctx.partialBytes,e->data,e->data_len);ctx.partialBytes+=e->data_len;
    if(ctx.partialBytes==ctx.partial.len){
      ctx.partial.topic[ctx.partial.topicLen]=0;
      portENTER_CRITICAL(&mqttLock);
      if(ctx.count==8)ctx.dropped++;
      else {ctx.queue[ctx.tail]=ctx.partial;ctx.tail=(ctx.tail+1)%8;ctx.count++;}
      portEXIT_CRITICAL(&mqttLock);
    }
  }
}
static void destroy(void* arg)
{
  esp_mqtt_client_stop(ctx.client);esp_mqtt_client_destroy(ctx.client);
  portENTER_CRITICAL(&mqttLock);memset(&ctx,0,sizeof(ctx));portEXIT_CRITICAL(&mqttLock);
  vTaskDelete(NULL);
}
static int fits(const char* text,int capacity){return text && strnlen(text,capacity)<capacity;}
#endif
Cell bclimateEsp32_MqttService_open(SedonaVM* vm,Cell* p){
  Cell r;r.ival=-7;
#ifndef ESP32SIM
  if(ctx.owner){r.ival=ctx.owner==p[0].aval && !ctx.closing?0:-3;return r;}
  const char* uri=p[1].aval;int qos=p[6].ival;
  if(!p[0].aval || !fits(uri,192) ||
     (strncmp(uri,"mqtt://",7) && strncmp(uri,"mqtts://",8)) ||
     !fits(p[2].aval,64) || !fits(p[3].aval,64) || !fits(p[4].aval,128) || !fits(p[5].aval,128) || qos<0 || qos>1){r.ival=-1;return r;}
  ctx.owner=p[0].aval;ctx.qos=qos;
  strcpy(ctx.uri,uri);strcpy(ctx.clientId,p[2].aval);strcpy(ctx.user,p[3].aval);
  strcpy(ctx.password,p[4].aval);strcpy(ctx.filter,p[5].aval);
  esp_mqtt_client_config_t cfg={
    .broker.address.uri=ctx.uri,.broker.verification.crt_bundle_attach=esp_crt_bundle_attach,
    .credentials.client_id=ctx.clientId[0]?ctx.clientId:NULL,
    .credentials.username=ctx.user[0]?ctx.user:NULL,
    .credentials.authentication.password=ctx.password[0]?ctx.password:NULL,
    .session.keepalive=30,.network.reconnect_timeout_ms=5000,
    .buffer.size=512,.buffer.out_size=512,
    .outbox.limit=4096,
  };
  ctx.client=esp_mqtt_client_init(&cfg);
  if(!ctx.client){memset(&ctx,0,sizeof(ctx));r.ival=-5;return r;}
  if(esp_mqtt_client_register_event(ctx.client,ESP_EVENT_ANY_ID,event,NULL)!=ESP_OK ||
     esp_mqtt_client_start(ctx.client)!=ESP_OK){esp_mqtt_client_destroy(ctx.client);memset(&ctx,0,sizeof(ctx));r.ival=-5;return r;}
  r.ival=0;
#endif
  return r;
}
Cell bclimateEsp32_MqttService_status(SedonaVM* vm,Cell* p){
  Cell r;r.ival=-7;
#ifndef ESP32SIM
  portENTER_CRITICAL(&mqttLock);
  if(ctx.owner!=p[0].aval || ctx.closing)r.ival=-4;
  else switch(p[1].ival){case 0:r.ival=ctx.connected;break;case 1:r.ival=ctx.dropped;break;case 2:r.ival=ctx.lastTopic;break;default:r.ival=ctx.fault;}
  portEXIT_CRITICAL(&mqttLock);
#endif
  return r;
}
Cell bclimateEsp32_MqttService_poll(SedonaVM* vm,Cell* p){
  Cell r;r.ival=-7;
#ifndef ESP32SIM
  if(!p[1].aval || !p[3].aval || p[2].ival<128 || p[4].ival<256){r.ival=-1;return r;}
  portENTER_CRITICAL(&mqttLock);
  if(ctx.owner!=p[0].aval || ctx.closing)r.ival=-4;
  else if(!ctx.count)r.ival=-9;
  else {Message* m=&ctx.queue[ctx.head];memcpy(p[1].aval,m->topic,m->topicLen+1);
    memcpy(p[3].aval,m->payload,m->len);ctx.lastTopic=m->topicLen;r.ival=m->len;
    ctx.head=(ctx.head+1)%8;ctx.count--;}
  portEXIT_CRITICAL(&mqttLock);
#endif
  return r;
}
Cell bclimateEsp32_MqttService_send(SedonaVM* vm,Cell* p){
  Cell r;r.ival=-7;
#ifndef ESP32SIM
  int len=p[3].ival,qos=p[4].ival;
  if(ctx.owner!=p[0].aval || ctx.closing || !ctx.connected){r.ival=-4;return r;}
  if(!fits(p[1].aval,128) || !((char*)p[1].aval)[0] || len<0 || len>256 || !p[2].aval || qos<0 || qos>1){r.ival=-1;return r;}
  int id=esp_mqtt_client_enqueue(ctx.client,p[1].aval,len?p[2].aval:"",len,qos,p[5].ival!=0,true);
  r.ival=id>=0?id:-9;
#endif
  return r;
}
Cell bclimateEsp32_MqttService_close(SedonaVM* vm,Cell* p){
#ifndef ESP32SIM
  if(ctx.owner==p[0].aval && ctx.owner && !ctx.closing){
    ctx.closing=1;ctx.connected=0;
    if(xTaskCreate(destroy,"mqtt_close",4096,NULL,3,NULL)!=pdPASS){ctx.closing=0;ctx.fault=-5;}
  }
#endif
  return nullCell;
}
