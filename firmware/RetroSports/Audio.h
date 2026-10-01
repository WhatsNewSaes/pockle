#pragma once
#include <stdint.h>
#include <stddef.h>
// Microphone capture through the board's ES8311 codec (I2C 0x18) and I2S0.
// Pins from Waveshare's audio example: MCLK 13, BCLK 14, WS 47, mic data 21,
// speaker data 48, amplifier enable 39 (kept off: no spoken replies yet).
namespace audio {
constexpr uint32_t SAMPLE_RATE=12000; // speech transcribes as well as 16 kHz and uploads 25% less
constexpr size_t MAX_SECONDS=10;
constexpr size_t MAX_BYTES=SAMPLE_RATE*2*MAX_SECONDS; // 16-bit mono
bool init();                       // configure the codec; false if it is not answering
bool available();
bool startRecording();             // begins filling a PSRAM buffer until stop or MAX_SECONDS
size_t stopRecording(uint8_t** pcm); // hands the mono PCM buffer to the caller (heap_caps_free it)
bool recording();
size_t recordedBytes();
// Playback: begin (codec clock, amplifier), write blocking PCM, end (drain, amplifier off).
bool playBegin(uint32_t rate);
size_t playWrite(const uint8_t* pcm,size_t bytes);
void playEnd();
bool play(const uint8_t* pcm,size_t bytes,uint32_t rate); // convenience: begin, write, end
bool playing();
// Before deep sleep: codec into standby and the amplifier off. init() brings it back after wake.
void sleep();
// 44-byte PCM WAV header for the given mono 16-bit payload size.
void wavHeader(uint8_t out[44],size_t pcmBytes);
}
