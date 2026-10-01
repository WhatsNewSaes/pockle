#pragma once
#include <ArduinoJson.h>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <climits>
#include "Core.h"
#include "Bible.h"
// Voice questions go to one chat-completion request on OpenRouter carrying the
// recorded WAV plus the device's saved scores as the only allowed knowledge.
namespace retro {
static const char* VOICE_MODEL="google/gemini-2.5-flash";
inline std::string jsonEscape(const std::string& s){
 std::string o;o.reserve(s.size()+8);
 for(unsigned char c:s){
  if(c=='"')o+="\\\"";else if(c=='\\')o+="\\\\";else if(c=='\n')o+="\\n";else if(c<32)o+=' ';else o+=(char)c;
 }
 return o;
}
// The device prefers to open one of its own pages; a spoken/written answer is
// the fallback for requests no page can satisfy.
inline std::string voiceSystemPrompt(){
 return "You are the voice control of a small e-paper sports scoreboard used by a child. It has these pages: "
  "a league scoreboard (all recent games of MLB, NFL, NBA or CFB), a team page (a team's recent results and next game), "
  "a matchup page (one game with its score and stats), a Bible reader (Berean Standard Bible) with a verse of the day, and a 7-day weather forecast page. For anything about current or recent games use ONLY the saved scores provided. "
  "Choose the action that best satisfies the request: "
  "\"open_game\" when they ask about a team's game or score (its most recent game, or a specific opponent if named); "
  "\"open_team\" when they ask about a team's schedule, next game, record or season; "
  "\"open_league\" when they ask for a league's scores or games in general; "
  "\"open_standings\" when they ask for standings, the table, who leads a division or conference, or a team's place: set group to the division or conference name they said (e.g. NFC North, AFC, AL East, Pacific, SEC) or team to the team whose division they mean; "
  "\"open_weather\" for anything about the weather, forecast, temperature, rain, snow or what to wear, today or any coming day: the context carries the 7-day forecast, so also put a one-sentence spoken reply in answer (e.g. the day asked about with its high, low and rain chance, or a plain answer like whether to bring a jacket); "
  "\"open_bible\" when they ask to open the Bible, go to or read a book, chapter, verse or passage (set book to the full English book name, chapter and verse as numbers or null; set daily to true for the verse of the day; set read to true when they want it read aloud or ask what it says); "
  "\"fact\" for sports history, trivia, rules, records, players, teams, championships or seasons before the saved scores: answer from your own knowledge in one or two short sentences and mention the season or year. "
  "Your training ended long before today's date in the context, so anything about the PRESENT - who currently plays for, starts for, coaches or owns a team, rosters, injuries, contracts, standings, 'this season', 'now', 'the latest' - MUST set needs_lookup to true with the answer exactly \"I'm not sure about that one.\"; "
  "answer from memory only for settled history (a named past season or year, retired players). Relative words - last year, last season, latest, most recent, this year, so far - count from today's date in the context, not from your training, so they need the lookup too, as do totals that can still grow (how many titles, career stats). "
  "If you are unsure whether something is still current, set needs_lookup to true - never offer an older fact as if it were current; "
  "\"answer\" only for a question about the saved scores that no page fits, or when the saved scores do not have it, "
  "You only talk about sports, the Bible and the weather: for anything else use action \"answer\" with the answer \"I only know about sports, the Bible and the weather. Ask me about a team, a game, or a verse!\" "
  "Reply with strict JSON: {\"heard\": <what the user said, briefly>, \"action\": <\"open_game\"|\"open_team\"|\"open_league\"|\"open_standings\"|\"open_bible\"|\"open_weather\"|\"fact\"|\"answer\">, \"group\": <division or conference name for standings, else null>, "
  "\"book\": <Bible book name or null>, \"chapter\": <number or null>, \"verse\": <number or null>, \"daily\": <true or false>, \"read\": <true or false>, "
  "\"league\": <\"MLB\"|\"NFL\"|\"NBA\"|\"CFB\"|null>, \"team\": <team abbreviation exactly as in the saved scores, or null>, "
  "\"opponent\": <opponent abbreviation if a specific matchup was named, else null>, "
  "\"answer\": <the answer for fact/answer, under 220 characters; otherwise a short confirmation>, \"needs_lookup\": <true or false>}";
}
// Compact, newest-first summary of what the device has saved.
inline std::string voiceContext(const Snapshot* feeds[4],const std::vector<Favorite>& favorites,int64_t now,const std::string& weather="",size_t perLeague=14){
 static const char* names[]={"MLB","NFL","NBA","CFB"};
 std::string ctx="Now: "+stamp(now)+" ("+dayLabel(now,now)+" is today). ";if(!weather.empty())ctx+="Weather (7-day forecast): "+weather+". ";ctx+="Saved scores, newest first:\n";
 for(int l=0;l<4;l++){
  const Snapshot* f=feeds[l];ctx+=names[l];ctx+=": ";
  if(!f||f->games.empty()){ctx+="nothing saved\n";continue;}
  size_t n=0;
  for(const auto& g:f->games){if(n++>=perLeague)break;
   const int64_t start=isoEpoch(g.start);
   // Spell out who won: "PHI 7 @ CHI 27" alone was misread as a Bears loss.
   ctx+=g.away.abbr+" (away) "+score(g,g.away)+" at "+g.home.abbr+" (home) "+score(g,g.home)+" ("+(g.state=="pre"?"scheduled "+clockLabel(start):compactStatus(g.status))+", "+dayLabel(start,now);
   if(g.week>0&&(l==1||l==3))ctx+=", week "+std::to_string(g.week);
   if(g.state=="post"){int a=atoi(g.away.score.c_str()),h=atoi(g.home.score.c_str());if(!g.away.score.empty()&&!g.home.score.empty())ctx+=a>h?", "+g.away.abbr+" won":h>a?", "+g.home.abbr+" won":", tie";}
   ctx+="); ";
  }
  ctx+="\n";
 }
 if(!favorites.empty()){ctx+="Favorite teams: ";for(const auto& f:favorites)ctx+=f.name+" ("+names[f.league]+"); ";ctx+="\n";}
 return ctx;
}
enum class VoiceAction {Answer,OpenGame,OpenTeam,OpenLeague,Fact,OpenStandings,OpenBible,OpenWeather};
struct VoiceReply { bool ok=false,lookup=false; VoiceAction action=VoiceAction::Answer; std::string heard,answer,error; int league=-1; std::string teamAbbr,teamId,teamName,gameId,group,scope; BibleRef bible; bool daily=false,read=false; };
inline std::string upperAbbr(std::string s){for(auto& c:s)c=toupper((unsigned char)c);return s;}
// Newest game in the feeds involving `team` (and `opponent` if given), preferring `league`.
inline bool findGame(const Snapshot* feeds[4],int league,const std::string& team,const std::string& opponent,int& outLeague,std::string& outId){
 for(int pass=0;pass<2;pass++)for(int l=0;l<4;l++){
  if((pass==0)!=(l==league))continue;if(!feeds[l])continue;
  for(const auto& g:feeds[l]->games){
   const bool has=upperAbbr(g.away.abbr)==team||upperAbbr(g.home.abbr)==team;
   const bool opp=opponent.empty()||upperAbbr(g.away.abbr)==opponent||upperAbbr(g.home.abbr)==opponent;
   if(has&&opp){outLeague=l;outId=g.id;return true;}
  }
 }
 return false;
}
inline int leagueIndex(std::string s){for(auto& c:s)c=toupper((unsigned char)c);if(s=="MLB")return 0;if(s=="NFL")return 1;if(s=="NBA")return 2;if(s=="CFB"||s=="NCAA"||s=="COLLEGE")return 3;return -1;}
// Parses OpenRouter's chat completion; the model's JSON sits in message.content.
inline VoiceReply parseVoiceReply(JsonVariantConst root,const Snapshot* feeds[4]){
 VoiceReply r;
 if(!root["error"].isNull()){r.error=clean(root["error"]["message"].as<const char*>()?root["error"]["message"].as<const char*>():"service error",80);return r;}
 const char* content=root["choices"][0]["message"]["content"].as<const char*>();
 if(!content){r.error="no answer";return r;}
 JsonDocument inner;if(deserializeJson(inner,content)){r.error="bad answer";return r;}
 r.heard=clean(inner["heard"].as<const char*>()?inner["heard"].as<const char*>():"",120);
 r.answer=clean(inner["answer"].as<const char*>()?inner["answer"].as<const char*>():"",240);
 auto str=[&](const char* key){const char* v=inner[key].as<const char*>();if(!v)v=inner["navigate"][key].as<const char*>();return std::string(v?v:"");};
 r.league=leagueIndex(str("league"));
 const std::string action=str("action");r.lookup=inner["needs_lookup"]|false;
 r.action=action=="open_game"?VoiceAction::OpenGame:action=="open_team"?VoiceAction::OpenTeam:action=="open_league"?VoiceAction::OpenLeague:action=="fact"?VoiceAction::Fact:action=="open_standings"?VoiceAction::OpenStandings:action=="open_bible"?VoiceAction::OpenBible:action=="open_weather"?VoiceAction::OpenWeather:VoiceAction::Answer;
 if(r.action==VoiceAction::OpenWeather){r.ok=true;return r;} // the page carries the forecast; the answer, if any, is spoken
 if(r.action==VoiceAction::OpenBible){ // a place in the Bible; no book means the saved reading position
  auto num=[&](const char* key){JsonVariantConst x=inner[key];return x.is<int>()?x.as<int>():x.is<const char*>()?atoi(x.as<const char*>()):0;};
  r.daily=inner["daily"]|false;r.read=inner["read"]|false;
  r.bible.book=bibleBookIndex(str("book"));if(r.bible.book){r.bible.chapter=std::max(1,num("chapter"));r.bible.verse=std::max(0,num("verse"));if(!r.bible.valid())r.bible.chapter=1;}
  if(!r.bible.book&&!r.daily){BibleRef parsed=parseBibleRef(r.heard);if(parsed.valid())r.bible=parsed;} // the model named nothing: try the words themselves
  r.ok=true;return r;
 }
 r.group=clean(str("group"),40);
 if(r.action==VoiceAction::OpenStandings&&r.league<0&&!r.group.empty()){ // infer the league from a conference or division name
  const std::string k=groupKey(r.group);
  if(k.rfind("AFC",0)==0||k.rfind("NFC",0)==0)r.league=1;
  else if(k.rfind("AL",0)==0||k.rfind("NL",0)==0||k.find("LEAGUE")!=std::string::npos)r.league=0;
  else if(k=="EAST"||k=="WEST"||k=="EASTERN"||k=="WESTERN"||k=="ATLANTIC"||k=="CENTRAL"||k=="SOUTHEAST"||k=="NORTHWEST"||k=="PACIFIC"||k=="SOUTHWEST")r.league=2;
  else r.league=3;}
 std::string abbr=upperAbbr(str("team")),opponent=upperAbbr(str("opponent"));
 if(!abbr.empty()){ // resolve the abbreviation against saved games, preferring the named league
  for(int pass=0;pass<2&&r.teamId.empty();pass++)for(int l=0;l<4&&r.teamId.empty();l++){
   if(pass==0&&l!=r.league)continue;if(pass==1&&l==r.league)continue;if(!feeds[l])continue;
   for(const auto& g:feeds[l]->games){const Team* ts[]={&g.away,&g.home};for(const Team* t:ts){if(upperAbbr(t->abbr)==abbr){r.teamAbbr=abbr;r.teamId=t->id;r.teamName=t->name;r.scope=t->conf;r.league=l;break;}}if(!r.teamId.empty())break;}
  }
 }
 // Degrade gracefully: a game we cannot find becomes the team page, a team we
 // cannot find becomes the league page, and no league at all becomes an answer.
 if(r.action==VoiceAction::OpenGame){int l=r.league;std::string id;if(!r.teamAbbr.empty()&&findGame(feeds,r.league,r.teamAbbr,opponent,l,id)){r.league=l;r.gameId=id;}else r.action=VoiceAction::OpenTeam;}
 if(r.action==VoiceAction::OpenTeam&&r.teamId.empty())r.action=VoiceAction::OpenLeague;
 if(r.action==VoiceAction::OpenStandings&&r.league==3&&r.scope.empty())r.scope=cfbConferenceId(r.group); // college standings are per conference
 if(r.action==VoiceAction::OpenStandings&&r.league==3&&r.scope.empty())r.action=VoiceAction::Answer;
 if(r.action==VoiceAction::OpenStandings&&r.league<0&&r.teamId.empty())r.action=VoiceAction::Answer;
 if(r.action==VoiceAction::OpenLeague&&r.league<0)r.action=VoiceAction::Answer;
 if((r.action==VoiceAction::Answer||r.action==VoiceAction::Fact)&&r.answer.empty()){r.error="empty answer";return r;}
 r.ok=true;return r;
}
// Recent-event facts: a follow-up text request with OpenRouter's web search
// attached (a few results, a fraction of a cent), still scoped to sports.
inline std::string lookupRequestBody(const std::string& question,const std::string& today){
 return "{\"model\":\""+std::string(VOICE_MODEL)+"\",\"plugins\":[{\"id\":\"web\",\"max_results\":3}],\"max_tokens\":300,\"temperature\":0.2,"
  "\"messages\":[{\"role\":\"system\",\"content\":\"You are a sports-only assistant for a child's scoreboard. Today is "+jsonEscape(today)+". "
  "Answer the sports question in one or two short plain sentences with the season or year, using the search results and preferring the most recent ones - the question is usually about the current season. No links, no markdown, no citations. "
  "If it is not about sports reply only: I only know about sports. If the results do not say, reply only: I'm not sure about that one.\"},"
  "{\"role\":\"user\",\"content\":\""+jsonEscape(question)+"\"}]}";
}
// Plain text from the lookup reply: JSON or fences unwrapped, [label](url) citations removed.
inline std::string parseLookupReply(JsonVariantConst root){
 const char* content=root["choices"][0]["message"]["content"].as<const char*>();if(!content)return "";
 std::string t=content;
 for(const char* fence:{"```json","```"}){for(size_t p;(p=t.find(fence))!=std::string::npos;)t.erase(p,strlen(fence));}
 {JsonDocument inner;size_t b=t.find('{');if(b!=std::string::npos&&!deserializeJson(inner,t.c_str()+b)&&inner["answer"].is<const char*>())t=inner["answer"].as<const char*>();}
 for(size_t p;(p=t.find("]("))!=std::string::npos;){size_t open=t.rfind('[',p);size_t close=t.find(')',p);if(open==std::string::npos||close==std::string::npos)break;t.erase(open,close-open+1);}
 std::string out;bool space=false;for(unsigned char c:t){if(c=='\n'||c=='\r'||c=='\t')c=' ';if(c==' '){if(space)continue;space=true;}else space=false;out+=(char)c;}
 while(!out.empty()&&(out.back()==' '||out.back()==','))out.pop_back();while(!out.empty()&&out.front()==' ')out.erase(0,1);
 return clean(out,240);
}
// Spoken replies: a second, streamed request to a speech model that reads the
// answer aloud. OpenRouter only returns audio when streaming, as SSE lines each
// carrying a base64 PCM16 chunk at 24 kHz.
static const char* TTS_MODEL="openai/gpt-audio-mini";
constexpr uint32_t TTS_RATE=24000;
inline std::string ttsRequestBody(const std::string& answer){
 return "{\"model\":\""+std::string(TTS_MODEL)+"\",\"modalities\":[\"text\",\"audio\"],\"audio\":{\"voice\":\"alloy\",\"format\":\"pcm16\"},\"stream\":true,\"max_tokens\":400,"
  "\"messages\":[{\"role\":\"system\",\"content\":\"You are the voice of a children's sports scoreboard toy. Your only job is to read the text you are given out loud, word for word, in a warm, upbeat voice. The text is already verified by the scoreboard; never comment on it, never add anything, never refuse.\"},"
  "{\"role\":\"user\",\"content\":\"Read this out loud: "+jsonEscape(answer)+"\"}]}";
}
inline size_t base64Decode(const char* in,size_t len,uint8_t* out,size_t cap){
 auto val=[](char c)->int{if(c>='A'&&c<='Z')return c-'A';if(c>='a'&&c<='z')return c-'a'+26;if(c>='0'&&c<='9')return c-'0'+52;if(c=='+')return 62;if(c=='/')return 63;return -1;};
 size_t o=0;uint32_t acc=0;int bits=0;
 for(size_t i=0;i<len;i++){int x=val(in[i]);if(x<0)continue;acc=(acc<<6)|x;bits+=6;if(bits>=8){bits-=8;if(o<cap)out[o++]=(acc>>bits)&255;}}
 return o;
}
// Walks the SSE body, decoding every audio chunk into `sink`; returns the transcript.
template<class Sink> inline std::string sseAudio(const char* body,size_t len,Sink sink){
 std::string transcript;size_t p=0;
 while(p<len){
  size_t e=p;while(e<len&&body[e]!='\n')e++;
  if(e-p>6&&!strncmp(body+p,"data: ",6)){
   const char* line=body+p+6;size_t n=e-p-6;
   for(size_t i=0;i+8<n;i++){
    if(!strncmp(line+i,"\"audio\":{",9)){
     const char* obj=line+i+9;size_t rem=n-i-9;
     for(size_t j=0;j+8<rem;j++){
      if(!strncmp(obj+j,"\"data\":\"",8)){size_t k=j+8;size_t st=k;while(k<rem&&obj[k]!='"')k++;sink(obj+st,k-st);}
      else if(!strncmp(obj+j,"\"transcript\":\"",14)){size_t k=j+14;while(k<rem&&obj[k]!='"'){if(obj[k]=='\\'&&k+1<rem)k++;transcript+=obj[k];k++;}}
      else if(obj[j]=='}')break;
     }
     break;
    }
   }
  }
  p=e+1;
 }
 return transcript;
}
// Drops silence before and after speech in a 16-bit mono clip so less is
// uploaded; keeps short margins. Returns the trimmed [offset, length).
inline std::pair<size_t,size_t> trimSilence(const int16_t* pcm,size_t samples,uint32_t rate){
 const size_t frame=rate/50;if(samples<frame*4)return {0,samples};
 const size_t frames=samples/frame;std::vector<int> rms(frames);int floor_=INT32_MAX;
 for(size_t f=0;f<frames;f++){int64_t acc=0;for(size_t i=0;i<frame;i++){int v=pcm[f*frame+i];acc+=v*v;}rms[f]=(int)std::sqrt((double)acc/frame);floor_=std::min(floor_,rms[f]);}
 const int threshold=std::max(120,floor_*4);
 size_t first=frames,last=0;for(size_t f=0;f<frames;f++)if(rms[f]>threshold){if(first==frames)first=f;last=f;}
 if(first==frames)return {0,samples};
 const size_t lead=8,tail=12; // 160 ms before, 240 ms after
 size_t a=first>lead?first-lead:0,b=std::min(frames,last+1+tail);
 return {a*frame,(b-a)*frame};
}
// Word-wrap for the 12x16 font (or any fixed advance), for answer text.
inline std::vector<std::string> wrapLines(const std::string& text,size_t maxChars,size_t maxLines){
 std::vector<std::string> out;std::string line,word;size_t p=0;
 while(p<=text.size()&&out.size()<maxLines){
  size_t q=text.find(' ',p);word=text.substr(p,q==std::string::npos?std::string::npos:q-p);
  if(word.size()>maxChars)word=word.substr(0,maxChars);
  std::string trial=line.empty()?word:line+" "+word;
  if(trial.size()>maxChars){out.push_back(line);line=word;}else line=trial;
  if(q==std::string::npos)break;p=q+1;
 }
 if(!line.empty()&&out.size()<maxLines)out.push_back(line);
 if(out.size()==maxLines&&(line.size()>maxChars||p<text.size()))out.back()=clean(out.back(),maxChars);
 return out;
}
}
