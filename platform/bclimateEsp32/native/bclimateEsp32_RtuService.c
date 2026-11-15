#include "sedona.h"
#ifndef ESP32SIM
#include "esp32_pins.h"
#include "rtu_codec.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include <string.h>
static struct {
  void* owner;TaskHandle_t task;volatile int closing,busy;
  int pins[3],pinCount,installed,baud,parity,unit,role,timeout;
  int fault,transactions,errors,fc,address,quantity,value;
  uint16_t bank[4][64];
} rtu;
static portMUX_TYPE rtuLock=portMUX_INITIALIZER_UNLOCKED;
static int get16(const uint8_t* p){return (p[0]<<8)|p[1];}
static void put16(uint8_t* p,int n){p[0]=n>>8;p[1]=n;}
static int finish(uint8_t* p,int n){uint16_t crc=sedona_rtu_crc(p,n);p[n]=crc;p[n+1]=crc>>8;return n+2;}
static int frame(uint8_t* p,int timeout)
{
  int n=uart_read_bytes(UART_NUM_1,p,1,pdMS_TO_TICKS(timeout));
  if(n!=1)return 0;
  int gap=(38500+rtu.baud-1)/rtu.baud;if(gap<2)gap=2;
  while(n<256){int got=uart_read_bytes(UART_NUM_1,p+n,256-n,pdMS_TO_TICKS(gap));if(got<=0)break;n+=got;}
  return n;
}
static int sendFrame(const uint8_t* p,int n)
{
  return uart_write_bytes(UART_NUM_1,p,n)==n &&
    uart_wait_tx_done(UART_NUM_1,pdMS_TO_TICKS(rtu.timeout))==ESP_OK;
}
static int master(void)
{
  uint8_t q[260],p[260];int n=6,fc=rtu.fc,count=rtu.quantity;
  q[0]=rtu.unit;q[1]=fc;put16(q+2,rtu.address);
  put16(q+4,fc==5||fc==6?rtu.value:count);
  if(fc==15 || fc==16){
    int bytes=fc==15?(count+7)/8:count*2;q[6]=bytes;memset(q+7,0,bytes);n=7+bytes;
    portENTER_CRITICAL(&rtuLock);
    for(int i=0;i<count;i++)if(fc==15)q[7+i/8]|=(rtu.bank[2][i]!=0)<<(i%8);
      else put16(q+7+i*2,rtu.bank[0][i]);
    portEXIT_CRITICAL(&rtuLock);
  }
  n=finish(q,n);uart_flush_input(UART_NUM_1);
  if(!sendFrame(q,n))return -5;
  int len=frame(p,rtu.timeout);
  if(!len)return -12;
  if(len<5 || p[0]!=rtu.unit || sedona_rtu_crc(p,len))return -13;
  if(p[1]==(fc|0x80) && len==5)return -20-p[2];
  if(p[1]!=fc)return -13;
  if(fc>=1 && fc<=4){
    int bytes=fc<=2?(count+7)/8:count*2;
    if(len!=5+bytes || p[2]!=bytes)return -13;
    int table=fc==3?0:fc==4?1:fc==1?2:3;
    portENTER_CRITICAL(&rtuLock);
    for(int i=0;i<count;i++)rtu.bank[table][i]=fc<=2?(p[3+i/8]>>(i%8))&1:get16(p+3+i*2);
    portEXIT_CRITICAL(&rtuLock);
  } else if(len!=8 || memcmp(q,p,6))return -13;
  return 0;
}
static void cleanup(void)
{
  if(rtu.installed)uart_driver_delete(UART_NUM_1);
  for(int i=0;i<rtu.pinCount;i++)sedona_pin_release(rtu.owner,rtu.pins[i]);
  portENTER_CRITICAL(&rtuLock);memset(&rtu,0,sizeof(rtu));portEXIT_CRITICAL(&rtuLock);
}
static void worker(void* arg)
{
  uint8_t q[260],p[260];
  while(!rtu.closing){
    if(rtu.role==0){
      ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(20));
      if(!rtu.busy || rtu.closing)continue;
      int r=master();
      portENTER_CRITICAL(&rtuLock);rtu.fault=r;rtu.transactions++;if(r)rtu.errors++;rtu.busy=0;portEXIT_CRITICAL(&rtuLock);
      vTaskDelay(pdMS_TO_TICKS(4));
    } else {
      int n=frame(q,20);if(!n)continue;
      if(n<4 || sedona_rtu_crc(q,n)){
        portENTER_CRITICAL(&rtuLock);rtu.errors++;rtu.fault=-13;portEXIT_CRITICAL(&rtuLock);continue;
      }
      if(q[0]!=rtu.unit && q[0]!=0)continue;
      portENTER_CRITICAL(&rtuLock);int len=sedona_rtu_slave(rtu.bank,rtu.unit,q,n,p);rtu.transactions++;rtu.fault=0;portEXIT_CRITICAL(&rtuLock);
      if(len && !sendFrame(p,len)){
        portENTER_CRITICAL(&rtuLock);rtu.errors++;rtu.fault=-5;portEXIT_CRITICAL(&rtuLock);
      }
    }
  }
  cleanup();vTaskDelete(NULL);
}
static int openRtu(void* who,int rx,int tx,int de,int baud,int parity,int unit,int role,int timeout)
{
  if(rtu.owner)return rtu.owner==who && !rtu.closing?0:-3;
  if(!who || unit<1 || unit>247 || role<0 || role>1 || parity<0 || parity>2 ||
     baud<1200 || baud>115200 || timeout<10 || timeout>5000 || de<0 || rx==tx || rx==de || tx==de)return -1;
  rtu.owner=who;rtu.baud=baud;rtu.parity=parity;rtu.unit=unit;rtu.role=role;rtu.timeout=timeout;
  int pins[]={rx,tx,de};
  for(int i=0;i<3;i++){int r=sedona_pin_claim(who,pins[i],i!=0);if(r){cleanup();return r;}rtu.pins[rtu.pinCount++]=pins[i];}
  uart_config_t cfg={.baud_rate=baud,.data_bits=UART_DATA_8_BITS,
    .parity=parity==1?UART_PARITY_ODD:parity==2?UART_PARITY_EVEN:UART_PARITY_DISABLE,
    .stop_bits=UART_STOP_BITS_1,.flow_ctrl=UART_HW_FLOWCTRL_DISABLE,.source_clk=UART_SCLK_DEFAULT};
  if(uart_driver_install(UART_NUM_1,1024,0,0,NULL,0)!=ESP_OK)goto error;
  rtu.installed=1;
  if(uart_param_config(UART_NUM_1,&cfg)!=ESP_OK || uart_set_pin(UART_NUM_1,tx,rx,de,UART_PIN_NO_CHANGE)!=ESP_OK ||
     uart_set_mode(UART_NUM_1,UART_MODE_RS485_HALF_DUPLEX)!=ESP_OK ||
     xTaskCreate(worker,"sedona_rtu",4096,NULL,4,&rtu.task)!=pdPASS)goto error;
  return 0;
error:cleanup();return -5;
}
#endif
Cell bclimateEsp32_RtuService_open(SedonaVM* vm,Cell* p){
  Cell r;r.ival=-7;
#ifndef ESP32SIM
  r.ival=openRtu(p[0].aval,p[1].ival,p[2].ival,p[3].ival,p[4].ival,p[5].ival,p[6].ival,p[7].ival,p[8].ival);
#endif
  return r;
}
Cell bclimateEsp32_RtuService_status(SedonaVM* vm,Cell* p){
  Cell r;r.ival=-7;
#ifndef ESP32SIM
  portENTER_CRITICAL(&rtuLock);
  if(rtu.owner!=p[0].aval || rtu.closing)r.ival=-4;
  else switch(p[1].ival){case 0:r.ival=rtu.busy;break;case 1:r.ival=rtu.fault;break;case 2:r.ival=rtu.transactions;break;default:r.ival=rtu.errors;}
  portEXIT_CRITICAL(&rtuLock);
#endif
  return r;
}
Cell bclimateEsp32_RtuService_command(SedonaVM* vm,Cell* p){
  Cell r;r.ival=-7;
#ifndef ESP32SIM
  int fc=p[1].ival,a=p[2].ival,n=p[3].ival,v=p[4].ival;
  if((fc<1 || fc>6) && fc!=15 && fc!=16){r.ival=-1;return r;}
  if(a<0 || a>65535 || n<1 || n>64 || (fc!=5 && fc!=6 && a+n>65536) ||
     (fc==5 && v!=0 && v!=65280) || (fc==6 && (v<0 || v>65535))){r.ival=-1;return r;}
  portENTER_CRITICAL(&rtuLock);
  if(rtu.owner!=p[0].aval || rtu.closing || rtu.role)r.ival=-4;
  else if(rtu.busy)r.ival=-9;
  else {rtu.fc=fc;rtu.address=a;rtu.quantity=n;rtu.value=v;rtu.busy=1;r.ival=0;}
  portEXIT_CRITICAL(&rtuLock);
  if(r.ival==0)xTaskNotifyGive(rtu.task);
#endif
  return r;
}
Cell bclimateEsp32_RtuService_word(SedonaVM* vm,Cell* p){
  Cell r;r.ival=-7;
#ifndef ESP32SIM
  int table=p[1].ival,index=p[2].ival,value=p[3].ival,write=p[4].ival;
  if(table<0 || table>3 || index<0 || index>=64 || (write && (value<0 || value>65535 || (table>=2 && value>1)))){r.ival=-1;return r;}
  portENTER_CRITICAL(&rtuLock);
  if(rtu.owner!=p[0].aval || rtu.closing)r.ival=-4;
  else {if(write)rtu.bank[table][index]=value;r.ival=rtu.bank[table][index];}
  portEXIT_CRITICAL(&rtuLock);
#endif
  return r;
}
Cell bclimateEsp32_RtuService_close(SedonaVM* vm,Cell* p){
#ifndef ESP32SIM
  if(rtu.owner==p[0].aval && rtu.owner){rtu.closing=1;if(rtu.task)xTaskNotifyGive(rtu.task);}
#endif
  return nullCell;
}
