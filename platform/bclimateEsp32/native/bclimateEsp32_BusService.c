#include "sedona.h"
#ifndef ESP32SIM
#include "esp32_pins.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include <string.h>
typedef struct {
  void* owner;int kind,cfg[6],pins[4],pinCount;
  volatile int state,cancel; /* 0 idle, 1 queued/running, 2 completed */
  TaskHandle_t task;
  i2c_master_bus_handle_t i2c;i2c_master_dev_handle_t device;
  spi_device_handle_t spi;int spiInitialized,uartInitialized;
  int txLen,rxLen,timeout,result;
  unsigned char tx[256],rx[256];
} Bus;
static Bus buses[3];
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static void cleanup(Bus* b)
{
  if(b->device){i2c_master_bus_rm_device(b->device);b->device=NULL;}
  if(b->i2c){i2c_del_master_bus(b->i2c);b->i2c=NULL;}
  if(b->spi){spi_bus_remove_device(b->spi);b->spi=NULL;}
  if(b->spiInitialized){spi_bus_free(SPI2_HOST);b->spiInitialized=0;}
  if(b->uartInitialized){uart_driver_delete(UART_NUM_1);b->uartInitialized=0;}
  for(int i=0;i<b->pinCount;i++)sedona_pin_release(b->owner,b->pins[i]);
  portENTER_CRITICAL(&lock);b->owner=NULL;b->pinCount=0;b->cancel=0;b->state=0;portEXIT_CRITICAL(&lock);
}
static void worker(void* arg)
{
  Bus* b=arg;
  while(1){
    ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
    if(b->cancel){cleanup(b);continue;}
    if(!b->owner || b->state!=1)continue;
    int r=ESP_FAIL;
    if(b->kind==0){
      if(b->txLen && b->rxLen)r=i2c_master_transmit_receive(b->device,b->tx,b->txLen,b->rx,b->rxLen,b->timeout);
      else if(b->txLen)r=i2c_master_transmit(b->device,b->tx,b->txLen,b->timeout);
      else r=i2c_master_receive(b->device,b->rx,b->rxLen,b->timeout);
    } else if(b->kind==1){
      int n=b->txLen>b->rxLen?b->txLen:b->rxLen;
      spi_transaction_t t={.length=n*8,.tx_buffer=b->tx,.rx_buffer=b->rx};
      /* Exclusive synchronous hardware transaction in this worker, never scan. */
      r=spi_device_transmit(b->spi,&t);
    } else {
      uart_flush_input(UART_NUM_1);
      r=ESP_OK;
      if(b->txLen && (uart_write_bytes(UART_NUM_1,b->tx,b->txLen)!=b->txLen ||
         uart_wait_tx_done(UART_NUM_1,pdMS_TO_TICKS(b->timeout))!=ESP_OK))r=ESP_FAIL;
      if(r==ESP_OK && b->rxLen){
        int n=uart_read_bytes(UART_NUM_1,b->rx,b->rxLen,pdMS_TO_TICKS(b->timeout));
        if(n!=b->rxLen)r=ESP_ERR_TIMEOUT;
      }
    }
    portENTER_CRITICAL(&lock);
    b->result=r==ESP_OK?b->rxLen:r==ESP_ERR_TIMEOUT?-12:-5;b->state=2;
    int cancel=b->cancel;portEXIT_CRITICAL(&lock);
    if(cancel)cleanup(b);
  }
}
static int configure(void* who,int kind,int a,int bpin,int c,int d,int rate,int mode)
{
  if(!who || kind<0 || kind>2 || rate<=0)return -1;
  if((kind==0 && (c<8 || c>119 || rate>400000)) ||
     (kind==1 && (mode<0 || mode>3 || rate>20000000)) ||
     (kind==2 && (mode<0 || mode>2 || rate<1200 || rate>1000000)))return -1;
  Bus* b=&buses[kind];int cfg[]={a,bpin,c,d,rate,mode};
  if(b->owner){
    if(b->owner!=who || b->cancel)return -3;
    if(memcmp(cfg,b->cfg,sizeof(cfg)))return -3; /* disable before reconfigure */
    return 0;
  }
  b->kind=kind;b->owner=who;memcpy(b->cfg,cfg,sizeof(cfg));
  int pins[]={a,bpin,c,d},n=kind==0?2:kind==1?4:c>=0?3:2;
  for(int i=0;i<n;i++){
    for(int j=0;j<i;j++)if(pins[j]==pins[i]){cleanup(b);return -1;}
    int output=kind==0 || (kind==1?i!=2:i!=0);
    int r=sedona_pin_claim(who,pins[i],output);if(r){cleanup(b);return r;}
    b->pins[b->pinCount++]=pins[i];
  }
  if(kind==0){
    i2c_master_bus_config_t cfg={.i2c_port=0,.sda_io_num=a,.scl_io_num=bpin,
      .clk_source=I2C_CLK_SRC_DEFAULT,.glitch_ignore_cnt=7,.flags.enable_internal_pullup=1};
    if(i2c_new_master_bus(&cfg,&b->i2c)!=ESP_OK)goto error;
    i2c_device_config_t dev={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=c,.scl_speed_hz=rate};
    if(i2c_master_bus_add_device(b->i2c,&dev,&b->device)!=ESP_OK)goto error;
  } else if(kind==1){
    spi_bus_config_t cfg={.mosi_io_num=bpin,.miso_io_num=c,.sclk_io_num=a,
      .quadwp_io_num=-1,.quadhd_io_num=-1,.max_transfer_sz=256};
    if(spi_bus_initialize(SPI2_HOST,&cfg,SPI_DMA_CH_AUTO)!=ESP_OK)goto error;
    b->spiInitialized=1;
    spi_device_interface_config_t dev={.clock_speed_hz=rate,.mode=mode,.spics_io_num=d,.queue_size=1};
    if(spi_bus_add_device(SPI2_HOST,&dev,&b->spi)!=ESP_OK)goto error;
  } else {
    uart_config_t cfg={.baud_rate=rate,.data_bits=UART_DATA_8_BITS,
      .parity=mode==1?UART_PARITY_ODD:mode==2?UART_PARITY_EVEN:UART_PARITY_DISABLE,
      .stop_bits=UART_STOP_BITS_1,.flow_ctrl=UART_HW_FLOWCTRL_DISABLE,.source_clk=UART_SCLK_DEFAULT};
    if(uart_driver_install(UART_NUM_1,1024,0,0,NULL,0)!=ESP_OK)goto error;
    b->uartInitialized=1;
    if(uart_param_config(UART_NUM_1,&cfg)!=ESP_OK ||
       uart_set_pin(UART_NUM_1,bpin,a,c>=0?c:UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE)!=ESP_OK ||
       (c>=0 && uart_set_mode(UART_NUM_1,UART_MODE_RS485_HALF_DUPLEX)!=ESP_OK))goto error;
  }
  if(!b->task && xTaskCreate(worker,"sedona_bus",4096,b,4,&b->task)!=pdPASS)goto error;
  return 0;
error:cleanup(b);return -5;
}
#endif
Cell bclimateEsp32_BusService_configure(SedonaVM* vm,Cell* p){
  Cell r;
#ifdef ESP32SIM
  r.ival=-7;
#else
  r.ival=configure(p[0].aval,p[1].ival,p[2].ival,p[3].ival,p[4].ival,p[5].ival,p[6].ival,p[7].ival);
#endif
  return r;
}
Cell bclimateEsp32_BusService_submit(SedonaVM* vm,Cell* p){
  Cell r;r.ival=-7;
#ifndef ESP32SIM
  int kind=p[1].ival,n=p[3].ival,rx=p[4].ival,timeout=p[5].ival;
  if(kind<0 || kind>2 || n<0 || n>256 || rx<0 || rx>256 || (!n && !rx) || timeout<1 || timeout>1000 || (n && !p[2].aval)){r.ival=-1;return r;}
  Bus* b=&buses[kind];
  portENTER_CRITICAL(&lock);
  if(b->owner!=p[0].aval || b->cancel)r.ival=-4;
  else if(b->state)r.ival=-9;
  else {
    memset(b->tx,0,256);if(n)memcpy(b->tx,p[2].aval,n);
    b->txLen=n;b->rxLen=rx;b->timeout=timeout;b->state=1;r.ival=0;
  }
  portEXIT_CRITICAL(&lock);
  if(r.ival==0)xTaskNotifyGive(b->task);
#endif
  return r;
}
Cell bclimateEsp32_BusService_collect(SedonaVM* vm,Cell* p){
  Cell r;r.ival=-7;
#ifndef ESP32SIM
  int k=p[1].ival;
  if(k<0 || k>2 || !p[2].aval || p[3].ival<256){r.ival=-1;return r;}
  Bus* b=&buses[k];portENTER_CRITICAL(&lock);
  if(b->owner!=p[0].aval || b->cancel)r.ival=-4;
  else if(b->state!=2)r.ival=-9;
  else {r.ival=b->result;if(r.ival>0)memcpy(p[2].aval,b->rx,r.ival);b->state=0;}
  portEXIT_CRITICAL(&lock);
#endif
  return r;
}
Cell bclimateEsp32_BusService_close(SedonaVM* vm,Cell* p){
#ifndef ESP32SIM
  int k=p[1].ival;
  if(k>=0 && k<3){Bus* b=&buses[k];
    portENTER_CRITICAL(&lock);int ours=b->owner==p[0].aval && b->owner!=NULL;
    if(ours)b->cancel=1;portEXIT_CRITICAL(&lock);
    if(ours){if(b->task)xTaskNotifyGive(b->task);else cleanup(b);}
  }
#endif
  return nullCell;
}
