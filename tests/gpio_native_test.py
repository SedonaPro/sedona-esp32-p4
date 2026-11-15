#!/usr/bin/env python3
"""Exercise the actual GPIO native source with an instrumented ESP-IDF driver."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as temp:
    p = Path(temp)
    (p/'driver').mkdir()
    (p/'sedona.h').write_text('#include <stdint.h>\n#include <stddef.h>\ntypedef union {int32_t ival; void* aval;} Cell;\ntypedef struct {int unused;} SedonaVM;\nstatic Cell nullCell;\n')
    (p/'sdkconfig.h').write_text('#define CONFIG_SEDONA_GPIO_ALLOWED_PINS "2,3"\n')
    (p/'driver/gpio.h').write_text(r"""
#pragma once
#define GPIO_NUM_MAX 55
#define GPIO_IS_VALID_GPIO(p) ((p)<55)
#define GPIO_IS_VALID_OUTPUT_GPIO(p) ((p)<54)
#define GPIO_MODE_INPUT 1
#define GPIO_MODE_INPUT_OUTPUT 3
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_ENABLE 1
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
#define ESP_OK 0
typedef struct {unsigned long long pin_bit_mask; int mode,pull_up_en,pull_down_en,intr_type;} gpio_config_t;
static int levels[55], configs, resets, calls, setAt, configAt, fail;
static int gpio_set_level(int pin,int value) {setAt=++calls; levels[pin]=value; return fail;}
static int gpio_config(const gpio_config_t* cfg) {configAt=++calls; ++configs; return fail;}
static int gpio_get_level(int pin) {return levels[pin];}
static int gpio_reset_pin(int pin) {++resets; return 0;}
""")
    native = ROOT/'platform/bclimateEsp32/native/bclimateEsp32_GpioPoint.c'
    (p/'main.c').write_text('#include <assert.h>\n#include "'+str(native.parent/'esp32_pins.c')+'"\n#include "'+str(native)+'"\n'+r"""
int main(void) {
  int a,b; Cell args[5]={{0}}; args[0].aval=&a; args[1].ival=2; args[2].ival=1; args[4].ival=1;
  assert(bclimateEsp32_GpioPoint_configure(0,args).ival==0);
  assert(setAt<configAt && levels[2]==1 && configs==1);
  assert(bclimateEsp32_GpioPoint_configure(0,args).ival==0 && configs==1);
  args[0].aval=&b; assert(bclimateEsp32_GpioPoint_configure(0,args).ival==-3);
  assert(bclimateEsp32_GpioPoint_writePin(0,args).ival==-4);
  bclimateEsp32_GpioPoint_release(0,args); assert(resets==0);
  args[0].aval=&a; args[2].ival=0;
  assert(bclimateEsp32_GpioPoint_writePin(0,args).ival==0 && levels[2]==0);
  assert(bclimateEsp32_GpioPoint_readPin(0,args).ival==0);
  bclimateEsp32_GpioPoint_release(0,args); assert(resets==1);
  assert(bclimateEsp32_GpioPoint_configure(0,args).ival==0);
  assert(bclimateEsp32_GpioPoint_writePin(0,args).ival==-4);
  bclimateEsp32_GpioPoint_release(0,args);
  args[1].ival=-1; assert(bclimateEsp32_GpioPoint_configure(0,args).ival==-1);
  args[1].ival=55; assert(bclimateEsp32_GpioPoint_configure(0,args).ival==-1);
  args[1].ival=4; assert(bclimateEsp32_GpioPoint_configure(0,args).ival==-2);
  args[1].ival=2; args[3].ival=3; assert(bclimateEsp32_GpioPoint_configure(0,args).ival==-1);
  args[3].ival=0; fail=1; assert(bclimateEsp32_GpioPoint_configure(0,args).ival==-5);
  fail=0; args[0].aval=&b; assert(bclimateEsp32_GpioPoint_configure(0,args).ival==0);
  args[1].ival=3;
  assert(sedona_pin_claim(&a,3,0)==0);
  assert(bclimateEsp32_GpioPoint_configure(0,args).ival==-3);
  sedona_pin_release(&b,3);assert(sedona_pin_owned(&a,3));
  sedona_pin_release(&a,3);assert(!sedona_pin_owned(&a,3));
  assert(bclimateEsp32_GpioPoint_configure(0,args).ival==0);
  return 0;
}
""")
    subprocess.run(['gcc','-std=c99','-Wall','-Werror','-Wno-unused-function','-I'+str(p),str(p/'main.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('GPIO native: ownership, pin validation, allow-list, initial latch, read/write, release, driver errors PASS')
