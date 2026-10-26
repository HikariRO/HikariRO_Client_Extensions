/* HikariRO SPR reader: bounded, palette-based SPR 2.x image decoder.
 * The ACT layer/frame composer and MobID->sprite resource mapping are separate.
 * No pre-rendered PNG assets are used.
 */
#ifndef HRO_SPR_READER_H
#define HRO_SPR_READER_H
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct HroSprImage {
    unsigned short width,height;
    uint32_t* argb;
} HroSprImage;
typedef struct HroSprFile {
    unsigned short indexed_count,rgba_count;
    HroSprImage* images;
} HroSprFile;
static unsigned hro_spr_u16(const unsigned char* p) {
    return (unsigned)p[0]|((unsigned)p[1]<<8);
}
static unsigned hro_spr_u32(const unsigned char* p) {
    return (unsigned)p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24);
}
static void hro_spr_free(HroSprFile* f) {
    if(!f)return;
    if(f->images) {
        unsigned n=(unsigned)f->indexed_count+f->rgba_count;
        for(unsigned i=0;i<n;i++)free(f->images[i].argb);
        free(f->images);
    }
    memset(f,0,sizeof(*f));
}
/* Decode the indexed RLE used in SPR >= 2.1. 0,count means a run of
 * transparent palette index zero; all other bytes are literal indices. */
static int hro_spr_expand_indexed(const unsigned char* src,size_t len,
    unsigned char* out,size_t pixels,int rle) {
    size_t pos=0,dst=0;
    while(dst<pixels) {
        if(pos>=len)return 0;
        unsigned char v=src[pos++];
        if(rle && v==0) {
            if(pos>=len)return 0;
            unsigned count=src[pos++];
            if(count==0 || count>pixels-dst)return 0;
            memset(out+dst,0,count);dst+=count;
        } else out[dst++]=v;
    }
    return pos==len;
}
static int hro_spr_decode(const unsigned char* data,size_t size,HroSprFile* result) {
    if(!data||!result||size<10||data[0]!='S'||data[1]!='P')return 0;
    HroSprFile tmp={0};
    unsigned major=data[3],minor=data[2];
    if(major!=2 || minor>1)return 0; /* SPR 2.0 and 2.1 only */
    unsigned indexed=hro_spr_u16(data+4),rgba=hro_spr_u16(data+6);
    if(indexed+rgba==0 || indexed+rgba>4096)return 0;
    tmp.indexed_count=(unsigned short)indexed;
    tmp.rgba_count=(unsigned short)rgba;
    tmp.images=(HroSprImage*)calloc(indexed+rgba,sizeof(HroSprImage));
    if(!tmp.images)return 0;
    size_t offset=8;
    /* The indexed frame pixel data is stored before RGBA frames; palette
       is the last 1024 bytes of SPR 2.x. */
    if(size<offset+1024)goto fail;
    const unsigned char* palette=data+size-1024;
    for(unsigned i=0;i<indexed+rgba;i++) {
        if(offset+4>size-1024)goto fail;
        unsigned w=hro_spr_u16(data+offset),h=hro_spr_u16(data+offset+2);
        offset+=4;
        if(!w||!h||w>2048||h>2048||(size_t)w*h>1048576)goto fail;
        size_t count=(size_t)w*h;
        tmp.images[i].width=(unsigned short)w;
        tmp.images[i].height=(unsigned short)h;
        tmp.images[i].argb=(uint32_t*)malloc(count*sizeof(uint32_t));
        if(!tmp.images[i].argb)goto fail;
        if(i<indexed) {
            size_t bytes=count;
            if(minor>=1) {
                if(offset+2>size-1024)goto fail;
                bytes=hro_spr_u16(data+offset);offset+=2;
            }
            if(bytes>size-1024-offset)goto fail;
            unsigned char* indices=(unsigned char*)malloc(count);
            if(!indices)goto fail;
            int valid=hro_spr_expand_indexed(data+offset,bytes,indices,count,minor>=1);
            if(valid) {
                for(size_t k=0;k<count;k++) {
                    unsigned idx=indices[k];
                    const unsigned char* c=palette+idx*4;
                    tmp.images[i].argb[k]=idx==0?0:
                        (0xff000000u|((uint32_t)c[0]<<16)|((uint32_t)c[1]<<8)|c[2]);
                }
            }
            free(indices);
            if(!valid)goto fail;
            offset+=bytes;
        } else {
            if(count>(size-1024-offset)/4)goto fail;
            for(size_t k=0;k<count;k++) {
                const unsigned char* c=data+offset+k*4;
                tmp.images[i].argb[k]=((uint32_t)c[3]<<24)|
                    ((uint32_t)c[2]<<16)|((uint32_t)c[1]<<8)|c[0];
            }
            offset+=count*4;
        }
    }
    *result=tmp;
    return 1;
fail:
    hro_spr_free(&tmp);
    return 0;
}
#endif
