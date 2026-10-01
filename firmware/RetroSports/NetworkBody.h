#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
// HTTPClient::writeToStream decodes HTTP chunk framing; getStream does not.
// Keep the bounded response and parsed JSON in the board's 8 MB PSRAM.
class ScoreBody : public Stream {
 public:
 uint8_t* data=nullptr;size_t length=0,capacity=0;bool failed=false;
 ~ScoreBody(){heap_caps_free(data);}
 size_t write(uint8_t b) override {return write(&b,1);}
 size_t write(const uint8_t* src,size_t n) override {
  constexpr size_t limit=4*1024*1024;
  if(failed||n>limit-length){failed=true;return 0;}
  if(length+n>capacity){size_t next=std::min(limit,std::max(length+n,capacity?capacity*2:size_t(8192)));
   void* p=heap_caps_realloc(data,next,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
   if(!p){failed=true;return 0;}data=static_cast<uint8_t*>(p);capacity=next;
  }
  memcpy(data+length,src,n);length+=n;return n;
 }
 int available() override{return 0;}
 int read() override{return -1;}
 int peek() override{return -1;}
 void flush() override{}
};
class ScoreJsonAllocator:public ArduinoJson::Allocator {
 public:
 void* allocate(size_t n)override{return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
 void deallocate(void* p)override{heap_caps_free(p);}
 void* reallocate(void* p,size_t n)override{return heap_caps_realloc(p,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);}
};
