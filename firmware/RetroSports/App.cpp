#include <Arduino.h>
#include <WiFi.h>
#include <NetworkClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <Wire.h>
#include <esp_netif_sntp.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <sys/time.h>
#include <atomic>
#include "Core.h"
#include "ScoreJson.h"
#include "Render.h"
#include "EPD_3in97.h"
#include "Certificates.h"
#include "Portal.h"
#include "NetworkBody.h"
#include "Audio.h"
#include "Voice.h"
#include <mbedtls/base64.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include "Version.h"
using namespace retro;
static UI ui;
static GFXcanvas1 canvas(800,480);
static Renderer renderer(canvas);
static Preferences prefs;
static WebServer server(80);
static DNSServer dns;
static QueueHandle_t inputQueue,requestQueue,resultQueue;
static bool dirty=true,fsOK=false,requestBusy=false,followToday=true;
static uint32_t nextFetch=0,apStarted=0,connectedAt=0,lastRtcWrite=0,nextReconnect=0,reconnectDelay=5000;
static int backgroundLeague=0,partialCount=0;
// Display pipeline: the panel refreshes in the background while input keeps
// flowing; `shown` mirrors the frame last sent so identical frames are skipped.
static uint8_t* shown=nullptr;
static bool panelPending=false;
static uint32_t panelStartedAt=0,lastKeyAt=0,lastFullRefresh=0,detailFetchedAt=0;
static String lastDateSaved;
// Voice: the OpenRouter key lives in NVS (set through the setup page or USB), never in source.
static String voiceKey,ttsVoice="alloy";
static QueueHandle_t voiceQueue;
static uint32_t voiceDemoRelease=0;
static bool voiceRecorded=false,speakReplies=true,darkMode=false,nightSleep=true;
static int lastSleepDay=-1; // yday of the last automatic bedtime, so a BOOT wake at night stays awake
// Idle sleep: after IDLE_MS without a press the page is left on the panel behind an
// "asleep" banner and the board deep-sleeps; any button wakes it, and a timer wakes it
// every REFRESH_S to refresh that page and sleep again. State survives in RTC memory.
static const uint32_t IDLE_MS=10*60*1000UL;static const int REFRESH_S=15*60;
RTC_DATA_ATTR static int rtcTab=0;RTC_DATA_ATTR static int rtcSleeps=0;
static bool refreshWake=false;static int refreshPending=0,refreshMaskBits=0;static uint32_t refreshStarted=0;
// While awake, a league is polled every minute only if it has a game on; idle leagues wait 15 minutes.
static bool leagueLive[4]={false,false,false,false};static uint32_t leagueFetched[4]={0,0,0,0};static const uint32_t IDLE_POLL_MS=15*60*1000UL;
static int pmuRead(uint8_t reg);
static int batteryPercent(){int v=pmuRead(0xA4);return v<0?-1:(v&0x7f);} // AXP2101 fuel gauge
static bool inNight(){if(!nightSleep||!ui.clockValid)return false;time_t now=time(nullptr);tm t{};localtime_r(&now,&t);return t.tm_hour>=23||t.tm_hour<6||(t.tm_hour==6&&t.tm_min<30);}
static void armWakeButtons(){ // rocker up/down/press and BOOT, all active-low
 for(int pin:{0,4,5,6}){rtc_gpio_init((gpio_num_t)pin);rtc_gpio_set_direction((gpio_num_t)pin,RTC_GPIO_MODE_INPUT_ONLY);rtc_gpio_pullup_en((gpio_num_t)pin);rtc_gpio_pulldown_dis((gpio_num_t)pin);}
 esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH,ESP_PD_OPTION_ON);
 esp_sleep_enable_ext1_wakeup((1ULL<<0)|(1ULL<<4)|(1ULL<<5)|(1ULL<<6),ESP_EXT1_WAKEUP_ANY_LOW);
}
// Seconds until the next 6:30 local; used as the timer wake for the nightly sleep.
static int64_t secondsUntilWake(){
 time_t now=time(nullptr);tm t{};localtime_r(&now,&t);t.tm_hour=6;t.tm_min=30;t.tm_sec=0;t.tm_isdst=-1;time_t wake=mktime(&t);
 if(wake<=now){t.tm_mday+=1;t.tm_isdst=-1;wake=mktime(&t);}return wake-now;
}
// Dark mode is a whole-frame inversion applied after rendering, so every page,
// logo and highlight flips together and the panel's partial updates stay consistent.
static void applyTheme(uint8_t* frame){if(darkMode)for(int i=0;i<48000;i++)frame[i]=~frame[i];}
static bool connectionPending=false,connectionFailed=false,finishSetup=false;
static uint32_t connectionStarted=0,beginAt=0,finishAt=0;
static std::atomic<int> disconnectReason(0);
static std::atomic<bool> timeSynced(false);
static bool sntpStarted=false;
static String ssid,password,tz;
static const char* zones[]={"EST5EDT,M3.2.0,M11.1.0","CST6CDT,M3.2.0,M11.1.0","MST7MDT,M3.2.0,M11.1.0","PST8PDT,M3.2.0,M11.1.0","UTC0"};
static const char* zoneLabels[]={"Eastern","Central","Mountain","Pacific","UTC"};
struct FetchRequest {int league;char date[9];bool feed;char team[12];char game[16];bool standings;char scope[8];bool weather,update,devotional,devoSync;uint8_t book,chapter,verse;char key[96];};
struct FetchResult {Snapshot snapshot;GameDetail detail;VoiceReply voice;Standings standings;Weather weather;int league;std::string date,team,game;bool feed=false,ok=false,stored=false,more=false,isDetail=false,isVoice=false,voiceInterim=false,isStandings=false,isWeather=false,isUpdate=false,installed=false,isDevotional=false,isDevoSync=false;std::string note;Devotional devotional;int fetched=0,code=0;};
struct VoiceJob {uint8_t* pcm;size_t len;char favorites[400];char key[96];bool speak,warm;char say[1100];char weather[700];char voice[12];};
struct Key {int button;ButtonEvent event;};
SET_LOOP_TASK_STACK_SIZE(16*1024); // VoiceJob copies and the renderer need more than the 8 KB default
static std::string readFileText(const std::string& path);static int dayOfYear(); // Bible helpers, defined with the reader below

