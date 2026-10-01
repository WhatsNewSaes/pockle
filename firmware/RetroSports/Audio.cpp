#include <Arduino.h>
#include <ESP_I2S.h>
#include <esp_heap_caps.h>
#include <atomic>
#include <algorithm>
#include "Audio.h"
#include "es8311.h"

namespace audio {
namespace {
constexpr int PIN_MCLK=13,PIN_BCLK=14,PIN_WS=47,PIN_DOUT=48,PIN_DIN=21,PIN_PA=39;
I2SClass i2s;
es8311_handle_t codec=nullptr;
bool ready=false;
std::atomic<bool> active(false),stopRequested(false),busyPlaying(false);
std::atomic<size_t> length(0);
uint8_t* buffer=nullptr;
TaskHandle_t task=nullptr;

void recordTask(void*){
 static uint8_t chunk[2048];
 while(!stopRequested&&length<MAX_BYTES){
  size_t n=i2s.readBytes((char*)chunk,sizeof(chunk));
  if(!n){vTaskDelay(1);continue;}
  size_t room=MAX_BYTES-length;if(n>room)n=room;
  memcpy(buffer+length,chunk,n);length+=n;
 }
 active=false;task=nullptr;vTaskDelete(nullptr);
}
}

bool init(){
 pinMode(PIN_PA,OUTPUT);digitalWrite(PIN_PA,LOW);
 codec=es8311_create(0,ES8311_ADDRESS_0);
 const es8311_clock_config_t clk={false,false,true,(int)(SAMPLE_RATE*256),(int)SAMPLE_RATE};
 ready=es8311_init(codec,&clk,ES8311_RESOLUTION_16,ES8311_RESOLUTION_16)==ESP_OK
  &&es8311_voice_volume_set(codec,0,nullptr)==ESP_OK
  &&es8311_microphone_config(codec,false)==ESP_OK
  &&es8311_microphone_gain_set(codec,ES8311_MIC_GAIN_24DB)==ESP_OK;
 Serial.printf("AUDIO codec=%s\n",ready?"ok":"missing");
 return ready;
}
bool available(){return ready;}
bool recording(){return active;}
size_t recordedBytes(){return length;}

bool playing(){return busyPlaying;}
static uint8_t silence[2400];
// Playback runs through a PSRAM ring buffer fed by the network side and drained
// by its own task, so a slow chunk never starves the codec mid-word.
namespace {
uint8_t* ring=nullptr;size_t ringCap=0;std::atomic<size_t> ringHead(0),ringTail(0),ringCount(0);
std::atomic<bool> ringEnded(false),playerDone(true),aborted(false);uint32_t playRate=24000;
void playerTask(void*){
 const size_t prebuffer=playRate*2*8/10; // 0.8 s before the first sample
 while(ringCount<prebuffer&&!ringEnded)vTaskDelay(pdMS_TO_TICKS(5));
 // The amplifier takes tens of milliseconds to come up after enable; play 200 ms of
 // silence through it first so the first word is not swallowed.
 digitalWrite(PIN_PA,HIGH);for(int i=0;i<4;i++)i2s.write(silence,sizeof(silence));
 for(;;){
  if(aborted)break;
  size_t count=ringCount;
  if(!count){if(ringEnded)break;vTaskDelay(pdMS_TO_TICKS(3));continue;}
  size_t tail=ringTail,n=std::min({count,(size_t)4096,ringCap-tail});
  i2s.write(ring+tail,n);ringTail=(tail+n)%ringCap;ringCount-=n;
 }
 i2s.write(silence,sizeof(silence));vTaskDelay(pdMS_TO_TICKS(60));
 playerDone=true;vTaskDelete(nullptr);
}
}
bool playBegin(uint32_t rate){
 if(!ready||active||busyPlaying)return false;
 if(!ring){ringCap=1536*1024;ring=(uint8_t*)heap_caps_malloc(ringCap,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!ring)return false;}
 busyPlaying=true;playRate=rate;ringHead=ringTail=ringCount=0;ringEnded=false;aborted=false;playerDone=false;
 es8311_sample_frequency_config(codec,rate*256,rate);
 i2s.setPins(PIN_BCLK,PIN_WS,PIN_DOUT,PIN_DIN,PIN_MCLK);
 if(!i2s.begin(I2S_MODE_STD,rate,I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_MONO,I2S_STD_SLOT_LEFT)){busyPlaying=false;playerDone=true;return false;}
 es8311_voice_volume_set(codec,80,nullptr);es8311_voice_mute(codec,false);
 memset(silence,0,sizeof(silence));i2s.write(silence,sizeof(silence));
 if(xTaskCreatePinnedToCore(playerTask,"speaker",4096,nullptr,3,nullptr,1)!=pdPASS){i2s.end();busyPlaying=false;playerDone=true;return false;}
 return true;
}
size_t playWrite(const uint8_t* pcm,size_t bytes){
 if(!busyPlaying)return 0;size_t off=0;
 while(off<bytes){if(aborted)return bytes; // dropped: the listener left
  size_t room=ringCap-ringCount;if(!room){vTaskDelay(pdMS_TO_TICKS(3));continue;}
  size_t head=ringHead,n=std::min({bytes-off,room,ringCap-head});
  memcpy(ring+head,pcm+off,n);ringHead=(head+n)%ringCap;ringCount+=n;off+=n;
 }
 return off;
}
void playAbort(){if(!busyPlaying)return;aborted=true;ringEnded=true;}
void playEnd(){
 if(!busyPlaying)return;
 ringEnded=true;
 for(int i=0;i<12000&&!playerDone;i++)vTaskDelay(pdMS_TO_TICKS(5)); // up to 60 s of speech
 digitalWrite(PIN_PA,LOW);es8311_voice_mute(codec,true);es8311_voice_volume_set(codec,0,nullptr);
 i2s.end();es8311_sample_frequency_config(codec,SAMPLE_RATE*256,SAMPLE_RATE);
 busyPlaying=false;
}
bool play(const uint8_t* pcm,size_t bytes,uint32_t rate){
 if(!pcm||!bytes||!playBegin(rate))return false;playWrite(pcm,bytes);playEnd();return true;
}
void sleep(){
 digitalWrite(PIN_PA,LOW);if(!ready)return;
 if(active){stopRequested=true;for(int i=0;i<100&&active;i++)vTaskDelay(pdMS_TO_TICKS(5));i2s.end();}
 Serial.printf("AUDIO codec standby=%s\n",es8311_power_down(codec)==ESP_OK?"ok":"failed");
}
bool startRecording(){
 if(!ready||active||busyPlaying)return false;
 if(!buffer)buffer=(uint8_t*)heap_caps_malloc(MAX_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 if(!buffer)return false;
 i2s.setPins(PIN_BCLK,PIN_WS,PIN_DOUT,PIN_DIN,PIN_MCLK);
 if(!i2s.begin(I2S_MODE_STD,SAMPLE_RATE,I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_MONO,I2S_STD_SLOT_LEFT)){Serial.println("AUDIO i2s begin failed");return false;}
 length=0;stopRequested=false;active=true;
 if(xTaskCreatePinnedToCore(recordTask,"mic",4096,nullptr,3,&task,1)!=pdPASS){active=false;i2s.end();return false;}
 return true;
}

size_t stopRecording(uint8_t** pcm){
 *pcm=nullptr;if(!buffer)return 0;
 stopRequested=true;
 for(int i=0;i<200&&active;i++)vTaskDelay(pdMS_TO_TICKS(5));
 i2s.end();
 size_t n=length;*pcm=buffer;buffer=nullptr;length=0;
 return n;
}

void wavHeader(uint8_t out[44],size_t pcmBytes){
 auto u32=[&](int at,uint32_t v){out[at]=v&255;out[at+1]=(v>>8)&255;out[at+2]=(v>>16)&255;out[at+3]=(v>>24)&255;};
 auto u16=[&](int at,uint16_t v){out[at]=v&255;out[at+1]=(v>>8)&255;};
 memcpy(out,"RIFF",4);u32(4,36+pcmBytes);memcpy(out+8,"WAVEfmt ",8);u32(16,16);u16(20,1);u16(22,1);
 u32(24,SAMPLE_RATE);u32(28,SAMPLE_RATE*2);u16(32,2);u16(34,16);memcpy(out+36,"data",4);u32(40,pcmBytes);
}
}
