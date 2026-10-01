#include <cassert>
#include <iostream>
#include <vector>
#include "NetworkBody.h"
int main(){
 ScoreBody body;const uint8_t first[]={1,2,3};assert(body.write(first,3)==3);assert(body.write(uint8_t(4))==1);assert(body.length==4);assert(body.data[0]==1&&body.data[3]==4);
 std::vector<uint8_t> fill(4*1024*1024-4,7);assert(body.write(fill.data(),fill.size())==fill.size());assert(body.length==4*1024*1024);assert(body.write(uint8_t(8))==0);assert(body.failed);assert(body.length==4*1024*1024);assert(body.data[body.length-1]==7);
 ScoreJsonAllocator alloc;JsonDocument d(&alloc);assert(!deserializeJson(d,"{\"ok\":true}"));assert(d["ok"]==true);
 std::cout<<"PASS: response growth, exact 4 MB limit, overflow rejection, and JSON allocator\n";
}
