#pragma once
#ifndef ESP32SIM
int sedona_pin_claim(void* owner, int pin, int output);
int sedona_pin_owned(void* owner, int pin);
void sedona_pin_release(void* owner, int pin);
#endif
