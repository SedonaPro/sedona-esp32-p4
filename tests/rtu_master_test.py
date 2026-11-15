"""Run the production RTU master against the production slave through a fake UART."""
from pathlib import Path
import subprocess,tempfile
native=Path(__file__).resolve().parents[1]/'platform/bclimateEsp32/native'
with tempfile.TemporaryDirectory() as temp:
    p=Path(temp);(p/'freertos').mkdir();(p/'driver').mkdir()
    (p/'sedona.h').write_text('#include <stdint.h>\n#include <stddef.h>\ntypedef union {int32_t ival;void* aval;} Cell;typedef struct {int unused;} SedonaVM;static Cell nullCell;\n')
    (p/'freertos/FreeRTOS.h').write_text('''#pragma once
#include <stddef.h>
typedef int portMUX_TYPE;typedef void* TaskHandle_t;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define pdMS_TO_TICKS(x) (x)
#define pdTRUE 1
#define pdPASS 1
#define ESP_OK 0
''')
    (p/'freertos/task.h').write_text('''#pragma once
#include "FreeRTOS.h"
static void ulTaskNotifyTake(int a,int b){(void)a;(void)b;}
static void vTaskDelay(int a){(void)a;}
static void vTaskDelete(void* p){(void)p;}
static void xTaskNotifyGive(void* p){(void)p;}
static int xTaskCreate(void(*f)(void*),const char* n,int s,void* a,int pr,void** task){(void)f;(void)n;(void)s;(void)a;(void)pr;*task=(void*)1;return 1;}
''')
    (p/'driver/uart.h').write_text('''#pragma once
#include <string.h>
#include "rtu_codec.h"
#define UART_NUM_1 1
#define UART_DATA_8_BITS 8
#define UART_PARITY_ODD 1
#define UART_PARITY_EVEN 2
#define UART_PARITY_DISABLE 0
#define UART_STOP_BITS_1 1
#define UART_HW_FLOWCTRL_DISABLE 0
#define UART_SCLK_DEFAULT 0
#define UART_PIN_NO_CHANGE -1
#define UART_MODE_RS485_HALF_DUPLEX 1
typedef struct {int baud_rate,data_bits,parity,stop_bits,flow_ctrl,source_clk;} uart_config_t;
static uint16_t peer[4][64];static unsigned char reply[260];static int replyLen,replyAt,drop,badcrc,installed;
static int uart_read_bytes(int port,unsigned char* p,int n,int wait){(void)port;(void)wait;int available=replyLen-replyAt;if(n>available)n=available;memcpy(p,reply+replyAt,n);replyAt+=n;return n;}
static int uart_write_bytes(int port,const void* p,int n){(void)port;replyAt=0;replyLen=drop?0:sedona_rtu_slave(peer,7,p,n,reply);if(badcrc&&replyLen)reply[replyLen-1]^=1;return n;}
static int uart_wait_tx_done(int port,int wait){(void)port;(void)wait;return 0;}
static void uart_flush_input(int port){(void)port;replyAt=replyLen=0;}
static int uart_driver_install(int p,int a,int b,int c,void* d,int e){(void)p;(void)a;(void)b;(void)c;(void)d;(void)e;if(installed)return -1;installed=1;return 0;}
static void uart_driver_delete(int port){(void)port;installed=0;}
static int uart_param_config(int p,const uart_config_t* c){(void)p;(void)c;return 0;}
static int uart_set_pin(int p,int tx,int rx,int de,int cts){(void)p;(void)tx;(void)rx;(void)de;(void)cts;return 0;}
static int uart_set_mode(int p,int m){(void)p;(void)m;return 0;}
''')
    (p/'test.c').write_text('''#include <assert.h>
static int claims;
int sedona_pin_claim(void* o,int p,int output){(void)o;(void)output;if(p<0)return -1;claims++;return 0;}
void sedona_pin_release(void* o,int p){(void)o;(void)p;claims--;}
#include "'''+str(native/'bclimateEsp32_RtuService.c')+'''"
int main(void){
 int owner;
 assert(openRtu(&owner,1,2,3,19200,2,7,0,100)==0 && claims==3);
 rtu.quantity=2;rtu.address=0;
 for(int fc=1;fc<=4;fc++){
   int table=fc==3?0:fc==4?1:fc==1?2:3;
   peer[table][0]=fc<=2?1:0x1234;peer[table][1]=fc<=2?0:0xabcd;
   rtu.fc=fc;assert(master()==0);assert(rtu.bank[table][0]==peer[table][0] && rtu.bank[table][1]==peer[table][1]);
 }
 rtu.fc=6;rtu.address=4;rtu.value=0x9876;assert(master()==0 && peer[0][4]==0x9876);
 rtu.fc=5;rtu.value=0xff00;assert(master()==0 && peer[2][4]==1);
 rtu.address=10;rtu.quantity=9;rtu.fc=15;rtu.bank[2][0]=1;rtu.bank[2][8]=1;
 assert(master()==0 && peer[2][10]==1 && peer[2][18]==1);
 rtu.fc=16;rtu.quantity=2;rtu.bank[0][0]=101;rtu.bank[0][1]=202;
 assert(master()==0 && peer[0][10]==101 && peer[0][11]==202);
 rtu.fc=3;drop=1;assert(master()==-12);drop=0;badcrc=1;assert(master()==-13);badcrc=0;
 rtu.address=100;assert(master()==-22);
 cleanup();assert(claims==0 && !installed && !rtu.owner);
 return 0;
}
''')
    subprocess.run(['gcc','-std=c99','-Wall','-Wextra','-Werror','-Wno-unused-function',
                    '-Wno-unused-parameter','-Wno-array-parameter','-Wno-misleading-indentation',
                    '-I'+str(p),'-I'+str(native),str(p/'test.c'),str(native/'rtu_codec.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('RTU master/slave loopback: all 8 functions, timeout, CRC, exceptions and UART cleanup PASS')
