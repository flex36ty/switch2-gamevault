#include "console/include/metadata_layout.h"
#include <assert.h>
#include <stdio.h>
static InstallPlan p,before;
static void u16(unsigned at,uint16_t n){memcpy(p.metadata+at,&n,2);}
static void u32(unsigned at,uint32_t n){memcpy(p.metadata+at,&n,4);}
static void u64(unsigned at,uint64_t n){memcpy(p.metadata+at,&n,8);}
int main(void){
 const uint64_t app=UINT64_C(0x0400c3f00006e000);
 p.schema=2;p.count=4;p.title=app+0x800;p.meta_size=272;
 u16(0,24);u16(2,4);u64(8,app);u32(20,3284);
 memset(p.metadata+128,0xa5,144);before=p;
 assert(vaultMetadataLayoutValid(&p,129,app));assert(!memcmp(&p,&before,sizeof(p)));
 p.schema=1;assert(!vaultMetadataLayoutValid(&p,129,app));p.meta_size=3412;assert(vaultMetadataLayoutValid(&p,129,app));
 p=before;p.meta_size=127;assert(!vaultMetadataLayoutValid(&p,129,app));
 p=before;p.meta_size=META_MAX+1;assert(!vaultMetadataLayoutValid(&p,129,app));
 p=before;u64(8,app+0x1000);assert(!vaultMetadataLayoutValid(&p,129,app));
 p=before;u16(4,1);assert(!vaultMetadataLayoutValid(&p,129,app));
 p=before;u16(2,5);assert(!vaultMetadataLayoutValid(&p,129,app));
 p=before;u16(0,0xffff);assert(!vaultMetadataLayoutValid(&p,129,app));
 p=before;p.schema=3;assert(!vaultMetadataLayoutValid(&p,129,app));
 p=before;p.title=app;u16(0,16);p.meta_size=120;assert(vaultMetadataLayoutValid(&p,128,app));
 p.schema=1;assert(vaultMetadataLayoutValid(&p,128,app));p.meta_size=121;assert(!vaultMetadataLayoutValid(&p,128,app));
 puts("Metadata layout: MKW installed record, legacy layout, preservation and malformed bounds passed.");return 0;
}
