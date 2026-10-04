#pragma once
#include <ArduinoJson.h>
#include <string>
#include <vector>
#include "Core.h"
#include "Bible.h"
#include "Voice.h"
// A one-screen daily devotional for kids (7-13) on the verse of the day, written
// by the model once a day and cached in NVS: a title, one truth, three bullets,
// something to do today, and a one-line prayer.
namespace retro {
struct Devotional { int day=-1; BibleRef ref; std::string verse,title,truth,apply,prayer,hash,scene; std::vector<std::string> points; bool valid=false,fromFile=false; }; // scene: the picture's id in /scenes, chosen by tools/pick_scenes.py
// Where the day's file lives: devotionals/out/MM-DD.json on the repo's main branch, written by tools/build_devotionals.py.
#define DEVOTIONAL_BASE_URL "https://raw.githubusercontent.com/WhatsNewSaes/pockle/main/devotionals/out/"
inline std::string devotionalFileUrl(int month,int day){char b[112];snprintf(b,sizeof(b),DEVOTIONAL_BASE_URL "%02d-%02d.json",month,day);return b;}
// The local copy on LittleFS, kept in sync with the repo: /devo/MM-DD.json plus /devo/index.json (day -> content hash).
inline std::string devotionalLocalPath(int month,int day){char b[24];snprintf(b,sizeof(b),"/devo/%02d-%02d.json",month,day);return b;}
inline std::string devotionalDayKey(int month,int day){char b[8];snprintf(b,sizeof(b),"%02d-%02d",month,day);return b;}
inline std::string devotionalSystemPrompt(){
 return "You write a one-screen daily devotional for kids aged 7 to 13, shown on a small e-paper device and read aloud. "
  "You are given the verse of the day (Berean Standard Bible) and a little of its chapter for context. Reply with strict JSON: "
  "{\"title\": <up to 4 words, no punctuation>, \"truth\": <one sentence, at most 90 characters: what this verse shows about God or about us>, "
  "\"points\": [<three short bullets, each at most 55 characters, simple and concrete>], "
  "\"apply\": <one or two short sentences, at most 140 characters, something a kid can actually do today>, "
  "\"prayer\": <one sentence, at most 110 characters, starting with 'God,' or 'Jesus,'>}. "
  "Keep it gospel-centered, not moralistic: the truth must point to what God has done for us in Jesus - His love, His death and resurrection, His grace and forgiveness - "
  "so the kid hears first what Jesus did, and only then what we do in response; never make the lesson 'be good so God will like you'. "
  "Plain words a 7-year-old understands, warm and encouraging, no theology jargon, do not quote other Bible verses, no markdown.";
}
inline std::string devotionalRequestBody(const BibleRef& ref,const std::string& verse,const std::string& context){
 return "{\"model\":\""+std::string(VOICE_MODEL)+"\",\"response_format\":{\"type\":\"json_object\"},\"max_tokens\":1500,\"temperature\":0.7," // room for the model's thinking tokens, which count against the limit
  "\"messages\":[{\"role\":\"system\",\"content\":\""+jsonEscape(devotionalSystemPrompt())+"\"},"
  "{\"role\":\"user\",\"content\":\""+jsonEscape("Verse of the day: "+bibleRefLabel(ref,false)+" - "+verse+"\nContext: "+context)+"\"}]}";
}
// Parses the chat completion; the model's JSON sits in message.content.
inline bool parseDevotional(JsonVariantConst root,const BibleRef& ref,const std::string& verse,int day,Devotional& out){
 const char* content=root["choices"][0]["message"]["content"].as<const char*>();if(!content)return false;
 JsonDocument inner;if(deserializeJson(inner,content))return false;
 Devotional d;d.ref=ref;d.verse=verse;d.day=day;
 auto str=[&](const char* k,size_t limit){const char* v=inner[k].as<const char*>();return clean(v?v:"",limit);};
 d.title=str("title",40);d.truth=str("truth",140);d.apply=str("apply",200);d.prayer=str("prayer",160);
 for(JsonVariantConst p:inner["points"].as<JsonArrayConst>()){const char* v=p.as<const char*>();if(v&&d.points.size()<3)d.points.push_back(clean(v,90));}
 if(d.title.empty()||d.truth.empty()||d.points.empty())return false;
 d.valid=true;out=d;return true;
}
// A day's file from the repo. `ref` in the file overrides the verse of the day; `verseFor` looks the text up.
inline bool decodeDevotionalFile(const std::string& json,int day,const BibleRef& fallbackRef,const std::string& fallbackVerse,const std::function<std::string(const BibleRef&)>& verseFor,Devotional& out){
 JsonDocument j;if(json.empty()||deserializeJson(j,json))return false;
 Devotional d;d.day=day;d.fromFile=true;
 auto str=[&](const char* k,size_t limit){const char* v=j[k].as<const char*>();return clean(v?v:"",limit);};
 d.title=str("title",40);d.truth=str("truth",140);d.apply=str("apply",200);d.prayer=str("prayer",160);d.scene=str("scene",24);
 for(JsonVariantConst p:j["points"].as<JsonArrayConst>()){const char* v=p.as<const char*>();if(v&&d.points.size()<3)d.points.push_back(clean(v,90));}
 const char* ref=j["ref"].as<const char*>();BibleRef r=ref?parseBibleRef(ref):BibleRef{};
 if(r.valid()&&r.verse>0){d.ref=r;d.verse=verseFor(r);}
 if(d.verse.empty()){d.ref=fallbackRef;d.verse=fallbackVerse;}
 if(d.title.empty()||d.truth.empty()||d.points.empty()||d.verse.empty())return false;
 d.valid=true;out=d;return true;
}
// NVS cache, and the verse it was written for.
inline std::string encodeDevotional(const Devotional& d){
 JsonDocument j;j["day"]=d.day;j["book"]=d.ref.book;j["chapter"]=d.ref.chapter;j["verse"]=d.ref.verse;j["text"]=d.verse;j["title"]=d.title;j["truth"]=d.truth;j["apply"]=d.apply;j["prayer"]=d.prayer;j["file"]=d.fromFile;j["hash"]=d.hash;j["scene"]=d.scene;
 JsonArray p=j["points"].to<JsonArray>();for(const auto& s:d.points)p.add(s);
 std::string out;serializeJson(j,out);return out;
}
inline bool decodeDevotional(const std::string& json,Devotional& out){
 JsonDocument j;if(json.empty()||deserializeJson(j,json))return false;
 Devotional d;d.day=j["day"]|-1;d.ref.book=j["book"]|0;d.ref.chapter=j["chapter"]|0;d.ref.verse=j["verse"]|0;
 auto str=[&](const char* k){const char* v=j[k].as<const char*>();return std::string(v?v:"");};
 d.verse=str("text");d.title=str("title");d.truth=str("truth");d.apply=str("apply");d.prayer=str("prayer");d.fromFile=j["file"]|false;d.hash=str("hash");d.scene=str("scene");
 for(JsonVariantConst p:j["points"].as<JsonArrayConst>()){const char* v=p.as<const char*>();if(v)d.points.push_back(v);}
 if(d.title.empty()||d.points.empty())return false;d.valid=true;out=d;return true;
}
// What the speaker reads: verse, title, truth, the bullets, the action, the prayer.
inline std::string devotionalSpeech(const Devotional& d){
 if(!d.valid)return "";
 std::string s="Today's devotional, "+d.title+". "+bibleRefLabel(d.ref,false)+" says: "+d.verse+" "+d.truth+" ";
 for(const auto& p:d.points){s+=p;if(!p.empty()&&p.back()!='.'&&p.back()!='!'&&p.back()!='?')s+=".";s+=" ";}
 s+="Here is something to do today: "+d.apply+" Let's pray. "+d.prayer;return s;
}
}
