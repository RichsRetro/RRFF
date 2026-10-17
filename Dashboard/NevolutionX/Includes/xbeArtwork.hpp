// Original implementation based on the documented XBE/XPR formats:
// https://xboxdevwiki.net/Xbe and https://xboxdevwiki.net/XPR
#pragma once
#include <SDL.h>
#include <SDL_image.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
namespace XdashArtwork {
inline uint32_t le32(const unsigned char* p) { return p[0]|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24); }
inline unsigned le16(const unsigned char* p) { return p[0]|(unsigned(p[1])<<8); }
inline uint32_t rgb565(unsigned v) { return 0xFF000000u|(((v>>11)*255/31)<<16)|((((v>>5)&63)*255/63)<<8)|((v&31)*255/31); }
inline uint32_t blend(uint32_t a,uint32_t b,unsigned wa,unsigned wb) {
  uint32_t r=0xFF000000u; for(unsigned s=0;s<24;s+=8) r|=((((a>>s)&255)*wa+((b>>s)&255)*wb)/(wa+wb))<<s; return r;
}
inline size_t swizzled(unsigned x,unsigned y,unsigned w,unsigned h) {
  size_t result=0; unsigned out=0;
  for(unsigned bit=1;bit<w||bit<h;bit<<=1) {
    if(bit<w){if(x&bit)result|=size_t(1)<<out; ++out;}
    if(bit<h){if(y&bit)result|=size_t(1)<<out; ++out;}
  } return result;
}
inline SDL_Surface* decode(const std::vector<unsigned char>& b) {
  if(b.size()<32)return nullptr;
  if(le32(b.data())!=0x30525058) {
    return IMG_Load_RW(SDL_RWFromConstMem(b.data(),static_cast<int>(b.size())),1);
  }
  const unsigned header=le32(b.data()+8), gpu=le32(b.data()+24), size=le32(b.data()+28);
  if(header<32||header>b.size()||((le32(b.data()+12)>>16)&7)!=4)return nullptr;
  const unsigned format=(gpu>>8)&255;
  unsigned w=1u<<((gpu>>20)&15),h=1u<<((gpu>>24)&15);
  const bool linear=format==0x11||format==0x12||format==0x1E||format==0x3F;
  if(linear){w=(size&4095)+1;h=((size>>12)&4095)+1;}
  if(w>1024||h>1024)return nullptr;
  const size_t start=size_t(header)+le32(b.data()+16);
  if(start>b.size())return nullptr;
  const unsigned char* data=b.data()+start; const size_t available=b.size()-start;
  SDL_Surface* surface=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888);
  if(!surface)return nullptr;
  auto pixel=[&](unsigned x,unsigned y,uint32_t value) {
    if(x<w&&y<h)reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(surface->pixels)+y*surface->pitch)[x]=value;
  };
  bool valid=true;
  if(format==0x0C||format==0x0E||format==0x0F) {
    unsigned blockSize=format==0x0C?8:16;
    if(size_t((w+3)/4)*((h+3)/4)*blockSize>available)valid=false;
    else for(unsigned by=0;by<h;by+=4)for(unsigned bx=0;bx<w;bx+=4) {
      const unsigned char* block=data+((by/4)*((w+3)/4)+bx/4)*blockSize;
      const unsigned char* colors=block+(blockSize==8?0:8);
      unsigned c0=le16(colors),c1=le16(colors+2);
      uint32_t palette[4]={rgb565(c0),rgb565(c1),0,0};
      if(c0>c1||format!=0x0C){palette[2]=blend(palette[0],palette[1],2,1);palette[3]=blend(palette[0],palette[1],1,2);}
      else {palette[2]=blend(palette[0],palette[1],1,1);palette[3]=0;}
      uint32_t indices=le32(colors+4); unsigned alpha[8]={}; uint64_t alphaBits=0;
      if(format==0x0F){alpha[0]=block[0];alpha[1]=block[1];
        if(alpha[0]>alpha[1])for(unsigned k=2;k<8;++k)alpha[k]=((8-k)*alpha[0]+(k-1)*alpha[1])/7;
        else {for(unsigned k=2;k<6;++k)alpha[k]=((6-k)*alpha[0]+(k-1)*alpha[1])/5;alpha[6]=0;alpha[7]=255;}
        for(unsigned k=0;k<6;++k)alphaBits|=uint64_t(block[k+2])<<(8*k);
      }
      for(unsigned i=0;i<16;++i){uint32_t value=palette[(indices>>(2*i))&3];
        if(format==0x0E)value=(value&0xFFFFFFu)|uint32_t(((block[i/2]>>((i%2)*4))&15)*17)<<24;
        if(format==0x0F)value=(value&0xFFFFFFu)|uint32_t(alpha[(alphaBits>>(3*i))&7])<<24;
        pixel(bx+i%4,by+i/4,value);
      }
    }
  } else {
    unsigned bytes=(format==2||format==3||format==4||format==5||format==0x11)?2:4;
    valid=format==2||format==3||format==4||format==5||format==6||format==7||format==0x11||format==0x12||format==0x1E||format==0x3A||format==0x3F;
    // Linear resources can have a GPU pitch greater than width * pixel size.
    size_t pitch=linear?size_t(((size>>24)&255)+1)*64:0;
    if(linear&&pitch<w*bytes)pitch=w*bytes;
    if((linear?pitch*h:size_t(w)*h*bytes)>available)valid=false;
    if(valid)for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
      const unsigned char* p=data+(linear?y*pitch+x*bytes:swizzled(x,y,w,h)*bytes);uint32_t v;
      if(bytes==4){v=le32(p);if(format==7||format==0x1E)v|=0xFF000000u;
        if(format==0x3A||format==0x3F)v=(v&0xFF00FF00u)|((v&255)<<16)|((v>>16)&255);
      }else {unsigned c=le16(p);if(format==5||format==0x11)v=rgb565(c);
        else if(format==4)v=(((c>>12)*17)<<24)|(((c>>8)&15)*17<<16)|(((c>>4)&15)*17<<8)|((c&15)*17);
        else v=((format==3||(c&0x8000))?0xFF000000u:0)|(((c>>10)&31)*255/31<<16)|(((c>>5)&31)*255/31<<8)|((c&31)*255/31);
      }pixel(x,y,v);
    }
  }
  if(!valid){SDL_FreeSurface(surface);return nullptr;}return surface;
}
inline SDL_Surface* fromXbe(const std::string& path) {
  FILE* file=fopen(path.c_str(),"rb");if(!file)return nullptr;
  struct Close{FILE* p;~Close(){fclose(p);}} close{file};
  if(fseek(file,0,SEEK_END))return nullptr;long length=ftell(file);if(length<0x124)return nullptr;
  auto readAt=[&](uint32_t offset,void* out,size_t count){return offset<=uint32_t(length)&&count<=uint32_t(length)-offset&&!fseek(file,offset,SEEK_SET)&&fread(out,1,count,file)==count;};
  unsigned char header[0x124];if(!readAt(0,header,sizeof(header))||le32(header)!=0x48454258)return nullptr;
  uint32_t base=le32(header+0x104),sections=le32(header+0x120),count=le32(header+0x11C);
  if(sections<base||count>128)return nullptr;sections-=base;
  if(sections>uint32_t(length)||count>(uint32_t(length)-sections)/56)return nullptr;
  for(uint32_t i=0;i<count;++i){unsigned char section[56];if(!readAt(sections+i*56,section,56))return nullptr;
    uint32_t nameAddress=le32(section+20);if(nameAddress<base)continue;
    char name[16]={};if(!readAt(nameAddress-base,name,15)||strncmp(name,"$$XTIMAGE",9))continue;
    uint32_t offset=le32(section+12),bytes=le32(section+16);if(bytes<32||bytes>2*1024*1024)return nullptr;
    std::vector<unsigned char> image(bytes);if(!readAt(offset,image.data(),bytes))return nullptr;return decode(image);
  }return nullptr;
}
} // namespace XdashArtwork
