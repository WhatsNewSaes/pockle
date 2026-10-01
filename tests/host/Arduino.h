#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string>
#include <algorithm>
using std::min;using std::max;
#define PROGMEM
class __FlashStringHelper;
class String:public std::string {public:using std::string::string;unsigned int length()const{return size();}};
inline void yield(){}
class Stream {
public:
 virtual ~Stream(){}
 virtual size_t write(uint8_t)=0;
 virtual size_t write(const uint8_t*,size_t)=0;
 virtual int available()=0;
 virtual int read()=0;
 virtual int peek()=0;
 virtual void flush()=0;
};