// Vendor ESP-IDF driver powers the panel through PMU ALDO3 at 3.3 V.
// Preserve every unrelated regulator and all battery charging settings.
static int pmuRead(uint8_t reg){
 Wire.beginTransmission(0x34);Wire.write(reg);if(Wire.endTransmission()!=0)return -1;
 if(Wire.requestFrom(0x34,1)!=1)return -1;return Wire.read();
}
static bool pmuWrite(uint8_t reg,uint8_t value){Wire.beginTransmission(0x34);Wire.write(reg);Wire.write(value);return Wire.endTransmission()==0;}
// PMU LDO enable bits in register 0x90: ALDO1 (unused pull-up rail), ALDO2 (codec analog + microphone), ALDO3 (panel).
static bool pmuRail(uint8_t bit,bool on){int e=pmuRead(0x90);if(e<0)return false;return pmuWrite(0x90,on?(e|bit):(e&~bit));}
static const int EPD_PINS[]={EPD_SCK_PIN,EPD_MOSI_PIN,EPD_CS_PIN,EPD_RST_PIN,EPD_DC_PIN};
// Everything the ESP32 can switch off before deep sleep: codec standby and its
// analog rail, the amplifier, and the panel lines driven low and held so an
// unpowered panel is not back-fed through its protection diodes.
static void powerDownForSleep(){
 audio::sleep();pmuRail(0x02,false);pmuRail(0x01,false);
 for(int pin:EPD_PINS){pinMode(pin,OUTPUT);digitalWrite(pin,LOW);gpio_hold_en((gpio_num_t)pin);}
 pinMode(EPD_BUSY_PIN,INPUT);gpio_deep_sleep_hold_en();
 Serial.printf("SLEEP rails=%02x\n",pmuRead(0x90));
}
// After a wake: release the held pins and bring the codec rail back before the drivers initialize.
static void powerUpFromSleep(){
 gpio_deep_sleep_hold_dis();for(int pin:EPD_PINS)gpio_hold_dis((gpio_num_t)pin);
 // Only DCDC1 (3.3 V), ALDO2 (codec analog) and ALDO3 (panel) are wired on this board; the PMU
 // powers up with every converter enabled, so switch the unconnected ones off for good.
 pmuWrite(0x80,0x01);pmuWrite(0x90,0x06);pmuWrite(0x91,0x00);delay(10);
 Serial.printf("PMU rails dcdc=%02x ldo=%02x/%02x\n",pmuRead(0x80),pmuRead(0x90),pmuRead(0x91));
}
static bool panelPower(bool on){
 int enables=pmuRead(0x90),voltage=pmuRead(0x94);if(enables<0||voltage<0){Serial.println("PANEL POWER: PMU unavailable");return false;}
 bool ok=(!on||pmuWrite(0x94,(voltage&0xe0)|28))&&pmuWrite(0x90,on?(enables|4):(enables&~4));
 Serial.printf("PANEL POWER on=%d ok=%d enable=%02x voltage=%02x\n",on,ok,pmuRead(0x90),pmuRead(0x94));delay(20);return ok;
}
static uint8_t bcd(int n){return ((n/10)<<4)|(n%10);}
static int dec(uint8_t n){return (n>>4)*10+(n&15);}
static bool readRtc(){
 Wire.beginTransmission(0x51);Wire.write(4);if(Wire.endTransmission()!=0)return false;
 if(Wire.requestFrom(0x51,7)!=7)return false;
 uint8_t r[7];for(auto& v:r)v=Wire.read();if(r[0]&0x80)return false;
 // Marker ensures the RTC was set in UTC by this app, not by factory firmware.
 if(!prefs.getBool("rtcUTC",false))return false;
 int y=2000+dec(r[6]),m=dec(r[5]&31),d=dec(r[3]&63);
 if(y<2025||m<1||m>12||d<1||d>31)return false;
 timeval t{(time_t)utcEpoch(y,m,d,dec(r[2]&63),dec(r[1]&127),dec(r[0]&127)),0};settimeofday(&t,nullptr);return true;
}
static void writeRtc(){
 time_t now=time(nullptr);tm b{};gmtime_r(&now,&b);
 Wire.beginTransmission(0x51);Wire.write(0);Wire.write(0);if(Wire.endTransmission()!=0)return;
 Wire.beginTransmission(0x51);Wire.write(4);
 for(int v:{b.tm_sec,b.tm_min,b.tm_hour,b.tm_mday,b.tm_wday,b.tm_mon+1,b.tm_year-100})Wire.write(bcd(v));
 if(Wire.endTransmission()==0)prefs.putBool("rtcUTC",true);
}
// Day views cache per league/date, the recent-games feed one file per league,
// team pages one file per league/team.
static String cachePath(int league,const std::string& date,bool feed,const std::string& team){
 if(!team.empty())return "/t"+String(league)+"_"+String(team.c_str())+".json";
 return feed?"/f"+String(league)+".json":"/s"+String(league)+"_"+String(date.c_str())+".json";
}
static bool loadCache(int league,const std::string& date,bool feed,const std::string& team,Snapshot& s){
 if(!fsOK)return false;File f=LittleFS.open(cachePath(league,date,feed,team),"r");if(!f||f.size()>200000)return false;
 JsonDocument d;auto err=deserializeJson(d,f);f.close();Snapshot candidate;
 if(err||!decodeCache(d,candidate)||candidate.league!=league)return false;
 if(!team.empty()){if(candidate.team!=team)return false;}
 else if(feed?candidate.span<=0:candidate.date!=date)return false;
 s=std::move(candidate);return true;
}
// Standings cache: one file per league, refreshed when older than six hours.
static String standingsPath(int league,const std::string& scope){return "/g"+String(league)+(scope.empty()?String(""):"_"+String(scope.c_str()))+".json";}
static bool loadStandings(int league,const std::string& scope,Standings& out){
 if(!fsOK)return false;File f=LittleFS.open(standingsPath(league,scope),"r");if(!f||f.size()>200000)return false;
 JsonDocument d;auto err=deserializeJson(d,f);f.close();Standings s;if(err||!decodeStandingsCache(d,s)||s.league!=league||s.scope!=scope)return false;out=std::move(s);return true;
}
static bool saveStandings(const Standings& s){
 if(!fsOK)return false;JsonDocument d;encodeStandings(s,d);File f=LittleFS.open(standingsPath(s.league,s.scope),"w");if(!f)return false;serializeJson(d,f);f.close();return true;
}
static void pruneCaches(){
 File root=LittleFS.open("/");std::vector<std::string> days,teams;
 for(File f=root.openNextFile();f;f=root.openNextFile()){String n=f.name();if(!n.endsWith(".json"))continue;if(n.startsWith("s"))days.emplace_back(n.c_str());else if(n.startsWith("t"))teams.emplace_back(n.c_str());}
 if(days.size()>56){std::sort(days.begin(),days.end(),[](auto&a,auto&b){return a.substr(3,8)<b.substr(3,8);});for(size_t i=0;i<days.size()-56;i++)LittleFS.remove(("/"+days[i]).c_str());}
 if(teams.size()>24){std::sort(teams.begin(),teams.end());for(size_t i=0;i<teams.size()-24;i++)LittleFS.remove(("/"+teams[i]).c_str());}
}
static bool saveCache(const Snapshot& s){
 if(!fsOK)return false;JsonDocument d;encodeCache(s,d);String path=cachePath(s.league,s.date,s.span>0,s.team),tmp=path+".tmp";
 File f=LittleFS.open(tmp,"w");if(!f)return false;size_t n=serializeJson(d,f);f.flush();f.close();
 if(n!=measureJson(d)){LittleFS.remove(tmp);return false;}
 if(!LittleFS.rename(tmp,path)){LittleFS.remove(tmp);return false;}pruneCaches();return true;
}
static void saveFavorites(){
 JsonDocument d;auto a=d.to<JsonArray>();for(auto& f:ui.favorites){auto o=a.add<JsonObject>();o["league"]=f.league;o["id"]=f.id;o["name"]=f.name;}
 String s;serializeJson(d,s);prefs.putString("favorites",s);
}
static void loadFavorites(){
 JsonDocument d;if(deserializeJson(d,prefs.getString("favorites","[]")))return;
 for(JsonObject f:d.as<JsonArray>()){int l=f["league"]|-1;if(l>=0&&l<4&&!js(f["id"]).empty()&&ui.favorites.size()<40)ui.favorites.push_back({l,js(f["id"]),clean(js(f["name"]))});}
}
static void toggleFavorite(const Team& team){
 for(auto i=ui.favorites.begin();i!=ui.favorites.end();++i)if(i->league==ui.league&&i->id==team.id){ui.favorites.erase(i);saveFavorites();return;}
 if(ui.favorites.size()<40){ui.favorites.push_back({ui.league,team.id,team.name});saveFavorites();}
}
// One ESPN request decoded into a snapshot. Games are kept for [start, date]
// (start empty = exactly `date`) since some leagues return a week, or all of
// them for a team schedule.
static bool fetchBody(const String& url,ScoreBody& body,int& code){
 if(WiFi.status()!=WL_CONNECTED||time(nullptr)<=1700000000)return false;
 NetworkClientSecure client;client.setCACert(SCORE_ROOTS);client.setHandshakeTimeout(10);
 HTTPClient http;http.useHTTP10(true);http.setTimeout(10000);http.setConnectTimeout(8000);
 bool ok=false;
 if(http.begin(client,url)){
  http.addHeader("Accept-Encoding","identity");
  code=http.GET();
  if(code==200){int received=http.writeToStream(&body);ok=received>=0&&!body.failed;if(!ok)Serial.printf("SCORE transport error=%d limited=%d\n",received,body.failed);}
  http.end();
 }
 return ok;
}
static bool fetchJson(const String& url,const char* what,int league,const std::string& date,const std::string& start,bool allDates,Snapshot& out,int& code){
 ScoreBody body;if(!fetchBody(url,body,code))return false;
 ScoreJsonAllocator allocator;JsonDocument document(&allocator),filter;makeScoreFilter(filter);
 auto error=deserializeJson(document,(const char*)body.data,body.length,DeserializationOption::Filter(filter),DeserializationOption::NestingLimit(30));
 bool ok=!error&&decodeScores(document,league,date,time(nullptr),out,start,allDates);
 Serial.printf("SCORE %s error=%s events=%u kept=%u valid=%d bytes=%u heap=%u\n",what,error.c_str(),(unsigned)document["events"].size(),(unsigned)out.games.size(),ok,(unsigned)body.length,ESP.getFreeHeap());
 return ok;
}
// Matchup detail: the scoreboard for the game's day, keeping one event's extras.
// Football uses the game summary (leaders per team, team totals); other
// leagues take the extras from that day's scoreboard.
static bool fetchDetail(int league,const std::string& date,const std::string& game,GameDetail& out,int& code){
 const bool football=league==1||league==3;
 String url="https://site.api.espn.com/apis/site/v2/sports/";url+=leagues[league].path;
 if(football){url+="/summary?event=";url+=game.c_str();}else{url+="/scoreboard?dates=";url+=date.c_str();url+="&limit=200";}
 ScoreBody body;if(!fetchBody(url,body,code))return false;
 ScoreJsonAllocator allocator;JsonDocument document(&allocator),filter;if(football)makeSummaryFilter(filter);else makeDetailFilter(filter);
 auto error=deserializeJson(document,(const char*)body.data,body.length,DeserializationOption::Filter(filter),DeserializationOption::NestingLimit(30));
 bool ok=!error&&(football?decodeSummary(document,game,league,out):decodeDetail(document,game,league,out));
 Serial.printf("DETAIL game=%s error=%s ok=%d stats=%u line=%u bytes=%u heap=%u\n",game.c_str(),error.c_str(),ok,(unsigned)out.stats.size(),(unsigned)out.awayLine.size(),(unsigned)body.length,ESP.getFreeHeap());
 return ok;
}
static bool fetchDay(int league,const std::string& day,const std::string& date,const std::string& start,Snapshot& out,int& code){
 String url="https://site.api.espn.com/apis/site/v2/sports/";url+=leagues[league].path;url+="/scoreboard?dates=";url+=day.c_str();url+="&limit=200";if(league==3)url+="&groups=80";
 return fetchJson(url,("day="+day).c_str(),league,date,start,false,out,code);
}
// Team page: the season schedule (results carry scores) plus the team's games
// from the league feed, which covers leagues whose schedule feed omits results.
static bool fetchTeam(int league,const std::string& team,const std::string& date,Snapshot& out,int& code){
 String url="https://site.api.espn.com/apis/site/v2/sports/";url+=leagues[league].path;url+="/teams/";url+=team.c_str();url+="/schedule";
 if(!fetchJson(url,("team="+team).c_str(),league,date,"",true,out,code))return false;
 Snapshot feed;if(loadCache(league,date,true,"",feed))for(const auto& g:feed.games){
  if(!matches(g,team))continue;bool have=false;
  for(auto& h:out.games)if(h.id==g.id){have=true;if(h.away.conf.empty())h.away.conf=g.away.conf;if(h.home.conf.empty())h.home.conf=g.home.conf;} // the schedule feed lacks conference ids
  if(!have)out.games.push_back(g);
 }
 out.team=team;out.date=date;trimUpcoming(out);return true;
}
// The voice task keeps one TLS connection to OpenRouter across an interaction:
// opened while the user is still talking, reused for the speech request, then
// closed. Each handshake costs about a second on this chip.
static NetworkClientSecure* voiceClient=nullptr;
static HTTPClient* voiceHttp=nullptr; // long-lived: HTTPClient's destructor would close the socket
static bool voiceConnect(){
 if(!voiceClient){voiceClient=new NetworkClientSecure;voiceClient->setCACert(SCORE_ROOTS);voiceClient->setHandshakeTimeout(20);voiceClient->setTimeout(20);}
 if(!voiceHttp)voiceHttp=new HTTPClient;
 if(voiceClient->connected())return true;
 uint32_t t=millis();bool ok=voiceClient->connect("openrouter.ai",443);Serial.printf("VOICE connect ok=%d %lums\n",ok,(unsigned long)(millis()-t));return ok;
}
static void voiceDisconnect(){if(voiceClient)voiceClient->stop();}
static void voiceHeaders(HTTPClient& http,const char* key){
 http.setReuse(true);http.setTimeout(45000);http.setConnectTimeout(12000);
 http.addHeader("Content-Type","application/json");http.addHeader("Authorization",String("Bearer ")+key);
 http.addHeader("HTTP-Referer","https://pixel-league.local");http.addHeader("X-Title","Pixel League");
}
// One OpenRouter request: WAV audio plus the saved scores as context, JSON back.
static VoiceReply askVoice(const VoiceJob& job){
 VoiceReply r;
 if(WiFi.status()!=WL_CONNECTED){r.error="VOICE NEEDS INTERNET";return r;}
 if(!job.key[0]){r.error="ADD A VOICE KEY IN WI-FI SETUP";return r;}
 Snapshot feeds[4];const Snapshot* fp[4];const std::string today=localDate(time(nullptr));
 for(int l=0;l<4;l++)fp[l]=loadCache(l,today,true,"",feeds[l])?&feeds[l]:nullptr;
 std::vector<Favorite> favs;{std::string all=job.favorites;size_t p=0;while(p<all.size()){size_t q=all.find('\n',p);std::string item=all.substr(p,q==std::string::npos?std::string::npos:q-p);size_t bar=item.find('|');if(bar!=std::string::npos)favs.push_back({atoi(item.substr(0,bar).c_str()),"",item.substr(bar+1)});if(q==std::string::npos)break;p=q+1;}}
 const std::string ctx=voiceContext(fp,favs,time(nullptr),job.weather);
 const std::string head="{\"model\":\""+std::string(VOICE_MODEL)+"\",\"response_format\":{\"type\":\"json_object\"},\"max_tokens\":300,\"temperature\":0.2,\"messages\":[{\"role\":\"system\",\"content\":\""+jsonEscape(voiceSystemPrompt())+"\"},{\"role\":\"user\",\"content\":[{\"type\":\"text\",\"text\":\""+jsonEscape(ctx+"\nThe spoken request is attached as audio.")+"\"},{\"type\":\"input_audio\",\"input_audio\":{\"format\":\"wav\",\"data\":\"";
 const std::string tail="\"}}]}]}";
 const size_t wavLen=44+job.len,b64Max=4*((wavLen+2)/3)+4;
 uint8_t* wav=(uint8_t*)heap_caps_malloc(wavLen,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 uint8_t* body=(uint8_t*)heap_caps_malloc(head.size()+b64Max+tail.size(),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 if(!wav||!body){heap_caps_free(wav);heap_caps_free(body);r.error="OUT OF MEMORY";return r;}
 audio::wavHeader(wav,job.len);memcpy(wav+44,job.pcm,job.len);
 size_t olen=0;memcpy(body,head.data(),head.size());
 mbedtls_base64_encode(body+head.size(),b64Max,&olen,wav,wavLen);
 memcpy(body+head.size()+olen,tail.data(),tail.size());const size_t bodyLen=head.size()+olen+tail.size();
 heap_caps_free(wav);
 int code=0;ScoreBody resp;uint32_t tConnect=0,tPost=0,tBody=0;
 // A cold TLS handshake or DNS miss can fail once; try twice before giving up.
 for(int attempt=0;attempt<2&&code<=0;attempt++){
  if(attempt){delay(1500);voiceDisconnect();}
  uint32_t t0=millis();voiceConnect();tConnect=millis()-t0;
  HTTPClient& http=*voiceHttp;
  if(http.begin(*voiceClient,"https://openrouter.ai/api/v1/chat/completions")){
   voiceHeaders(http,job.key);
   uint32_t t1=millis();code=http.POST(body,bodyLen);tPost=millis()-t1;
   uint32_t t2=millis();if(code>0)http.writeToStream(&resp);else Serial.printf("VOICE attempt %d failed: %d\n",attempt+1,code);tBody=millis()-t2;
   http.end();
  }
 }
 Serial.printf("VOICE timing connect=%lums post=%lums body=%lums\n",(unsigned long)tConnect,(unsigned long)tPost,(unsigned long)tBody);
 heap_caps_free(body);
 if(resp.length&&!resp.failed){JsonDocument doc;if(!deserializeJson(doc,(const char*)resp.data,resp.length))r=parseVoiceReply(doc,fp);}
 if(!r.ok&&r.error.empty())r.error=code==200?"COULDN'T UNDERSTAND THAT":code==401?"VOICE KEY WAS REJECTED":code>0?"VOICE SERVICE ERROR "+std::to_string(code):"COULD NOT REACH THE VOICE SERVICE";
 Serial.printf("VOICE http=%d sent=%u got=%u ok=%d heard=\"%s\" answer=\"%s\" nav=%d/%s err=%s heap=%u\n",code,(unsigned)bodyLen,(unsigned)resp.length,r.ok,r.heard.c_str(),r.answer.c_str(),r.league,r.teamAbbr.c_str(),r.error.c_str(),ESP.getFreeHeap());
 return r;
}
// Reads the answer aloud: the speech streams from OpenRouter as SSE lines
// (de-chunked here since HTTP/1.1 keeps the connection reusable) and each
// audio chunk is decoded and played as it arrives.
struct ChunkedReader {
 NetworkClient& c;bool chunked;long remaining;bool done=false;uint32_t last=millis();
 int read(uint8_t* out,size_t cap){
  if(done)return 0;
  if(chunked&&remaining<=0){
   String line=c.readStringUntil('\n');line.trim();if(line.isEmpty()){line=c.readStringUntil('\n');line.trim();}
   remaining=strtol(line.c_str(),nullptr,16);if(remaining<=0){done=true;return 0;}
  }
  if(!c.available()){if(!c.connected()||millis()-last>20000){done=true;return 0;}delay(2);return -1;}
  size_t want=chunked?std::min(cap,(size_t)remaining):cap;int n=c.read(out,want);
  if(n>0){last=millis();if(chunked)remaining-=n;if(!chunked&&remaining>0&&(remaining-=n)<=0)done=true;}
  return n;
 }
};
static std::atomic<int> speakState(0); // 0 idle, 1 fetching, 2 playing; the loop mirrors it into ui.speaking
static void speakAnswer(const std::string& answer,const char* key,const char* voice){
 speakState=1;struct Done{~Done(){speakState=0;}} done;
 const std::string body=ttsRequestBody(answer,voice);int code=0;uint32_t tStart=millis(),tConnect=0,tPost=0,tFirst=0;size_t sse=0,pcmTotal=0;std::string transcript;
 uint32_t t0=millis();voiceConnect();tConnect=millis()-t0;
 HTTPClient& http=*voiceHttp;
 if(http.begin(*voiceClient,"https://openrouter.ai/api/v1/chat/completions")){
  voiceHeaders(http,key);
  uint32_t t1=millis();code=http.POST((uint8_t*)body.data(),body.size());tPost=millis()-t1;
  if(code==200){
   NetworkClient* stream=http.getStreamPtr();const int size=http.getSize();
   ChunkedReader reader{*stream,size<0,size<0?0:(long)size};
   const size_t lineCap=96*1024,pcmCap=64*1024;
   char* line=(char*)heap_caps_malloc(lineCap,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);uint8_t* pcm=(uint8_t*)heap_caps_malloc(pcmCap,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
   static uint8_t buf[2048];size_t lineLen=0;bool playing=false,finished=false;
   if(line&&pcm)while(!finished){
    int n=reader.read(buf,sizeof(buf));if(n==0)break;if(n<0)continue;sse+=n;
    for(int i=0;i<n;i++){char ch=(char)buf[i];
     if(ch!='\n'){if(lineLen<lineCap-1)line[lineLen++]=ch;continue;}
     line[lineLen]=0;
     if(lineLen>6&&!strncmp(line,"data: ",6)){
      if(!strcmp(line+6,"[DONE]")){finished=true;}
      else{size_t got=0;transcript+=sseAudio(line,lineLen,[&](const char* b64,size_t len){got=base64Decode(b64,len,pcm,pcmCap);});
       if(got){if(!playing){tFirst=millis()-tStart;playing=audio::playBegin(TTS_RATE);if(playing)speakState=2;}if(playing)audio::playWrite(pcm,got&~size_t(1));pcmTotal+=got;}}
     }
     lineLen=0;
    }
   }
   if(playing)audio::playEnd();
   heap_caps_free(line);heap_caps_free(pcm);
  }
  http.end();
 }
 Serial.printf("SPEAK http=%d connect=%lums post=%lums firstAudio=%lums sse=%u pcm=%u (%.1fs) total=%lums said=\"%s\"\n",code,(unsigned long)tConnect,(unsigned long)tPost,(unsigned long)tFirst,(unsigned)sse,(unsigned)pcmTotal,pcmTotal/(2.0*TTS_RATE),(unsigned long)(millis()-tStart),transcript.c_str());
}
// Web-backed follow-up for a fact the model could not answer from memory.
static std::string lookupFact(const std::string& question,const char* key){
 const std::string body=lookupRequestBody(question,stamp(time(nullptr)));int code=0;ScoreBody resp;uint32_t t=millis();
 voiceConnect();HTTPClient& http=*voiceHttp;
 if(http.begin(*voiceClient,"https://openrouter.ai/api/v1/chat/completions")){voiceHeaders(http,key);code=http.POST((uint8_t*)body.data(),body.size());if(code>0)http.writeToStream(&resp);http.end();}
 std::string answer;
 if(code==200&&resp.length&&!resp.failed){JsonDocument doc;if(!deserializeJson(doc,(const char*)resp.data,resp.length))answer=parseLookupReply(doc);}
 Serial.printf("LOOKUP http=%d %lums answer=\"%s\"\n",code,(unsigned long)(millis()-t),answer.c_str());
 return answer;
}
static void voiceTask(void*){
 VoiceJob job;
 for(;;){if(xQueueReceive(voiceQueue,&job,portMAX_DELAY)!=pdTRUE)continue;
  if(job.warm){if(WiFi.status()==WL_CONNECTED)voiceConnect();continue;}
  if(job.say[0]&&!job.pcm){speakAnswer(job.say,job.key,job.voice);voiceDisconnect();continue;} // read-aloud request (devotional, USB speech test)
  FetchResult* result=new FetchResult;result->isVoice=true;result->voice=askVoice(job);heap_caps_free(job.pcm);
  if(result->voice.ok&&(result->voice.action==VoiceAction::Fact||result->voice.action==VoiceAction::Answer)&&result->voice.lookup){ // the model flagged a present-day question: ask the web
   FetchResult* interim=new FetchResult;interim->isVoice=true;interim->voiceInterim=true;xQueueSend(resultQueue,&interim,portMAX_DELAY);
   std::string found=lookupFact(result->voice.heard,job.key);if(!found.empty())result->voice.answer=found;
  }
  bool speak=job.speak&&result->voice.ok&&(result->voice.action==VoiceAction::Answer||result->voice.action==VoiceAction::Fact||(result->voice.action==VoiceAction::OpenWeather&&!result->voice.answer.empty()));std::string answer=result->voice.answer;
  if(result->voice.ok&&result->voice.action==VoiceAction::OpenDevotional&&result->voice.read&&job.say[0]){answer=job.say;speak=job.speak;} // the devotional text rides along in the job
  if(result->voice.ok&&result->voice.action==VoiceAction::OpenBible&&result->voice.read){ // "read me John 3:16": the words come from the chapter file, not the model
   const BibleRef r=result->voice.daily?verseOfDay(dayOfYear()):result->voice.bible;
   if(r.valid()){answer=bibleSpeakText(bibleVerses(readFileText(biblePath(r.book,r.chapter))),r);speak=job.speak&&!answer.empty();}
  }
  xQueueSend(resultQueue,&result,portMAX_DELAY); // the screen shows the answer while the speech is fetched
  if(speak)speakAnswer(answer,job.key,job.voice);
  voiceDisconnect();
 }
}
static bool fetchStandings(int league,const std::string& scope,Standings& out,int& code){
 String url="https://site.api.espn.com/apis/v2/sports/";url+=leagues[league].path;url+="/standings?";if(league==3)url+="group="+String(scope.c_str());else url+="level=3";
 ScoreBody body;uint32_t t=millis();if(!fetchBody(url,body,code))return false;
 ScoreJsonAllocator allocator;JsonDocument document(&allocator),filter;makeStandingsFilter(filter);
 auto error=deserializeJson(document,(const char*)body.data,body.length,DeserializationOption::Filter(filter),DeserializationOption::NestingLimit(30));
 bool ok=!error&&decodeStandings(document,league,time(nullptr),out,scope);
 Serial.printf("STANDINGS league=%d error=%s ok=%d groups=%u bytes=%u %lums heap=%u\n",league,error.c_str(),ok,(unsigned)out.groups.size(),(unsigned)body.length,(unsigned long)(millis()-t),ESP.getFreeHeap());
 return ok;
}
// Weather: the location comes once from the public IP and stays in NVS; the forecast is one small request.
static bool fetchWeather(Weather& out,int& code){
 String lat=prefs.getString("lat",""),lon=prefs.getString("lon",""),city=prefs.getString("city",""),query=prefs.getString("locq","");
 if(lat.isEmpty()||lon.isEmpty()){ // a typed ZIP or city wins; otherwise the public IP's rough location
  const bool typed=!query.isEmpty();
  ScoreBody body;if(!fetchBody(typed?geocodeUrl(query.c_str()).c_str():"https://ipinfo.io/json",body,code)){Serial.printf("WEATHER location http=%d\n",code);return false;}
  JsonDocument d;if(deserializeJson(d,(const char*)body.data,body.length))return false;std::string la,lo,ci;
  if(!(typed?decodeGeocode(d,la,lo,ci):decodeLocation(d,la,lo,ci))){Serial.printf("WEATHER location not found for %s\n",typed?query.c_str():"ip");return false;}
  lat=la.c_str();lon=lo.c_str();city=ci.c_str();prefs.putString("lat",lat);prefs.putString("lon",lon);prefs.putString("city",city);Serial.printf("WEATHER location saved city=%s from %s\n",city.c_str(),typed?"typed":"ip");
 }
 ScoreBody body;if(!fetchBody(weatherUrl(lat.c_str(),lon.c_str()).c_str(),body,code))return false;
 JsonDocument d;if(deserializeJson(d,(const char*)body.data,body.length))return false;
 out.city=city.c_str();return decodeWeather(d,time(nullptr),out);
}
// --- Over-the-air updates from the latest GitHub release (version.json, then RetroSports.bin into the inactive app slot).
static bool newerVersion(const std::string& a,const std::string& b){ // a > b, "1.5.0" style
 int x[3]={0,0,0},y[3]={0,0,0};sscanf(a.c_str(),"%d.%d.%d",&x[0],&x[1],&x[2]);sscanf(b.c_str(),"%d.%d.%d",&y[0],&y[1],&y[2]);
 for(int i=0;i<3;i++){if(x[i]!=y[i])return x[i]>y[i];}return false;
}
static bool fetchUpdate(std::string& note,bool& installed){
 installed=false;if(WiFi.status()!=WL_CONNECTED){note="NO WI-FI";return false;}
 NetworkClientSecure client;client.setCACert(SCORE_ROOTS);client.setHandshakeTimeout(15);
 HTTPClient http;http.setTimeout(20000);http.setConnectTimeout(10000);http.setReuse(false);
 const char* headers[]={"Location"};http.collectHeaders(headers,1);
 // GitHub answers with two redirects (latest -> tagged -> asset host); chase them by hand and log each hop.
 auto open=[&](const String& first)->int{String url=first;int code=0;
  for(int hop=0;hop<6;hop++){if(!http.begin(client,url))return -1;code=http.GET();Serial.printf("UPDATE hop %d http=%d %s\n",hop,code,url.substring(0,90).c_str());
   if(code==301||code==302||code==303||code==307||code==308){url=http.header("Location");http.end();if(url.isEmpty())return -2;continue;}
   return code;}
  return -3;};
 std::string version;
 int code=open(String(OTA_RELEASE_URL)+"version.json");
 if(code!=200){http.end();note="NO RELEASE FOUND ("+std::to_string(code)+")";return false;}
 {String body=http.getString();Serial.printf("UPDATE version.json: %s\n",body.substring(0,80).c_str());JsonDocument d;if(deserializeJson(d,body)){http.end();note="BAD VERSION FILE";return false;}version=d["version"].as<const char*>()?d["version"].as<const char*>():"";}
 http.end();
 Serial.printf("UPDATE latest=%s running=%s\n",version.c_str(),FIRMWARE_VERSION);
 if(version.empty()||!newerVersion(version,FIRMWARE_VERSION)){note="UP TO DATE";return true;}
 code=open(String(OTA_RELEASE_URL)+"RetroSports.bin");const int size=http.getSize();
 if(code!=200||size<=0){http.end();note="DOWNLOAD FAILED ("+std::to_string(code)+")";return false;}
 if(!Update.begin(size)){http.end();note="NO ROOM FOR UPDATE";return false;}
 const uint32_t t0=millis();const size_t written=Update.writeStream(http.getStream());http.end();
 if(written!=(size_t)size||!Update.end()||!Update.isFinished()){note="INSTALL FAILED: "+std::string(Update.errorString());Update.abort();Serial.printf("UPDATE failed written=%u/%d err=%s\n",(unsigned)written,size,Update.errorString());return false;}
 Serial.printf("UPDATE installed v%s (%d bytes, %lums)\n",version.c_str(),size,(unsigned long)(millis()-t0));
 note="INSTALLED V"+version+" - RESTARTING";installed=true;return true;
}
// --- Devotional library: every day's file from the repo mirrored on LittleFS, synced by content hash
// (one index request, then only the days that changed, at most 40 per pass).
static bool syncDevotionals(int& fetched,int& code){
 fetched=0;NetworkClientSecure client;client.setCACert(SCORE_ROOTS);client.setHandshakeTimeout(15);
 HTTPClient http;http.setReuse(true);http.setTimeout(15000);http.setConnectTimeout(10000);
 if(!http.begin(client,DEVOTIONAL_BASE_URL "index.json"))return false;
 code=http.GET();if(code!=200){http.end();Serial.printf("DEVOSYNC index http=%d\n",code);return false;}
 JsonDocument remote;const bool ok=!deserializeJson(remote,http.getString());http.end();if(!ok)return false;
 JsonDocument local;deserializeJson(local,readFileText("/devo/index.json"));LittleFS.mkdir("/devo");
 int total=0;
 for(JsonPairConst kv:remote.as<JsonObjectConst>()){const std::string day=kv.key().c_str();const char* h=kv.value().as<const char*>();if(!h||day.size()!=5)continue;total++;
  const char* lh=local[day].as<const char*>();const std::string path="/devo/"+day+".json";
  if(lh&&!strcmp(lh,h)&&LittleFS.exists(path.c_str()))continue;
  if(fetched>=40)break;
  if(!http.begin(client,String(DEVOTIONAL_BASE_URL)+day.c_str()+".json"))continue;
  const int c=http.GET();if(c==200){const String body=http.getString();File f=LittleFS.open(path.c_str(),"w");if(f){f.print(body);f.close();local[day]=h;fetched++;}}
  http.end();
 }
 std::string out;serializeJson(local,out);File f=LittleFS.open("/devo/index.json","w");if(f){f.print(out.c_str());f.close();}
 Serial.printf("DEVOSYNC fetched=%d of %d listed\n",fetched,total);return true;
}
// --- Today's devotional: the local file if the library has it, else one chat request with the verse and its neighbours.
static bool fetchDevotional(const FetchRequest& req,Devotional& out,int& code){
 BibleRef ref;ref.book=req.book;ref.chapter=req.chapter;ref.verse=req.verse;if(!ref.valid())return false;
 const auto verses=bibleVerses(readFileText(biblePath(ref.book,ref.chapter)));if(ref.verse<1||ref.verse>(int)verses.size())return false;
 tm now{};time_t t=time(nullptr);localtime_r(&t,&now);
 { // the library's file for today comes first (no network needed); without one the model writes it (when there is a key)
  const std::string body=readFileText(devotionalLocalPath(now.tm_mon+1,now.tm_mday));
  if(!body.empty()){
   auto verseFor=[&](const BibleRef& r){const auto vs=bibleVerses(readFileText(biblePath(r.book,r.chapter)));return r.verse>=1&&r.verse<=(int)vs.size()?vs[r.verse-1]:std::string();};
   if(decodeDevotionalFile(body,now.tm_yday,ref,verses[ref.verse-1],verseFor,out)){JsonDocument idx;deserializeJson(idx,readFileText("/devo/index.json"));const char* h=idx[devotionalDayKey(now.tm_mon+1,now.tm_mday)].as<const char*>();out.hash=h?h:"";
    Serial.printf("DEVOTIONAL file %02d-%02d: %s\n",now.tm_mon+1,now.tm_mday,out.title.c_str());return true;}
   Serial.println("DEVOTIONAL file did not parse");}
 }
 if(!req.key[0]||req.feed)return false; // feed=true here means "file only": the model already wrote today's
 std::string context;for(int v=std::max(1,ref.verse-6);v<=std::min((int)verses.size(),ref.verse+6)&&context.size()<1400;v++)context+=std::to_string(v)+" "+verses[v-1]+" ";
 NetworkClientSecure client;client.setCACert(SCORE_ROOTS);client.setHandshakeTimeout(15);
 HTTPClient http;http.setTimeout(45000);http.setConnectTimeout(10000);
 if(!http.begin(client,"https://openrouter.ai/api/v1/chat/completions"))return false;
 http.addHeader("Content-Type","application/json");http.addHeader("Authorization",String("Bearer ")+req.key);
 const std::string body=devotionalRequestBody(ref,verses[ref.verse-1],context);code=http.POST((uint8_t*)body.data(),body.size());
 if(code!=200){http.end();Serial.printf("DEVOTIONAL http=%d\n",code);return false;}
 const String reply=http.getString();http.end();JsonDocument d;const bool parsed=!deserializeJson(d,reply);
 const bool ok=parsed&&parseDevotional(d,ref,verses[ref.verse-1],now.tm_yday,out);
 if(!ok)Serial.printf("DEVOTIONAL unparsed: %s\n",reply.substring(0,160).c_str());
 return ok;
}
static void networkTask(void*){
 FetchRequest req;
 for(;;){if(xQueueReceive(requestQueue,&req,portMAX_DELAY)!=pdTRUE)continue;
  FetchResult* result=new FetchResult;result->league=req.league;result->date=req.date;result->feed=req.feed;result->team=req.team;result->game=req.game;
  if(req.devoSync){
   result->isDevoSync=true;result->ok=syncDevotionals(result->fetched,result->code);
  }else if(req.devotional){
   result->isDevotional=true;result->ok=fetchDevotional(req,result->devotional,result->code);
  }else if(req.update){
   result->isUpdate=true;result->ok=fetchUpdate(result->note,result->installed);
  }else if(req.weather){
   result->isWeather=true;result->ok=fetchWeather(result->weather,result->code);
  }else if(req.standings){
   result->isStandings=true;result->ok=fetchStandings(req.league,req.scope,result->standings,result->code);if(result->ok)result->stored=saveStandings(result->standings);
  }else if(req.game[0]){
   result->isDetail=true;result->ok=fetchDetail(req.league,req.date,req.game,result->detail,result->code);
  }else if(req.team[0]){
   result->ok=fetchTeam(req.league,req.team,req.date,result->snapshot,result->code);
  }else if(!req.feed){
   result->ok=fetchDay(req.league,req.date,req.date,"",result->snapshot,result->code);
  }else{
   // Recent-games feed: refresh today (and yesterday while it has games still
   // going), backfill a few uncovered days, and merge into the saved feed.
   const int span=feedDays[req.league];const std::string end=req.date,start=shiftDate(end,-span);
   Snapshot feed;loadCache(req.league,end,true,"",feed);feed.league=req.league;
   bool any=false;
   for(const auto& day:feedMissing(feed,start,end)){
    Snapshot part;if(!fetchDay(req.league,day,end,start,part,result->code))break;
    mergeFeed(feed,part,day,start,end,span);any=true;
   }
   if(any){result->ok=true;result->more=feedHasGaps(feed,start,end);result->snapshot=std::move(feed);}
  }
  // Flash writes stay off the UI task so a save never delays a key press.
  if(result->ok&&!result->isDetail&&!result->isStandings)result->stored=saveCache(result->snapshot);
  xQueueSend(resultQueue,&result,portMAX_DELAY);
 }
}
static void inputTask(void*){
 const int pins[]={4,6,5,0};Button buttons[4];for(int p:pins)pinMode(p,INPUT_PULLUP);
 for(;;){uint32_t now=millis();for(int i=0;i<4;i++){auto event=buttons[i].update(digitalRead(pins[i])==LOW,now,i==2);if(event!=ButtonEvent::None){Key k{i,event};xQueueSend(inputQueue,&k,0);}}vTaskDelay(pdMS_TO_TICKS(10));}
}
static bool sameView(int league,const std::string& date,bool feed,const std::string& team){
 return league==ui.league&&team==ui.filter&&(!team.empty()||(date==ui.date&&feed==ui.feed));
}
static void requestScores(int league,const std::string& date,bool feed,const std::string& team=""){
 if(requestBusy||!ui.online||!ui.clockValid||!validDate(date)||team.size()>=sizeof(FetchRequest::team))return;
 FetchRequest r{};r.league=league;r.feed=feed;snprintf(r.date,sizeof(r.date),"%s",date.c_str());snprintf(r.team,sizeof(r.team),"%s",team.c_str());
 // Only redraw for "updating" when there is nothing to show yet; otherwise the
 // page would flash twice a minute for a status line.
 if(xQueueSend(requestQueue,&r,0)==pdTRUE){requestBusy=true;if(sameView(league,date,feed,team)){ui.fetching=true;if(ui.snapshot.games.empty())dirty=true;}}
}
static uint32_t nextWeather=0;
// A new ZIP or city: forget the coordinates so the next weather fetch geocodes it.
static void setLocation(const String& query){prefs.putString("locq",query);prefs.remove("lat");prefs.remove("lon");prefs.remove("city");nextWeather=0;Serial.printf("WEATHER location set to \"%s\"\n",query.c_str());}
static bool updateBusy=false,autoUpdateCheck=false;static uint32_t restartAt=0;
// Manual: show the update page while it runs. Automatic (6:30 wake): silent unless a newer build installs.
static void requestUpdate(bool manual){
 if(requestBusy||!ui.online||!ui.clockValid||updateBusy)return;FetchRequest r{};r.update=true;
 if(xQueueSend(requestQueue,&r,0)==pdTRUE){requestBusy=true;updateBusy=true;if(manual){ui.page=Page::Update;ui.updateNote="CHECKING FOR A NEW VERSION...";ui.selected=0;dirty=true;}}
}
static void requestWeather(){
 if(requestBusy||!ui.online||!ui.clockValid)return;FetchRequest r{};r.weather=true;
 if(xQueueSend(requestQueue,&r,0)==pdTRUE)requestBusy=true;
}
static void requestDetail(const Game& g){
 const std::string date=gameDate(g);
 if(requestBusy||!ui.online||!ui.clockValid||!validDate(date)||g.id.empty()||g.id.size()>=sizeof(FetchRequest::game))return;
 FetchRequest r{};r.league=ui.league;snprintf(r.date,sizeof(r.date),"%s",date.c_str());snprintf(r.game,sizeof(r.game),"%s",g.id.c_str());
 if(xQueueSend(requestQueue,&r,0)==pdTRUE)requestBusy=true;
}
static void requestStandings(int league,const std::string& scope){
 if(requestBusy||!ui.online||!ui.clockValid||(league==3&&scope.empty()))return;
 FetchRequest r{};r.league=league;r.standings=true;snprintf(r.date,sizeof(r.date),"%s",localDate(time(nullptr)).c_str());snprintf(r.scope,sizeof(r.scope),"%s",scope.c_str());
 if(xQueueSend(requestQueue,&r,0)==pdTRUE){requestBusy=true;ui.standingsLoading=true;}
}
// The college conference a team belongs to, from any saved game it played.
static std::string teamConference(const std::string& teamId){
 for(const auto& g:ui.snapshot.games){if(g.away.id==teamId&&!g.away.conf.empty())return g.away.conf;if(g.home.id==teamId&&!g.home.conf.empty())return g.home.conf;}
 return "";
}
// Make sure the standings for this league (and, for college, conference) are in memory; fetch when missing or stale.
static std::string standingsScopeWanted;static int standingsLeagueWanted=-1;
static void ensureStandings(int league,const std::string& scope){
 if(ui.standings.league!=league||ui.standings.scope!=scope){ui.standings=Standings{};loadStandings(league,scope,ui.standings);}
 standingsLeagueWanted=league;standingsScopeWanted=scope; // the loop keeps trying while the network task is busy
}
// Called from the loop: fetch the wanted standings when missing or older than six hours.
static void standingsUpkeep(){
 if(standingsLeagueWanted<0||ui.standingsLoading||requestBusy)return;
 const bool relevant=ui.page==Page::Standings||(ui.page==Page::Games&&!ui.filter.empty());if(!relevant){standingsLeagueWanted=-1;return;}
 if(standingsLeagueWanted==3&&ui.page==Page::Games){std::string c=teamConference(ui.filter);if(!c.empty())standingsScopeWanted=c;} // a college team's conference is known once its games load
 const bool stale=ui.standings.league!=standingsLeagueWanted||ui.standings.scope!=standingsScopeWanted||time(nullptr)-ui.standings.updated>6*3600;
 if(stale)requestStandings(standingsLeagueWanted,standingsScopeWanted);
}
// Resolve which groups the standings page shows: a team's division and conference, or a named group.
static void aimStandings(){
 ui.standingsGroup=ui.standingsAlt=-1;if(ui.standings.league!=ui.league)return;
 if(!ui.standingsWant.empty()){int g=findGroup(ui.standings,ui.standingsWant);if(g>=0){ui.standingsGroup=g;if(ui.standings.groups[g].parent>=0)ui.standingsAlt=ui.standings.groups[g].parent;}return;}
 int d,c;teamGroups(ui.standings,ui.filter,d,c);if(d>=0){ui.standingsGroup=d;ui.standingsAlt=c;}else if(c>=0)ui.standingsGroup=c;
}
static void openStandings(int league,const std::string& teamId,const std::string& want,const std::string& scope,bool conferenceFirst){
 ui.league=league;ui.filter=teamId;ui.standingsWant=want;ui.page=Page::Standings;ui.selected=0;
 ensureStandings(league,league==3?scope:"");aimStandings();
 if(conferenceFirst&&ui.standingsAlt>=0)std::swap(ui.standingsGroup,ui.standingsAlt);
 dirty=true;
}
static void loadView(){
 ui.snapshot=Snapshot{};ui.snapshot.league=ui.league;ui.snapshot.date=ui.date;ui.snapshot.span=ui.feed?feedDays[ui.league]:0;ui.snapshot.team=ui.filter;
 loadCache(ui.league,ui.date,ui.feed,ui.filter,ui.snapshot);ui.failed=false;ui.fetching=false;ui.selected=0;ui.listPage=0;nextFetch=0;dirty=true;
 if(!ui.snapshot.games.empty()&&(ui.feed||!ui.filter.empty()))ui.selected=1; // feeds and team pages start on the first game
}
// Home shows the newest games across all leagues from the cached feeds.
// Home data: the ALL tab groups recent games from every feed; a league tab
// loads that league's feed into the snapshot and shows it like the scoreboard.
// Latest games across the four saved feeds, packed into `avail` pixels.
static void loadRecent(int avail){
 const std::string today=ui.clockValid?localDate(time(nullptr)):ui.date;
 Snapshot feeds[4];const Snapshot* fp[4]={nullptr,nullptr,nullptr,nullptr};
 for(int l=0;l<4;l++)fp[l]=loadCache(l,today,true,"",feeds[l])?&feeds[l]:nullptr;
 ui.recent=recentGamesGrouped(fp,avail);
}
// A league tab's feed into ui.snapshot (shared by the launcher and the sports home).
static void loadLeagueTab(){
 const std::string today=ui.clockValid?localDate(time(nullptr)):ui.date;
 ui.recent.clear();ui.league=ui.tab-1;ui.filter.clear();ui.feed=true;ui.date=today;followToday=true;
 ui.snapshot=Snapshot{};ui.snapshot.league=ui.league;ui.snapshot.date=today;ui.snapshot.span=feedDays[ui.league];
 loadCache(ui.league,today,true,"",ui.snapshot);ui.failed=false;ui.fetching=false;
}
static void buildLauncher(){if(ui.tab==0)loadRecent(780-launcherScoresTop(ui));else loadLeagueTab();}
// Reads the saved feeds to plan the next refresh wake: how long to sleep and which leagues to fetch.
static int64_t planRefresh(int& mask){
 const std::string today=ui.clockValid?localDate(time(nullptr)):ui.date;const int64_t now=time(nullptr);
 Snapshot feeds[4];const Snapshot* fp[4]={nullptr,nullptr,nullptr,nullptr};
 for(int l=0;l<4;l++)fp[l]=loadCache(l,today,true,"",feeds[l])?&feeds[l]:nullptr;
 mask=refreshMask(fp,now);return refreshDelay(fp,now);
}
static void goLauncher(int sel=-1){ui.page=Page::Launcher;ui.selected=sel<0?LAUNCH_TAB0+ui.tab:sel;ui.filter.clear();ui.detailFromHome=false;buildLauncher();dirty=true;}
static void buildRecent(){
 const std::string today=ui.clockValid?localDate(time(nullptr)):ui.date;
 if(ui.tab==0){
  loadRecent(768-68);if(ui.selected>=HOME_ALL_ROW+(int)ui.recent.size())ui.selected=0;
 }else{
  loadLeagueTab();
  if(ui.selected>=HOME_ROW+(int)visibleGames(ui).size())ui.selected=ui.tab;
 }
}
// --- Bible: chapters come off LittleFS, the reading position lives in NVS.
static std::string readFileText(const std::string& path){
 File f=LittleFS.open(path.c_str(),"r");if(!f)return "";std::string s;s.reserve(f.size()+1);
 char buf[512];while(f.available()){int n=f.readBytes(buf,sizeof(buf));if(n<=0)break;s.append(buf,n);}f.close();return s;
}
static int dayOfYear(){tm lt{};time_t now=time(nullptr);localtime_r(&now,&lt);return lt.tm_yday;}
static void saveBiblePos(){prefs.putUInt("bible",((uint32_t)ui.bible.book<<16)|((uint32_t)ui.bible.chapter<<8)|(uint32_t)std::min(ui.bible.page,255));}
static bool loadBibleChapter(int book,int chapter){
 BibleRef r;r.book=book;r.chapter=chapter;if(!r.valid())return false;
 const std::string text=fsOK?readFileText(biblePath(book,chapter)):"";
 ui.bible.book=book;ui.bible.chapter=chapter;ui.bible.verse=0;ui.bible.page=0;
 ui.bible.pages=text.empty()?std::vector<BiblePage>{}:paginateBible(bibleVerses(text),readWidth,456,READ_LINES);
 Serial.printf("BIBLE %s pages=%u bytes=%u\n",bibleRefLabel(r).c_str(),(unsigned)ui.bible.pages.size(),(unsigned)text.size());
 return !ui.bible.pages.empty();
}
// Opens the reader at a reference (book 0 = the saved position); a verse lands on its page, highlighted.
static void openBible(const BibleRef& r){
 if(r.valid()){if(ui.bible.book!=r.book||ui.bible.chapter!=r.chapter||ui.bible.pages.empty())loadBibleChapter(r.book,r.chapter);ui.bible.page=r.verse>0?biblePageOf(ui.bible.pages,r.verse):0;ui.bible.verse=r.verse;}
 else if(ui.bible.pages.empty())loadBibleChapter(ui.bible.book,ui.bible.chapter);
 ui.page=Page::Bible;ui.selected=0;saveBiblePos();dirty=true;
}
// Next or previous page, rolling into the neighbouring chapter (and book) at either end.
static void bibleStep(int step){
 BibleView& b=ui.bible;b.verse=0;const int pages=std::max(1,(int)b.pages.size());
 if(step>0&&b.page+1<pages)b.page++;
 else if(step<0&&b.page>0)b.page--;
 else{int book=b.book,chapter=b.chapter+step;
  if(chapter<1){book=book>1?book-1:BIBLE_BOOKS;chapter=bibleBooks[book-1].chapters;}
  else if(chapter>bibleBooks[book-1].chapters){book=book<BIBLE_BOOKS?book+1:1;chapter=1;}
  loadBibleChapter(book,chapter);b.page=step>0?0:std::max(0,(int)b.pages.size()-1);}
 saveBiblePos();dirty=true;
}
// Opening a page with a READ ALOUD button starts the TLS handshake early, so the press itself is quicker.
static void warmVoice(){if(voiceKey.isEmpty()||!ui.online)return;static VoiceJob job;memset(&job,0,sizeof(job));job.warm=true;xQueueSend(voiceQueue,&job,0);}
static uint32_t nextDevotional=0;static bool devotionalPending=false;
// Today's entry in the local library index (hash), refreshed after a sync and at a new day.
static std::string devoHashToday;static bool devoHaveToday=false;
static void refreshDevoHash(){tm lt{};time_t t=time(nullptr);localtime_r(&t,&lt);JsonDocument idx;deserializeJson(idx,readFileText("/devo/index.json"));const char* h=idx[devotionalDayKey(lt.tm_mon+1,lt.tm_mday)].as<const char*>();devoHashToday=h?h:"";devoHaveToday=LittleFS.exists(devotionalLocalPath(lt.tm_mon+1,lt.tm_mday).c_str());}
// Stale: nothing for today, a model-written one while the library now has today's file, or a file that changed since.
static bool devotionalStale(){if(!ui.devotional.valid||ui.devotional.day!=dayOfYear())return true;if(ui.devotional.fromFile)return ui.devotional.hash!=devoHashToday;return devoHaveToday;}
static bool devotionalWantsFile(){return false;}
static uint32_t nextDevoSync=0;static bool devoSyncPending=false;static const int64_t DEVO_SYNC_S=6*3600;
static bool devoSyncDue(){return time(nullptr)-prefs.getLong64("devosync",0)>DEVO_SYNC_S;}
static void requestDevoSync(){if(requestBusy||!ui.online||!ui.clockValid)return;FetchRequest r{};r.devoSync=true;if(xQueueSend(requestQueue,&r,0)==pdTRUE){requestBusy=true;nextDevoSync=millis()+1800000;}}
static void requestDevotional(){
 if(requestBusy||!ui.online||!ui.clockValid||!ui.votd.valid())return;
 FetchRequest r{};r.devotional=true;r.feed=devotionalWantsFile();r.book=ui.votd.book;r.chapter=ui.votd.chapter;r.verse=ui.votd.verse;snprintf(r.key,sizeof(r.key),"%s",voiceKey.c_str());
 if(xQueueSend(requestQueue,&r,0)==pdTRUE){requestBusy=true;ui.devotionalLoading=!ui.devotional.valid;nextDevotional=millis()+(r.feed?7200000:600000);if(ui.page==Page::Devotional)dirty=true;}
}
static void loadVerseOfDay(){
 const BibleRef r=verseOfDay(ui.clockValid?dayOfYear():0);const auto verses=bibleVerses(fsOK?readFileText(biblePath(r.book,r.chapter)):"");
 if(r.verse>=1&&r.verse<=(int)verses.size()){ui.votd=r;ui.votdText=verses[r.verse-1];}else{ui.votd=BibleRef{};ui.votdText.clear();}
}
static void goHome(){ui.page=Page::Home;ui.selected=ui.tab;ui.filter.clear();ui.detailFromHome=false;buildRecent();dirty=true;}
// Open a team's page, remembering where to return.
static void openTeam(const Team& team,Page from,const std::string& gameId){
 ui.origin={from,ui.league,ui.date,gameId,ui.feed};
 ui.filter=team.id;ui.filterName=team.name;ui.page=Page::Games;loadView();ensureStandings(ui.league,ui.league==3?teamConference(team.id):"");
}
// Rocker held: start listening, or explain why voice cannot run right now.
static void startVoice(){
 ui.voice=VoiceState::Error;voiceRecorded=false;ui.voiceNote.clear();
 if(!ui.online)ui.voiceNote="VOICE NEEDS INTERNET";
 else if(!ui.clockValid)ui.voiceNote="WAITING FOR THE CLOCK";
 else if(voiceKey.isEmpty())ui.voiceNote="ADD A VOICE KEY IN WI-FI SETUP";
 else if(!audio::available())ui.voiceNote="MICROPHONE NOT FOUND";
 else if(!audio::startRecording())ui.voiceNote="COULD NOT START THE MIC";
 else{ui.voice=VoiceState::Listening;VoiceJob warm{};warm.warm=true;xQueueSend(voiceQueue,&warm,0);} // handshake while they talk
 dirty=true;
}
// Rocker released: send the clip, or return to the previous page.
static void finishVoice(){
 if(ui.voice==VoiceState::Listening){
  uint8_t* pcm=nullptr;size_t n=audio::stopRecording(&pcm);voiceRecorded=true;ui.voiceNote.clear();
  if(n<audio::SAMPLE_RATE*2*2/5){heap_caps_free(pcm);ui.voice=VoiceState::Error;ui.voiceNote="TOO SHORT - HOLD WHILE YOU TALK";}
  else{
   auto cut=trimSilence((const int16_t*)pcm,n/2,audio::SAMPLE_RATE);
   if(cut.first)memmove(pcm,pcm+cut.first*2,cut.second*2);
   Serial.printf("VOICE clip %u -> %u bytes after trimming silence\n",(unsigned)n,(unsigned)(cut.second*2));n=cut.second*2;
   static VoiceJob job;memset(&job,0,sizeof(job));job.pcm=pcm;job.len=n;job.speak=speakReplies;snprintf(job.key,sizeof(job.key),"%s",voiceKey.c_str());snprintf(job.voice,sizeof(job.voice),"%s",ttsVoice.c_str());snprintf(job.weather,sizeof(job.weather),"%s",weatherSpeech(ui.weather).c_str());if(ui.devotional.valid)snprintf(job.say,sizeof(job.say),"%s",devotionalSpeech(ui.devotional).c_str());
   std::string favs;for(const auto& f:ui.favorites){std::string line=std::to_string(f.league)+"|"+f.name+"\n";if(favs.size()+line.size()>=sizeof(job.favorites))break;favs+=line;}
   snprintf(job.favorites,sizeof(job.favorites),"%s",favs.c_str());
   if(xQueueSend(voiceQueue,&job,0)==pdTRUE)ui.voice=VoiceState::Thinking;else{heap_caps_free(pcm);ui.voice=VoiceState::Error;ui.voiceNote="STILL BUSY WITH THE LAST QUESTION";}
  }
 }else if(ui.voice==VoiceState::Error&&!voiceRecorded){ui.page=ui.returnPage;ui.voice=VoiceState::Idle;}
 dirty=true;
}
static void stopAP(){dns.stop();server.stop();WiFi.softAPdisconnect(true);WiFi.mode(WIFI_STA);ui.ap=false;dirty=true;}
static String portalPage(){
 String s=F("<!doctype html><html><meta name='viewport' content='width=device-width,initial-scale=1'><title>Pixel League Wi-Fi</title><style>body{background:#f5f3e8;color:#182019;font:18px monospace;max-width:480px;margin:40px auto;padding:24px}h1{border-bottom:6px solid;padding-bottom:16px}input,select,button{box-sizing:border-box;width:100%;font:inherit;padding:14px;margin:8px 0 20px;border:2px solid;background:white}button{background:#182019;color:white}p{line-height:1.5}</style><h1>PIXEL LEAGUE</h1><p>Connect your scoreboard to a 2.4 GHz Wi-Fi network.</p><form action='/save' method='post'><input type='hidden' name='epoch' id='epoch'><label>Wi-Fi name</label><input name='ssid' maxlength='32' required autocomplete='off'><label>Wi-Fi password</label><input name='password' type='password' maxlength='63'><label>Timezone</label><select name='zone'>");
 for(int i=0;i<5;i++){s+="<option value='"+String(i)+"'"+(tz==zones[i]?" selected":"")+">"+zoneLabels[i]+"</option>";}
 s+=F("</select><label>Voice key (OpenRouter, optional)</label><input name='key' maxlength='128' autocomplete='off' placeholder='sk-or-v1-...'><label>ZIP or city for weather (optional)</label><input name='loc' maxlength='40' placeholder='44077'><button>CONNECT SCOREBOARD</button></form><p>Your password is saved only on the board. If connection fails, return here to retry. Press BOOT on the board to browse saved scores.</p><script>document.getElementById('epoch').value=Math.floor(Date.now()/1000);</script></html>");return s;
}
static void startAP(){
 connectionFailed=false;finishSetup=false;
 if(ui.ap)stopAP();WiFi.mode(WIFI_AP_STA);
 char suffix[5];snprintf(suffix,sizeof(suffix),"%04X",(unsigned)(ESP.getEfuseMac()&0xffff));ui.apName=std::string("PixelLeague-")+suffix;
 char secret[13];snprintf(secret,sizeof(secret),"play%08x",(unsigned)esp_random());ui.apPass=secret;
 if(!WiFi.softAP(ui.apName.c_str(),ui.apPass.c_str())){ui.notice="Could not start setup. Restart and retry.";return;}
 dns.start(53,"*",WiFi.softAPIP());
 server.on("/",HTTP_GET,[]{server.send(200,"text/html",portalPage());});
 server.on("/status",HTTP_GET,[]{
  JsonDocument d;
  bool online=WiFi.status()==WL_CONNECTED&&!connectionPending;
  d["state"]=online?"connected":connectionFailed?"failed":"connecting";
  d["clockReady"]=ui.clockValid;
  d["message"]="Check the Wi-Fi name and password. Use a 2.4 GHz network and keep the board near your router.";
  String body;serializeJson(d,body);server.sendHeader("Cache-Control","no-store");server.send(200,"application/json",body);
 });
 server.on("/done",HTTP_POST,[]{server.send(200,"text/plain","Setup complete");finishSetup=true;finishAt=millis()+1000;});
 server.on("/save",HTTP_POST,[]{
  String s=server.arg("ssid"),p=server.arg("password");int zone=server.arg("zone").toInt();
  if(s.length()==0||s.length()>32||p.length()>63||zone<0||zone>4){server.send(400,"text/plain","Check the network name, password, and timezone.");return;}
  ssid=s;password=p;tz=zones[zone];prefs.putString("ssid",ssid);prefs.putString("pass",password);prefs.putString("tz",tz);
  String key=server.arg("key");key.trim();if(key.length()){voiceKey=key;prefs.putString("orkey",voiceKey);}
  String loc=server.arg("loc");loc.trim();if(loc.length())setLocation(loc);
  setenv("TZ",tz.c_str(),1);tzset();
  // Bootstrap UTC from the user's phone if NTP is blocked by the local network.
  int64_t browserEpoch=strtoll(server.arg("epoch").c_str(),nullptr,10);
  if(browserEpoch>=1700000000LL&&browserEpoch<4102444800LL){timeval tv{(time_t)browserEpoch,0};settimeofday(&tv,nullptr);ui.clockValid=true;writeRtc();}
  // Send the page before changing radio channels. Allow a full connection attempt.
  connectionPending=true;connectionFailed=false;connectionStarted=millis();beginAt=millis()+750;
  nextReconnect=millis()+45000;disconnectReason=0;
  ui.notice="Connecting... check this screen in a moment.";dirty=true;
  server.send(200,"text/html",CONNECTION_PAGE);
 });
 server.onNotFound([]{server.sendHeader("Location","http://192.168.4.1/",true);server.send(302,"text/plain","");});server.begin();
 ui.ap=true;apStarted=millis();ui.notice="Setup stays open for 10 minutes.";ui.page=Page::Wifi;ui.selected=0;dirty=true;
}
static void sleepScreen(bool scheduled=false){
 ui.now=time(nullptr);renderer.sleepVerse(ui);
 canvas.fillRect(0,0,480,52,0);canvas.setTextColor(1);canvas.setTextSize(2);
 const std::string top="ASLEEP - PRESS ANY BUTTON";canvas.setCursor(240-int(top.size())*6,5);canvas.print(top.c_str());
 std::string line=scheduled?"BACK AT 6:30 AM":clockLabel(ui.now);int b=batteryPercent();if(b>=0)line+=" - BATTERY "+std::to_string(b)+"%";
 canvas.setCursor(240-int(line.size())*6,28);canvas.print(line.c_str());
 applyTheme(canvas.getBuffer());
 if(scheduled&&ui.clockValid)esp_sleep_enable_timer_wakeup((uint64_t)secondsUntilWake()*1000000ULL);
 EPD_3IN97_WaitIdle();panelPending=false;
 EPD_3IN97_Init();EPD_3IN97_Display_Base(canvas.getBuffer());EPD_3IN97_Sleep();panelPower(false);
 WiFi.disconnect(true);powerDownForSleep();armWakeButtons();delay(100);esp_deep_sleep_start();
}
// Leave the current page on the panel with a banner explaining it is asleep, then sleep
// until a button, the 15-minute refresh, or 6:30 am when it is night.
static void idleSleep(){
 ui.battery=batteryPercent();ui.now=time(nullptr);renderer.sleepVerse(ui);
 canvas.fillRect(0,0,480,52,0);canvas.setTextColor(1);canvas.setTextSize(2);
 const std::string top="PRESS ANY BUTTON FOR LATEST";canvas.setCursor(240-int(top.size())*6,5);canvas.print(top.c_str());
 std::string line="ASLEEP - "+clockLabel(ui.now);int b=batteryPercent();if(b>=0)line+=" - BATTERY "+std::to_string(b)+"%";
 canvas.setCursor(240-int(line.size())*6,28);canvas.print(line.c_str());
 applyTheme(canvas.getBuffer());EPD_3IN97_WaitIdle();panelPending=false;EPD_3IN97_Display_Partial(canvas.getBuffer(),shown);
 if(shown)memcpy(shown,canvas.getBuffer(),48000);
 EPD_3IN97_Sleep();panelPower(false);rtcTab=ui.tab;rtcSleeps++;
 int mask=0;const int64_t planned=planRefresh(mask);const int64_t secs=inNight()?secondsUntilWake():planned;
 Serial.printf("SLEEP idle: wake in %llds (night=%d, next refresh %llds, leagues 0x%x) battery=%d%%\n",(long long)secs,inNight(),(long long)planned,mask,b);
 WiFi.disconnect(true);esp_sleep_enable_timer_wakeup((uint64_t)secs*1000000ULL);powerDownForSleep();armWakeButtons();delay(100);esp_deep_sleep_start();
}
static void goBack(){
 if(ui.page==Page::Games&&!ui.filter.empty()){ // leave a team page the way it was entered
  Origin o=ui.origin;ui.filter.clear();ui.filterName.clear();
  if(o.page==Page::Detail){ui.league=o.league;ui.date=o.date;ui.feed=o.feed;loadView();
   auto it=std::find_if(ui.snapshot.games.begin(),ui.snapshot.games.end(),[&](const Game& g){return g.id==o.gameId;});
   if(it!=ui.snapshot.games.end()){ui.gameIndex=it-ui.snapshot.games.begin();ui.page=Page::Detail;}
  }else if(o.page==Page::Favorites){ui.page=Page::Favorites;ui.selected=0;}
  else goHome();
  dirty=true;return;
 }
 switch(ui.page){
 case Page::Games:goHome();break;
 case Page::Detail:if(ui.detailFromHome){const int keep=ui.gameIndex;const std::string id=ui.snapshot.games.size()>(size_t)keep?ui.snapshot.games[keep].id:"";goHome();
   if(ui.tab>0){const GamesLayout L=layoutGames(ui);for(size_t p=0;p<L.ids.size();p++)if(ui.snapshot.games[L.ids[p]].id==id){ui.selected=p+HOME_ROW;break;}}
   else for(size_t i=0;i<ui.recent.size();i++)if(ui.recent[i].game.id==id){ui.selected=HOME_ALL_ROW+i;break;}
   break;}
  ui.page=Page::Games;ui.selected=0;break;
 case Page::Date:ui.page=Page::Games;ui.selected=0;break;
 case Page::Wifi:ui.page=Page::Settings;ui.selected=0;break;
 case Page::Favorites:ui.page=Page::Settings;ui.selected=4;break;
 case Page::Standings:{Origin o=ui.origin;ui.standingsWant.clear();
  if(o.page==Page::Games&&!ui.filter.empty()){ui.origin={Page::Home,o.league,o.date,"",o.feed};ui.league=o.league;ui.date=o.date;ui.feed=o.feed;ui.page=Page::Games;loadView();}
  else goHome();break;}
 case Page::Voice:ui.page=ui.returnPage==Page::Voice?Page::Launcher:ui.returnPage;ui.voice=VoiceState::Idle;if(ui.page==Page::Home)buildRecent();else if(ui.page==Page::Launcher)buildLauncher();break;
 case Page::Home:goLauncher(LAUNCH_TAB0+ui.tab);break;
 case Page::Settings:goLauncher(LAUNCH_GEAR);break;
 case Page::BibleHome:goLauncher(LAUNCH_BIBLE);break;
 case Page::Devotional:goLauncher(LAUNCH_BIBLE);break;
 case Page::Weather:goLauncher(LAUNCH_WEATHER);break;
 case Page::Update:if(!updateBusy&&!restartAt){ui.page=Page::Settings;ui.selected=7;}break;
 case Page::Launcher:break;
 case Page::Bible:ui.page=Page::BibleHome;ui.selected=0;break;
 case Page::BibleBooks:ui.page=Page::Bible;ui.selected=0;break;
 case Page::BibleChapters:ui.page=Page::BibleBooks;break;
 default:goHome();break;
 }dirty=true;
}
static void keyAction(const Key& k){
 if(k.button==2&&k.event==ButtonEvent::Hold){if(ui.page!=Page::Voice){ui.returnPage=ui.page;ui.page=Page::Voice;}startVoice();return;}
 if(k.button==2&&k.event==ButtonEvent::ReleaseHold){if(ui.page==Page::Voice)finishVoice();return;}
 if(k.event!=ButtonEvent::Click)return;
 if(ui.page==Page::Voice&&(ui.voice==VoiceState::Listening||ui.voice==VoiceState::Thinking))return;
 Serial.printf("KEY %d\n",k.button);
 if(k.button==3){goBack();return;}
 if(ui.page==Page::Voice){ // an answer can open the league or team it was about
  if(k.button==2&&ui.voice==VoiceState::Answer&&(ui.voiceLeague>=0||!ui.voiceTeamId.empty())){
   const std::string today=ui.clockValid?localDate(time(nullptr)):ui.date;ui.voice=VoiceState::Idle;
   ui.league=ui.voiceLeague>=0?ui.voiceLeague:ui.league;ui.date=today;ui.feed=true;followToday=true;ui.filter.clear();
   if(!ui.voiceTeamId.empty()){Team t;t.id=ui.voiceTeamId;t.name=ui.voiceTeamName;openTeam(t,Page::Home,"");}
   else{ui.page=Page::Games;loadView();}
   dirty=true;
  }
  return;
 }
 if(k.button<2){int step=k.button==0?-1:1;
  // An empty day has nothing to move between; a press must still do something visible.
  if(ui.page==Page::Games&&ui.filter.empty()&&visibleGames(ui).empty()){ui.page=Page::Date;ui.dateOffset=step;dirty=true;return;}
  if(ui.page==Page::Games&&ui.feed&&ui.filter.empty()&&!visibleGames(ui).empty()){ // page bar at the top: enter/leave the current page
   const GamesLayout L=layoutGames(ui);
   if(ui.selected==0&&step==1){ui.selected=L.firstGame(ui.listPage)+1;dirty=true;return;}
   if(ui.selected>0&&step==-1){int p=L.pageOf(ui.selected-1);if(ui.selected-1==L.firstGame(p)){ui.listPage=p;ui.selected=0;dirty=true;return;}}
   if(ui.selected>0&&step==1&&ui.selected==(int)L.ids.size()){ui.listPage=0;ui.selected=0;dirty=true;return;} // past the last game: back to the bar, page 1
  }
  if(ui.page==Page::Standings){if(ui.standingsAlt>=0)std::swap(ui.standingsGroup,ui.standingsAlt);dirty=true;return;}
  if(ui.page==Page::Launcher){ui.selected=(ui.selected+step+LAUNCH_COUNT)%LAUNCH_COUNT;
   if(ui.selected>=LAUNCH_TAB0&&ui.selected<LAUNCH_GEAR&&ui.selected-LAUNCH_TAB0!=ui.tab){ui.tab=ui.selected-LAUNCH_TAB0;ui.listPage=0;buildLauncher();} // landing on a tab switches the list
   dirty=true;return;}
  if(ui.page==Page::Weather)return;
  if(ui.page==Page::Bible){bibleStep(step);return;}
  if(ui.page==Page::BibleBooks){ui.bible.pick=(ui.bible.pick+step+BIBLE_BOOKS)%BIBLE_BOOKS;dirty=true;return;}
  if(ui.page==Page::BibleChapters){const int n=bibleBooks[std::max(1,std::min(BIBLE_BOOKS,ui.bible.pickBook))-1].chapters;ui.bible.pick=(ui.bible.pick+step+n)%n;dirty=true;return;}
  if(ui.page==Page::Date)ui.dateOffset=std::max(-30,std::min(30,ui.dateOffset+step));
  if(ui.page==Page::Home){ // tabs: up/down switch; press enters the list. In the list: up from the top returns to the bar/tabs.
   const bool league=ui.tab>0;const GamesLayout L=league?layoutGames(ui):GamesLayout{};
   const int rows=league?(int)L.ids.size():(int)ui.recent.size(),firstRow=league?HOME_ROW:HOME_ALL_ROW;
   if(ui.selected<=HOME_GEAR){int t=(ui.selected+step+HOME_TABS+1)%(HOME_TABS+1);ui.selected=t;if(t<HOME_TABS&&t!=ui.tab){ui.tab=t;ui.listPage=0;buildRecent();}dirty=true;return;}
   if(league&&ui.selected==HOME_PREV){ui.selected=step>0?HOME_NEXT:ui.tab;dirty=true;return;}                       // PREV: down -> NEXT, up -> tabs
   if(league&&ui.selected==HOME_NEXT){if(step>0){int f=L.firstGame(ui.listPage);if(f>=0)ui.selected=f+HOME_ROW;}else ui.selected=HOME_PREV;dirty=true;return;} // NEXT: down -> rows, up -> PREV
   const int pos=ui.selected-firstRow;
   if(step<0){if(league){int p=L.pageOf(pos);if(pos==L.firstGame(p)){ui.listPage=p;ui.selected=HOME_NEXT;}else ui.selected--;}else ui.selected=pos==0?ui.tab:ui.selected-1;}
   else{if(pos+1>=rows){ui.selected=ui.tab;ui.listPage=0;}else ui.selected++;}
   dirty=true;return;
  }
  else {int count=1;switch(ui.page){case Page::Home:count=HOME_ALL_ROW+(int)ui.recent.size();break;case Page::Games:{count=visibleGames(ui).size()+1;if(!ui.filter.empty()){int d=-1,c=-1;if(ui.standings.league==ui.league)teamGroups(ui.standings,ui.filter,d,c);count+=(d>=0||c>=0)?(d>=0?1:0)+(c>=0?1:0):1;}}break;
   case Page::Standings:count=ui.standingsAlt>=0?2:1;break;case Page::BibleHome:count=4;break;case Page::Devotional:count=3;break;case Page::Update:count=1;break;case Page::Detail:count=3;break;case Page::Favorites:count=std::max(1,(int)ui.favorites.size());break;case Page::Settings:count=8;break;default:break;}ui.selected=(ui.selected+step+count)%count;
   if(ui.page==Page::Home&&ui.selected<HOME_TABS&&ui.selected!=ui.tab){ui.tab=ui.selected;buildRecent();} // landing on a tab switches the list (the gear does not)
   if(ui.page==Page::Games&&!ui.filter.empty()&&ui.selected==0)ui.selected=step>0?std::min(1,count-1):count-1; // team pages skip the phantom header slot
  }
  dirty=true;return;
 }
 switch(ui.page){
 case Page::Home:
  if(ui.selected<HOME_TABS){ // enter the list under the tab: ALL goes to its first row, a league lands on NEXT
   if(ui.tab==0){if(!ui.recent.empty())ui.selected=HOME_ALL_ROW;}
   else ui.selected=HOME_NEXT;
  }
  else if(ui.selected==HOME_GEAR){ui.page=Page::Settings;ui.selected=0;}
  else if(ui.tab>0&&(ui.selected==HOME_PREV||ui.selected==HOME_NEXT)){const GamesLayout L=layoutGames(ui);const int n=std::max(1,(int)L.pages.size());ui.listPage=(ui.listPage+(ui.selected==7?1:n-1))%n;} // PREV / NEXT
  else if(ui.tab>0){const GamesLayout L=layoutGames(ui);const int pos=ui.selected-HOME_ROW;
   if(pos>=0&&pos<(int)L.ids.size()){ui.gameIndex=L.ids[pos];ui.page=Page::Detail;ui.detailFromHome=true;ui.selected=0;}}
  else if(ui.selected<HOME_ALL_ROW+(int)ui.recent.size()){ // a recent game opens its matchup inside that league's feed
   const RecentGame rg=ui.recent[ui.selected-HOME_ALL_ROW];const int backSel=ui.selected;ui.league=rg.league;ui.filter.clear();ui.date=ui.clockValid?localDate(time(nullptr)):ui.date;ui.feed=true;followToday=true;ui.page=Page::Games;loadView();
   auto it=std::find_if(ui.snapshot.games.begin(),ui.snapshot.games.end(),[&](const Game& g){return g.id==rg.game.id;});
   if(it!=ui.snapshot.games.end()){ui.gameIndex=it-ui.snapshot.games.begin();ui.page=Page::Detail;ui.detailFromHome=true;ui.selected=0;ui.origin.gameId=std::to_string(backSel);}
   else goHome();
  }break;
 case Page::Games:
  if(!ui.filter.empty()&&ui.selected>(int)visibleGames(ui).size()){ // team page standings rows
   int d=-1,c=-1;if(ui.standings.league==ui.league)teamGroups(ui.standings,ui.filter,d,c);const int which=ui.selected-(int)visibleGames(ui).size();
   const bool conference=(d<0)||which==2;const std::string team=ui.filter,name=ui.filterName;
   ui.origin={Page::Games,ui.league,ui.date,"",ui.feed};openStandings(ui.league,team,"",teamConference(team),conference);ui.filterName=name;break;}
  if(ui.selected==0){
   if(!ui.filter.empty()){} // team pages have no header row
   else if(ui.feed){const GamesLayout L=layoutGames(ui);ui.listPage=L.pages.empty()?0:(ui.listPage+1)%(int)L.pages.size();} // page bar: next page
   else{ui.page=Page::Date;ui.dateOffset=0;}
  }
  else {auto ids=visibleGames(ui);if(ui.selected<=(int)ids.size()){ui.gameIndex=ids[ui.selected-1];ui.page=Page::Detail;ui.selected=0;}}break;
 case Page::Date:ui.date=shiftDate(ui.date,ui.dateOffset);ui.feed=false;followToday=ui.clockValid&&ui.date==localDate(time(nullptr));ui.page=Page::Games;loadView();break;
 case Page::Detail:if(ui.selected>0&&ui.gameIndex<(int)ui.snapshot.games.size()){Game g=ui.snapshot.games[ui.gameIndex];openTeam(ui.selected==1?g.away:g.home,Page::Detail,g.id);}break;
 case Page::Favorites:if(ui.selected<(int)ui.favorites.size()){auto f=ui.favorites[ui.selected];ui.league=f.league;ui.date=ui.clockValid?localDate(time(nullptr)):prefs.getString("lastDate","").c_str();ui.feed=true;followToday=true;Team t;t.id=f.id;t.name=f.name;openTeam(t,Page::Favorites,"");}break;
 case Page::Settings:if(ui.selected==0)startAP();else if(ui.selected==1){nextFetch=0;for(int l=0;l<4;l++)leagueFetched[l]=0;goHome();}else if(ui.selected==2)sleepScreen();else if(ui.selected==3){speakReplies=!speakReplies;prefs.putBool("speak",speakReplies);ui.speak=speakReplies;}else if(ui.selected==4){darkMode=!darkMode;prefs.putBool("dark",darkMode);ui.dark=darkMode;}else if(ui.selected==5){nightSleep=!nightSleep;prefs.putBool("night",nightSleep);ui.nightSleep=nightSleep;}
  else if(ui.selected==6){ // next voice, saved, and a sample line in it
   ttsVoice=TTS_VOICES[(ttsVoiceIndex(ttsVoice.c_str())+1)%TTS_VOICE_COUNT];prefs.putString("voice",ttsVoice);ui.ttsVoice=ttsVoice.c_str();
   if(!voiceKey.isEmpty()&&!audio::playing()){static VoiceJob job;memset(&job,0,sizeof(job));std::string name=ttsVoice.c_str();name[0]=toupper(name[0]);snprintf(job.say,sizeof(job.say),"Hi, I'm %s. Bears twenty seven, Eagles seven. For God so loved the world.",name.c_str());snprintf(job.key,sizeof(job.key),"%s",voiceKey.c_str());snprintf(job.voice,sizeof(job.voice),"%s",ttsVoice.c_str());snprintf(job.voice,sizeof(job.voice),"%s",ttsVoice.c_str());xQueueSend(voiceQueue,&job,0);}}
  else requestUpdate(true);break;
 case Page::Wifi:if(!ui.ap)startAP();break;
 case Page::Launcher:
  if(ui.selected==LAUNCH_WEATHER){ui.page=Page::Weather;if(!ui.weather.valid)nextWeather=0;}
  else if(ui.selected==LAUNCH_BIBLE){ui.page=Page::Devotional;ui.selected=0;if(devotionalStale())nextDevotional=0;warmVoice();} // the Bible row opens today's devotional; BIBLE on that page opens the reader home
  else if(ui.selected==LAUNCH_GEAR){ui.page=Page::Settings;ui.selected=0;}
  else{ui.tab=ui.selected-LAUNCH_TAB0;ui.listPage=0;goHome();if(ui.tab==0){if(!ui.recent.empty())ui.selected=HOME_ALL_ROW;}else ui.selected=HOME_NEXT;} // straight into the list
  break;
 case Page::BibleHome:if(ui.selected==0)openBible(BibleRef{});else if(ui.selected==1)openBible(ui.votd);else if(ui.selected==2){ui.page=Page::Devotional;ui.selected=0;if(devotionalStale())nextDevotional=0;warmVoice();}else{ui.bible.pick=ui.bible.book-1;ui.page=Page::BibleBooks;ui.selected=0;}break;
 case Page::Devotional:
  if(ui.selected==0){if(ui.devotional.valid&&!voiceKey.isEmpty()&&!audio::playing()){static VoiceJob job;memset(&job,0,sizeof(job));snprintf(job.say,sizeof(job.say),"%s",devotionalSpeech(ui.devotional).c_str());snprintf(job.key,sizeof(job.key),"%s",voiceKey.c_str());snprintf(job.voice,sizeof(job.voice),"%s",ttsVoice.c_str());if(xQueueSend(voiceQueue,&job,0)==pdTRUE)Serial.println("DEVOTIONAL reading aloud");}}
  else if(ui.selected==1){ui.page=Page::BibleHome;ui.selected=0;}
  else goLauncher(LAUNCH_BIBLE);
  break;
 case Page::Bible:ui.bible.pick=ui.bible.book-1;ui.bible.pickBook=ui.bible.book;ui.page=Page::BibleBooks;break;
 case Page::BibleBooks:ui.bible.pickBook=ui.bible.pick+1;ui.bible.pick=ui.bible.pickBook==ui.bible.book?ui.bible.chapter-1:0;ui.page=Page::BibleChapters;break;
 case Page::BibleChapters:{BibleRef r;r.book=ui.bible.pickBook;r.chapter=ui.bible.pick+1;openBible(r);break;}
 default:break;
 }dirty=true;
}
static void handleResults(){
 FetchResult* r=nullptr;if(xQueueReceive(resultQueue,&r,0)!=pdTRUE)return;requestBusy=false;
 if(r->isVoice){
  if(r->voiceInterim){if(ui.page==Page::Voice&&ui.voice==VoiceState::Thinking){ui.voiceNote="LOOKING THAT UP...";dirty=true;}delete r;return;}
  if(ui.page==Page::Voice&&ui.voice==VoiceState::Thinking){
   const VoiceReply& v=r->voice;
   if(!v.ok){ui.voice=VoiceState::Error;ui.voiceNote=v.error;}
   else if(v.action==VoiceAction::Answer||v.action==VoiceAction::Fact){ui.voice=VoiceState::Answer;ui.voiceHeard=v.heard;ui.voiceAnswer=v.answer;ui.voiceLeague=v.league;ui.voiceTeamId=v.teamId;ui.voiceTeamName=v.teamName;}
   else if(v.action==VoiceAction::OpenDevotional){ui.voice=VoiceState::Idle;ui.page=Page::Devotional;ui.selected=0;if(devotionalStale())nextDevotional=0;Serial.println("VOICE opened devotional");}
   else if(v.action==VoiceAction::OpenWeather){ui.voice=VoiceState::Idle;ui.page=Page::Weather;ui.selected=0;if(!ui.weather.valid)nextWeather=0;Serial.println("VOICE opened weather");}
   else if(v.action==VoiceAction::OpenBible){ui.voice=VoiceState::Idle;openBible(v.daily?ui.votd:v.bible);Serial.printf("VOICE opened bible %s\n",bibleRefLabel(v.daily?ui.votd:v.bible).c_str());}
   else{ // go straight to the page that shows what was asked for
    ui.voice=VoiceState::Idle;ui.league=v.league;ui.filter.clear();ui.date=ui.clockValid?localDate(time(nullptr)):ui.date;ui.feed=true;followToday=true;
    if(v.action==VoiceAction::OpenStandings){ui.origin={Page::Home,ui.league,ui.date,"",true};openStandings(v.league,v.teamId,v.group,v.scope,false);}
    else if(v.action==VoiceAction::OpenTeam){Team t;t.id=v.teamId;t.name=v.teamName;openTeam(t,Page::Home,"");}
    else if(v.action==VoiceAction::OpenLeague){ui.tab=v.league+1;ui.listPage=0;goHome();}
    else{ui.page=Page::Games;loadView();
     if(v.action==VoiceAction::OpenGame){auto it=std::find_if(ui.snapshot.games.begin(),ui.snapshot.games.end(),[&](const Game& g){return g.id==v.gameId;});
      if(it!=ui.snapshot.games.end()){ui.gameIndex=it-ui.snapshot.games.begin();ui.page=Page::Detail;ui.selected=0;}}
    }
    Serial.printf("VOICE opened action=%d league=%d team=%s game=%s\n",(int)v.action,v.league,v.teamId.c_str(),v.gameId.c_str());
   }
   dirty=true;
  }
  delete r;return;
 }
 if(r->isDevoSync){
  Serial.printf("FETCH devosync ok=%d fetched=%d http=%d\n",r->ok,r->fetched,r->code);
  if(r->ok){prefs.putLong64("devosync",time(nullptr));refreshDevoHash();if(devotionalStale())nextDevotional=0;if(r->fetched>=40)nextDevoSync=millis()+5000;} // more to do: go again shortly
  if(devoSyncPending){devoSyncPending=false;if(refreshWake)refreshPending--;}
  delete r;return;
 }
 if(r->isDevotional){
  ui.devotionalLoading=false;Serial.printf("FETCH devotional ok=%d http=%d title=%s\n",r->ok,r->code,r->devotional.title.c_str());
  if(r->ok){ui.devotional=r->devotional;prefs.putString("devo",encodeDevotional(ui.devotional).c_str());
   if(ui.devotional.fromFile&&ui.devotional.ref.valid()){ui.votd=ui.devotional.ref;ui.votdText=ui.devotional.verse;if(ui.page==Page::Launcher){buildLauncher();}dirty=true;}} // the file's verse is the day's verse
  if(devotionalPending){devotionalPending=false;if(refreshWake)refreshPending--;}
  if(ui.page==Page::Devotional||ui.page==Page::BibleHome)dirty=true;delete r;return;
 }
 if(r->isUpdate){
  updateBusy=false;autoUpdateCheck=false;Serial.printf("FETCH update ok=%d installed=%d note=%s\n",r->ok,r->installed,r->note.c_str());
  if(r->installed){ui.page=Page::Update;ui.updateNote=r->note;restartAt=millis()+3000;}
  else if(ui.page==Page::Update)ui.updateNote=r->ok?"UP TO DATE - V" FIRMWARE_VERSION " IS THE LATEST":"COULD NOT UPDATE: "+r->note;
  if(refreshWake)refreshPending--;dirty=true;delete r;return;
 }
 if(r->isWeather){
  Serial.printf("FETCH weather ok=%d http=%d temp=%d hi=%d lo=%d rain=%d code=%d city=%s\n",r->ok,r->code,r->weather.temp,r->weather.high,r->weather.low,r->weather.rain,r->weather.code,r->weather.city.c_str());
  if(r->ok){ui.weather=r->weather;prefs.putString("weather",encodeWeatherCache(ui.weather).c_str());nextWeather=millis()+3600000;}else nextWeather=millis()+300000;
  if(refreshWake)refreshPending--;if(ui.page==Page::Launcher||ui.page==Page::Weather)dirty=true;delete r;return;
 }
 if(r->isStandings){
  ui.standingsLoading=false;Serial.printf("FETCH standings league=%d ok=%d groups=%u\n",r->league,r->ok,(unsigned)r->standings.groups.size());
  if(r->ok&&r->league==standingsLeagueWanted&&r->standings.scope==standingsScopeWanted){ui.standings=r->standings;if(ui.page==Page::Standings)aimStandings();dirty=true;}
  delete r;return;
 }
 if(r->isDetail){
  Serial.printf("FETCH detail game=%s http=%d ok=%d\n",r->game.c_str(),r->code,r->ok);
  if(ui.page==Page::Detail&&ui.gameIndex<(int)ui.snapshot.games.size()&&ui.snapshot.games[ui.gameIndex].id==r->game){ui.detail=r->ok?r->detail:GameDetail{};ui.detail.id=r->game;dirty=true;}
  detailFetchedAt=millis();delete r;return;
 }
 Serial.printf("FETCH league=%d date=%s feed=%d team=%s http=%d ok=%d games=%u more=%d\n",r->league,r->date.c_str(),r->feed,r->team.c_str(),r->code,r->ok,(unsigned)r->snapshot.games.size(),r->more);
 if(r->ok){ui.storage=r->stored;if(lastDateSaved!=r->date.c_str()){lastDateSaved=r->date.c_str();prefs.putString("lastDate",lastDateSaved);}
  if(r->feed&&ui.page==Page::Home&&(ui.tab==0||r->league==ui.league)){const int keep=ui.selected;buildRecent();ui.selected=std::min(keep,ui.tab>0?HOME_NEXT+(int)visibleGames(ui).size():HOME_ALL_ROW-1+(int)ui.recent.size());dirty=true;}
  if(r->feed&&ui.page==Page::Launcher){buildLauncher();dirty=true;}
  if(r->feed&&r->team.empty()){leagueLive[r->league]=leagueActive(r->snapshot,time(nullptr));leagueFetched[r->league]=millis();}
  if(r->feed&&refreshWake){refreshPending--;nextFetch=0;}}
 if(sameView(r->league,r->date,r->feed,r->team)){
  ui.fetching=false;ui.failed=!r->ok;
  if(r->ok){
   std::string id=ui.gameIndex<(int)ui.snapshot.games.size()?ui.snapshot.games[ui.gameIndex].id:"";
   ui.snapshot=std::move(r->snapshot);
   if(ui.page==Page::Detail){auto it=std::find_if(ui.snapshot.games.begin(),ui.snapshot.games.end(),[&](const Game& g){return g.id==id;});if(it==ui.snapshot.games.end()){ui.page=Page::Games;ui.selected=0;}else ui.gameIndex=it-ui.snapshot.games.begin();}
   if(ui.page==Page::Games){ui.selected=std::min(ui.selected,(int)visibleGames(ui).size());if(ui.selected==0&&(ui.feed||!ui.filter.empty())&&!ui.snapshot.games.empty())ui.selected=1;}
  }dirty=true;
 }
 // A feed with days still to backfill continues almost immediately.
 bool other=(ui.page==Page::Games||ui.page==Page::Detail)&&!sameView(r->league,r->date,r->feed,r->team);
 nextFetch=other?0:millis()+(r->ok?(r->more?1500:60000):120000);delete r;
}
static void serialControl(){
 // Local USB diagnostic commands. No credentials or private state are logged.
 if(!Serial.available())return;char ch=Serial.read();
 if(ch=='u'||ch=='d'||ch=='s'||ch=='b'){Key k{ch=='u'?0:ch=='d'?1:ch=='s'?2:3,ButtonEvent::Click};keyAction(k);lastKeyAt=millis();}
 if(ch=='h')keyAction({2,ButtonEvent::Hold});if(ch=='r')keyAction({2,ButtonEvent::ReleaseHold});
 if(ch=='p'){Serial.println("FRAME_BEGIN");for(int i=0;i<48000;i++){Serial.printf("%02x",canvas.getBuffer()[i]);if(i%100==99)Serial.println();}Serial.println("FRAME_END");}
 if(ch=='w')startAP();
 if(ch=='Z'){int mask=0;const int64_t planned=planRefresh(mask);Serial.printf("WAKE in %lld s (%.1f h) battery=%d%% idle=%lus refreshWake=%d nextRefresh=%llds leagues=0x%x live=%d%d%d%d\n",(long long)secondsUntilWake(),secondsUntilWake()/3600.0,batteryPercent(),(unsigned long)((millis()-lastKeyAt)/1000),refreshWake,(long long)planned,mask,leagueLive[0],leagueLive[1],leagueLive[2],leagueLive[3]);}
 if(ch=='I'){Serial.println("SLEEP idle (forced)");lastKeyAt=0;idleSleep();}
 if(ch=='L'){String v=Serial.readStringUntil('\n');v.trim();setLocation(v);}
 if(ch=='P'){ // PMU rail dump: DCDC enables 0x80, LDO enables 0x90/0x91, LDO voltages 0x92-0x9B, status 0x00/0x01, battery 0xA4
  Serial.print("PMU");for(uint8_t reg:{0x00,0x01,0x80,0x82,0x83,0x84,0x85,0x86,0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0x9b,0xa4})Serial.printf(" %02x=%02x",reg,pmuRead(reg));Serial.println();
 }
 if(ch=='D'){ui.devotional=Devotional{};nextDevotional=0;Serial.println("DEVOTIONAL requested");}
 if(ch=='Y'){prefs.putLong64("devosync",0);nextDevoSync=0;Serial.println("DEVOSYNC requested");}
 if(ch=='U'){requestUpdate(true);Serial.println("UPDATE check requested");}
 if(ch=='W'){nextWeather=0;Serial.printf("WEATHER requested (cached: %s)\n",weatherSpeech(ui.weather).c_str());}
 if(ch=='B'){String v=Serial.readStringUntil('\n');v.trim();BibleRef r=v.equalsIgnoreCase("daily")?ui.votd:parseBibleRef(v.c_str());openBible(r);lastKeyAt=millis();Serial.printf("BIBLE open %s page=%d/%u votd=%s\n",bibleRefLabel(r).c_str(),ui.bible.page+1,(unsigned)ui.bible.pages.size(),bibleRefLabel(ui.votd).c_str());}
 if(ch=='K'){String value=Serial.readStringUntil('\n');value.trim();voiceKey=value;prefs.putString("orkey",voiceKey);Serial.printf("VOICE key %s (%u chars)\n",voiceKey.isEmpty()?"cleared":"saved",(unsigned)voiceKey.length());}
 if(ch=='S'){String text=Serial.readStringUntil('\n');text.trim();static VoiceJob job;memset(&job,0,sizeof(job));snprintf(job.say,sizeof(job.say),"%s",text.c_str());snprintf(job.key,sizeof(job.key),"%s",voiceKey.c_str());snprintf(job.voice,sizeof(job.voice),"%s",ttsVoice.c_str());if(xQueueSend(voiceQueue,&job,0)==pdTRUE)Serial.println("VOICE speaking test text");}
 if(ch=='a'){keyAction({2,ButtonEvent::Hold});voiceDemoRelease=millis()+4000;Serial.println("VOICE demo: listening for 4 s");}
 if(ch=='v'){ // microphone check: 3 s of PCM as hex
  if(audio::startRecording()){delay(3000);uint8_t* pcm=nullptr;size_t n=audio::stopRecording(&pcm);
   Serial.printf("PCM_BEGIN %u\n",(unsigned)n);for(size_t i=0;i<n;i++){Serial.printf("%02x",pcm[i]);if(i%128==127)Serial.println();}Serial.println("\nPCM_END");heap_caps_free(pcm);
  }else Serial.println("PCM_FAIL");
 }
 if(ch=='T'){
  String value=Serial.readStringUntil('\n');int64_t epoch=strtoll(value.c_str(),nullptr,10);
  if(epoch>=1700000000LL&&epoch<4102444800LL){timeval tv{(time_t)epoch,0};settimeofday(&tv,nullptr);ui.clockValid=true;writeRtc();nextFetch=0;dirty=true;Serial.println("CLOCK set from USB host");}
 }
 if(ch=='?')Serial.printf("STATUS page=%d sel=%d tab=%d league=%d feed=%d team=%s conf=%s standings=%d/%s games=%u covered=%u wifi=%d clock=%d heap=%u psram=%u saved=%lld uptime=%lu epoch=%lld reason=%d\n",(int)ui.page,ui.selected,ui.tab,ui.league,ui.feed,ui.filter.c_str(),teamConference(ui.filter).c_str(),ui.standings.league,ui.standings.scope.c_str(),(unsigned)ui.snapshot.games.size(),(unsigned)ui.snapshot.covered.size(),ui.online,ui.clockValid,ESP.getFreeHeap(),ESP.getFreePsram(),ui.snapshot.updated,(unsigned long)millis(),(long long)time(nullptr),disconnectReason.load());
 if(ch=='?')Serial.printf("LOCAL IP %s\n",WiFi.localIP().toString().c_str());
 if(ch=='?')Serial.printf("VOICE state=%d key=%u codec=%d recording=%d playing=%d speak=%d\n",(int)ui.voice,(unsigned)voiceKey.length(),audio::available(),audio::recording(),audio::playing(),speakReplies);
 if(ch=='?')Serial.printf("PANEL partials=%d pending=%d busy=%d idleMs=%lu sinceFull=%lu keysQueued=%u\n",partialCount,panelPending,EPD_3IN97_Busy(),(unsigned long)(millis()-lastKeyAt),(unsigned long)(millis()-lastFullRefresh),(unsigned)uxQueueMessagesWaiting(inputQueue));
}
void setupApp(){
 Serial.begin(115200);Serial.setTimeout(1000);delay(500);Serial.println("PIXEL LEAGUE v" FIRMWARE_VERSION " boot");ui.version=FIRMWARE_VERSION;
 prefs.begin("pixel-league",false);ssid=prefs.getString("ssid","");password=prefs.getString("pass","");tz=prefs.getString("tz",zones[0]);setenv("TZ",tz.c_str(),1);tzset();lastDateSaved=prefs.getString("lastDate","");voiceKey=prefs.getString("orkey","");ttsVoice=prefs.getString("voice","alloy");ui.ttsVoice=ttsVoice.c_str();speakReplies=prefs.getBool("speak",true);darkMode=prefs.getBool("dark",false);ui.dark=darkMode;nightSleep=prefs.getBool("night",true);ui.nightSleep=nightSleep;
 Wire.begin(41,42);powerUpFromSleep();panelPower(true);ui.clockValid=readRtc();audio::init();
 ui.speak=speakReplies;fsOK=LittleFS.begin(false);
 // This dedicated new filesystem partition is initialized only on first app boot.
 if(!fsOK&&!prefs.getBool("fsInit",false)){fsOK=LittleFS.format()&&LittleFS.begin(false);}
 if(fsOK)prefs.putBool("fsInit",true);ui.storage=fsOK;loadFavorites();
 {const uint32_t pos=prefs.getUInt("bible",0);BibleRef r;r.book=(pos>>16)&255;r.chapter=(pos>>8)&255;if(r.valid()){loadBibleChapter(r.book,r.chapter);ui.bible.page=std::min((int)(pos&255),std::max(0,(int)ui.bible.pages.size()-1));}else loadBibleChapter(43,1);}
 loadVerseOfDay();
 {Devotional d;if(decodeDevotional(prefs.getString("devo","").c_str(),d)){ui.devotional=d;if(d.fromFile&&d.day==dayOfYear()&&d.ref.valid()){ui.votd=d.ref;ui.votdText=d.verse;}}}
 {Weather w;if(decodeWeatherCache(prefs.getString("weather","").c_str(),w))ui.weather=w;nextWeather=(w.valid&&time(nullptr)-w.fetched<=3000)?1:0;} // a fresh cache waits until it is stale; 0 forces a fetch
 ui.date=ui.clockValid?localDate(time(nullptr)):prefs.getString("lastDate","").c_str();
 inputQueue=xQueueCreate(12,sizeof(Key));requestQueue=xQueueCreate(1,sizeof(FetchRequest));resultQueue=xQueueCreate(2,sizeof(FetchResult*));voiceQueue=xQueueCreate(2,sizeof(VoiceJob));
 xTaskCreatePinnedToCore(inputTask,"keys",3072,nullptr,2,nullptr,1);
 xTaskCreatePinnedToCore(networkTask,"scores",16384,nullptr,1,nullptr,0);
 xTaskCreatePinnedToCore(voiceTask,"voice",16384,nullptr,1,nullptr,0);

 WiFi.onEvent([](WiFiEvent_t event,WiFiEventInfo_t info){if(event==ARDUINO_EVENT_WIFI_STA_DISCONNECTED)disconnectReason=info.wifi_sta_disconnected.reason;});
 WiFi.mode(WIFI_STA);WiFi.setAutoReconnect(true);if(ssid.length()){WiFi.begin(ssid.c_str(),password.c_str());nextReconnect=millis()+45000;}
 DEV_Module_Init();EPD_3IN97_Init();
 if(ssid.isEmpty())startAP();
 shown=(uint8_t*)malloc(48000);ui.page=Page::Launcher;ui.selected=LAUNCH_TAB0+ui.tab;buildLauncher();
 {tm lt{};time_t now=time(nullptr);localtime_r(&now,&lt);if(lt.tm_hour==23&&lt.tm_min==0)lastSleepDay=lt.tm_yday;
  const auto cause=esp_sleep_get_wakeup_cause();refreshWake=cause==ESP_SLEEP_WAKEUP_TIMER;refreshStarted=millis();
  if(cause==ESP_SLEEP_WAKEUP_TIMER||cause==ESP_SLEEP_WAKEUP_EXT1){ui.tab=rtcTab;if(ui.tab<0||ui.tab>=HOME_TABS)ui.tab=0;}
  if(refreshWake){planRefresh(refreshMaskBits);refreshPending=__builtin_popcount(refreshMaskBits);}else{refreshMaskBits=15;refreshPending=4;}
  if(refreshWake&&(!ui.weather.valid||time(nullptr)-ui.weather.fetched>3000))refreshPending++; // only leagues with a game on (or a stale cache), plus the weather when stale
  {tm lt{};time_t t=time(nullptr);localtime_r(&t,&lt);autoUpdateCheck=refreshWake&&ui.clockValid&&lt.tm_hour==6;if(autoUpdateCheck)refreshPending++;}
  refreshDevoHash();if(refreshWake&&ui.clockValid&&devoSyncDue()){devoSyncPending=true;refreshPending++;} // the library sync rides on a wake every six hours
  if(refreshWake&&ui.clockValid&&ui.votd.valid()&&devotionalStale()){devotionalPending=true;refreshPending++;} // a new day's devotional rides on the wake // the 6:30 wake also looks for a new release
  Serial.printf("WAKE cause=%d refresh=%d leagues=0x%x pending=%d tab=%d sleeps=%d battery=%d%%\n",(int)cause,refreshWake,refreshMaskBits,refreshPending,ui.tab,rtcSleeps,batteryPercent());}
 ui.now=time(nullptr);renderer.render(ui);applyTheme(canvas.getBuffer());EPD_3IN97_Display_Base(canvas.getBuffer());dirty=false;
 if(shown)memcpy(shown,canvas.getBuffer(),48000);lastFullRefresh=millis();
 Serial.println("READY: rocker browse/select, BOOT back, hold rocker voice status");
}
void loopApp(){
 uint32_t now=millis();
 if(beginAt&&(int32_t)(now-beginAt)>=0){beginAt=0;WiFi.disconnect(false,false);WiFi.begin(ssid.c_str(),password.c_str());}
 bool connected=WiFi.status()==WL_CONNECTED;
 if(connectionPending&&!beginAt&&connected){connectionPending=false;connectionFailed=false;connectedAt=now;}
 if(connectionPending&&now-connectionStarted>=35000){connectionPending=false;connectionFailed=true;ui.notice="Could not connect. Check Wi-Fi details on phone.";dirty=true;}

 if(connected!=ui.online){ui.online=connected;dirty=true;nextFetch=0;if(connected){connectedAt=now;connectionFailed=false;reconnectDelay=5000;if(!sntpStarted){
 esp_sntp_config_t cfg=ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");cfg.wait_for_sync=false;cfg.sync_cb=[](struct timeval*){timeSynced=true;};
 sntpStarted=esp_netif_sntp_init(&cfg)==ESP_OK;
 }ui.notice="Connected! Press BOOT to start browsing.";}else{ui.notice="Connection lost. Saved scores remain available.";nextReconnect=now+5000;ui.offlineSince=ui.clockValid?time(nullptr):0;}}
 if(!connected&&!connectionPending&&ssid.length()&&(int32_t)(now-nextReconnect)>=0){WiFi.reconnect();nextReconnect=now+reconnectDelay;reconnectDelay=std::min(uint32_t(60000),reconnectDelay*2);}
 if(timeSynced.exchange(false)){ui.clockValid=true;writeRtc();lastRtcWrite=now;nextFetch=0;dirty=true;}
 if(ui.clockValid&&now-lastRtcWrite>3600000){writeRtc();lastRtcWrite=now;}
 ui.now=time(nullptr);
 if(ui.clockValid&&followToday&&ui.date!=localDate(ui.now)&&ui.page!=Page::Date&&ui.page!=Page::Detail){ui.date=localDate(ui.now);loadView();}
 {const int s=speakState;if(s!=ui.speaking){ui.speaking=s;if(ui.page==Page::Devotional)dirty=true;}}
 static uint32_t batteryRead=0;if(!batteryRead||now-batteryRead>60000){batteryRead=now;const int b=batteryPercent();if(b!=ui.battery){ui.battery=b;if(ui.page==Page::Launcher||ui.page==Page::Settings)dirty=true;}}
 static int votdDay=-1;if(ui.clockValid&&dayOfYear()!=votdDay){votdDay=dayOfYear();loadVerseOfDay();refreshDevoHash();nextDevotional=0;if(ui.page==Page::Launcher||ui.page==Page::BibleHome){if(ui.page==Page::Launcher)buildLauncher();dirty=true;}}
 if(ui.ap){dns.processNextRequest();server.handleClient();if((finishSetup&&(int32_t)(now-finishAt)>=0)||(ui.online&&!connectionPending&&now-connectedAt>120000)||now-apStarted>600000)stopAP();}
 handleResults();serialControl();Key k;while(xQueueReceive(inputQueue,&k,0)==pdTRUE){keyAction(k);lastKeyAt=now;refreshWake=false;}
 if(voiceDemoRelease&&(int32_t)(now-voiceDemoRelease)>=0){voiceDemoRelease=0;keyAction({2,ButtonEvent::ReleaseHold});}
 standingsUpkeep();
 // Idle: after ten quiet minutes (or once a refresh wake has fetched its page) go back to sleep.
 // A freshly installed build proves itself once Wi-Fi and the clock are up (or after two minutes), else the bootloader rolls back on the next boot.
 static bool pendingVerify=true;if(pendingVerify&&((ui.online&&ui.clockValid)||now>120000)){pendingVerify=false;esp_ota_mark_app_valid_cancel_rollback();Serial.println("UPDATE build verified");}
 if(restartAt&&(int32_t)(now-restartAt)>=0&&!panelPending){Serial.println("UPDATE restarting");delay(100);ESP.restart();}
 if(autoUpdateCheck&&ui.online&&ui.clockValid&&!requestBusy)requestUpdate(false);
 if(ui.online&&ui.clockValid&&!requestBusy&&devoSyncDue()&&(int32_t)(now-nextDevoSync)>=0)requestDevoSync();
 if(ui.online&&ui.clockValid&&!requestBusy&&devotionalStale()&&(int32_t)(now-nextDevotional)>=0)requestDevotional();
 if(ui.page!=Page::Wifi&&!ui.ap&&ui.voice==VoiceState::Idle&&!audio::playing()&&!audio::recording()&&!panelPending&&!updateBusy&&!restartAt&&!pendingVerify){
  const uint32_t quiet=now-std::max(lastKeyAt,(uint32_t)0);
  if(refreshWake&&((refreshPending<=0&&!requestBusy)||now-refreshStarted>60000))idleSleep();
  if(!refreshWake&&quiet>IDLE_MS&&now>IDLE_MS&&!requestBusy)idleSleep();
 }
 // Bedtime: at 11 pm local the board sleeps until 6:30 am (or a BOOT press). Only the
 // 11 pm transition triggers it, so waking it by hand at night keeps it awake.
 if(nightSleep&&ui.clockValid&&ui.page!=Page::Wifi&&!ui.ap){tm lt{};localtime_r((const time_t*)&ui.now,&lt);
  if(lt.tm_hour==23&&lt.tm_min==0&&lastSleepDay!=lt.tm_yday){lastSleepDay=lt.tm_yday;Serial.println("SLEEP scheduled bedtime");sleepScreen(true);}}
 // A matchup page fetches its details once, and again each minute while the game is on.
 if(ui.page==Page::Detail&&ui.online&&ui.clockValid&&!requestBusy&&ui.gameIndex<(int)ui.snapshot.games.size()){
  const Game& g=ui.snapshot.games[ui.gameIndex];
  if(ui.detail.id!=g.id||(g.state!="post"&&now-detailFetchedAt>60000))requestDetail(g);
 }
 if(ui.online&&ui.clockValid&&!requestBusy&&(int32_t)(now-nextWeather)>=0&&(!ui.weather.valid||ui.now-ui.weather.fetched>3000||nextWeather==0)){requestWeather();nextWeather=now+300000;}
 if(ui.online&&ui.clockValid&&!requestBusy&&(int32_t)(now-nextFetch)>=0){
  if(ui.page==Page::Games||ui.page==Page::Detail||((ui.page==Page::Home||ui.page==Page::Launcher)&&ui.tab>0))requestScores(ui.league,ui.date,ui.feed,ui.filter);
  else { // background rotation: on a refresh wake only the planned leagues; awake, idle leagues only every 15 minutes
   int l=backgroundLeague,tries=0;
   auto wanted=[&](int x){return refreshWake?((refreshMaskBits>>x)&1)!=0:(leagueLive[x]||leagueFetched[x]==0||now-leagueFetched[x]>IDLE_POLL_MS);};
   while(tries<4&&!wanted(l)){l=(l+1)%4;tries++;}
   if(tries<4){requestScores(l,localDate(ui.now),true);backgroundLeague=(l+1)%4;}else nextFetch=now+60000;
  }
 }
 static bool wasFresh=false;bool isFresh=fresh(ui.snapshot,ui.now,ui.online,ui.failed);if(wasFresh!=isFresh){wasFresh=isFresh;if(ui.page==Page::Games||ui.page==Page::Detail)dirty=true;}
 // The panel refreshes in the background. Input keeps changing `ui` meanwhile
 // and is drawn as one frame the moment the panel is free, so the device never
 // stalls on a press. Ghost-cleaning full refreshes wait for an idle moment.
 if(panelPending&&!EPD_3IN97_Busy()){panelPending=false;Serial.printf("DRAW done ms=%lu\n",(unsigned long)(now-panelStartedAt));}
 if(panelPending&&now-panelStartedAt>15000){panelPending=false;Serial.println("DISPLAY BUSY TIMEOUT");}
 if(!panelPending){
  bool idle=now-lastKeyAt>2500&&uxQueueMessagesWaiting(inputQueue)==0;
  if(dirty){
   ui.now=time(nullptr);renderer.render(ui);applyTheme(canvas.getBuffer());dirty=false;
   if(!shown||memcmp(shown,canvas.getBuffer(),48000)!=0){
    // The panel needs the frame it currently shows as the "old" plane.
    uint32_t start=millis();EPD_3IN97_Display_Partial_Async(canvas.getBuffer(),shown);partialCount++;
    if(shown)memcpy(shown,canvas.getBuffer(),48000);
    panelPending=true;panelStartedAt=millis();Serial.printf("DRAW page=%d send=%lu\n",(int)ui.page,(unsigned long)(panelStartedAt-start));
   }
  }else if(idle&&(partialCount>=8||(partialCount>0&&now-lastFullRefresh>1800000))){
   // Fast full refresh (~1.5 s) clears partial-update ghosting; a slow full
   // refresh every 30 minutes keeps the panel healthy.
   bool slow=now-lastFullRefresh>1800000;
   if(slow){EPD_3IN97_Init();EPD_3IN97_Display_Base_Async(shown?shown:canvas.getBuffer());lastFullRefresh=now;}
   else {EPD_3IN97_Init_Fast();EPD_3IN97_Display_Fast_Base_Async(shown?shown:canvas.getBuffer());}
   partialCount=0;panelPending=true;panelStartedAt=millis();Serial.printf("CLEAN %s\n",slow?"full":"fast");
  }
 }
 delay(10);
}
