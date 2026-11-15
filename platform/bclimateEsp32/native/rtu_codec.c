#include "rtu_codec.h"
uint16_t sedona_rtu_crc(const uint8_t* data,int length)
{
  uint16_t crc=0xffff;
  for(int i=0;i<length;i++){crc^=data[i];for(int bit=0;bit<8;bit++)crc=crc&1?(crc>>1)^0xa001:crc>>1;}
  return crc;
}
static int u16(const uint8_t* p){return (p[0]<<8)|p[1];}
static void put(uint8_t* p,int value){p[0]=value>>8;p[1]=value;}
static int finish(uint8_t* p,int n){uint16_t crc=sedona_rtu_crc(p,n);p[n]=crc;p[n+1]=crc>>8;return n+2;}
int sedona_rtu_slave(uint16_t bank[4][64],int unit,const uint8_t* q,int len,uint8_t* p)
{
  if(len<4 || len>256 || unit<1 || unit>247 || (q[0]!=unit && q[0]!=0) || sedona_rtu_crc(q,len))return 0;
  int broadcast=q[0]==0,fc=q[1],error=0,n=0;
  p[0]=unit;p[1]=fc;
  if(len<8){error=3;goto exception;}
  int address=u16(q+2),value=u16(q+4),count=value;
  if(fc>=1 && fc<=4){
    if(broadcast)return 0;
    if(len!=8 || count<1 || count>64){error=3;goto exception;}
    if(address>=64 || count>64-address){error=2;goto exception;}
    int table=fc==3?0:fc==4?1:fc==1?2:3;
    p[2]=fc<=2?(count+7)/8:count*2;n=3+p[2];
    for(int i=3;i<n;i++)p[i]=0;
    for(int i=0;i<count;i++)if(fc<=2)p[3+i/8]|=(bank[table][address+i]!=0)<<(i%8);
      else put(p+3+i*2,bank[table][address+i]);
  } else if(fc==5 || fc==6){
    if(len!=8 || (fc==5 && value!=0 && value!=0xff00)){error=3;goto exception;}
    if(address>=64){error=2;goto exception;}
    bank[fc==5?2:0][address]=fc==5?value!=0:value;
    for(int i=2;i<6;i++)p[i]=q[i];
    n=6;
  } else if(fc==15 || fc==16){
    int bytes=fc==15?(count+7)/8:count*2;
    if(count<1 || count>64 || len!=9+bytes || q[6]!=bytes){error=3;goto exception;}
    if(address>=64 || count>64-address){error=2;goto exception;}
    for(int i=0;i<count;i++)bank[fc==15?2:0][address+i]=fc==15?(q[7+i/8]>>(i%8))&1:u16(q+7+i*2);
    put(p+2,address);put(p+4,count);n=6;
  } else {error=1;goto exception;}
  return broadcast?0:finish(p,n);
exception:
  if(broadcast)return 0;
  p[1]=fc|0x80;p[2]=error;return finish(p,3);
}
