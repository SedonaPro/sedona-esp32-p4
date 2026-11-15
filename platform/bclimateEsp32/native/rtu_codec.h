#pragma once
#include <stdint.h>
uint16_t sedona_rtu_crc(const uint8_t* data,int length);
/* bank: holding, input, coils, discrete; 64 addresses per table. Caller locks. */
int sedona_rtu_slave(uint16_t bank[4][64],int unit,const uint8_t* req,int length,uint8_t response[260]);
