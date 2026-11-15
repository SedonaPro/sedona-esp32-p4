"""Compile and exercise the production RTU codec, including malformed frames."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
native=root/'platform/bclimateEsp32/native'
with tempfile.TemporaryDirectory() as temp:
    p=Path(temp)
    (p/'test.c').write_text(r'''
#include <assert.h>
#include <string.h>
#include "rtu_codec.h"
static uint16_t bank[4][64];static uint8_t q[260],p[260];
static int finish(int n){uint16_t c=sedona_rtu_crc(q,n);q[n]=c;q[n+1]=c>>8;return n+2;}
static int request(int unit,int fc,int a,int value){
 q[0]=unit;q[1]=fc;q[2]=a>>8;q[3]=a;q[4]=value>>8;q[5]=value;
 return finish(6);
}
int main(void){
 const uint8_t vector[]={1,3,0,0,0,10};assert(sedona_rtu_crc(vector,6)==0xcdc5);
 int n=request(1,6,63,0xabcd);assert(sedona_rtu_slave(bank,1,q,n,p)==8 && bank[0][63]==0xabcd && !sedona_rtu_crc(p,8));
 n=request(1,3,63,1);assert(sedona_rtu_slave(bank,1,q,n,p)==7 && p[2]==2 && p[3]==0xab && p[4]==0xcd);
 n=request(1,3,63,2);assert(sedona_rtu_slave(bank,1,q,n,p)==5 && p[1]==0x83 && p[2]==2);
 n=request(1,5,5,0xff00);assert(sedona_rtu_slave(bank,1,q,n,p)==8 && bank[2][5]==1);
 n=request(1,5,5,1);assert(sedona_rtu_slave(bank,1,q,n,p)==5 && p[2]==3 && bank[2][5]==1);
 n=request(0,6,0,123);assert(sedona_rtu_slave(bank,1,q,n,p)==0 && bank[0][0]==123);
 n=request(2,6,0,456);assert(sedona_rtu_slave(bank,1,q,n,p)==0 && bank[0][0]==123);
 q[n-1]^=1;assert(sedona_rtu_slave(bank,2,q,n,p)==0 && bank[0][0]==123);
 n=request(1,16,62,2);q[6]=4;q[7]=0x12;q[8]=0x34;q[9]=0x56;q[10]=0x78;n=finish(11);
 assert(sedona_rtu_slave(bank,1,q,n,p)==8 && bank[0][62]==0x1234 && bank[0][63]==0x5678);
 q[6]=3;n=finish(11);assert(sedona_rtu_slave(bank,1,q,n,p)==5 && p[2]==3);
 n=request(1,15,0,9);q[6]=2;q[7]=0xa5;q[8]=1;n=finish(9);
 assert(sedona_rtu_slave(bank,1,q,n,p)==8 && bank[2][0]==1 && bank[2][1]==0 && bank[2][8]==1);
 n=request(1,1,0,9);assert(sedona_rtu_slave(bank,1,q,n,p)==7 && p[2]==2 && p[3]==0xa5 && p[4]==1);
 bank[1][0]=0x55aa;n=request(1,4,0,1);assert(sedona_rtu_slave(bank,1,q,n,p)==7 && p[3]==0x55 && p[4]==0xaa);
 bank[3][0]=1;n=request(1,2,0,1);assert(sedona_rtu_slave(bank,1,q,n,p)==6 && p[3]==1);
 n=request(1,99,0,1);assert(sedona_rtu_slave(bank,1,q,n,p)==5 && p[2]==1);
 for(int len=0;len<8;len++)assert(sedona_rtu_slave(bank,1,q,len,p)==0);
 return 0;
}
''')
    subprocess.run(['gcc','-std=c99','-Wall','-Wextra','-Werror','-Wno-array-parameter',
                    '-I'+str(native),str(p/'test.c'),str(native/'rtu_codec.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('RTU codec: FC1/2/3/4/5/6/15/16, CRC, broadcast, bounds, exception and malformed-frame checks PASS')
