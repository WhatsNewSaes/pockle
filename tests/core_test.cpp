#include <cassert>
#include <fstream>
#include <iostream>
#include "ScoreJson.h"
#include "Voice.h"
#include "Weather.h"
#include "Devotional.h"
using namespace retro;
int main(){
 setenv("TZ","EST5EDT,M3.2.0,M11.1.0",1);tzset();
 Button b;assert(b.update(true,0,true)==ButtonEvent::None);assert(b.update(true,31,true)==ButtonEvent::None);assert(b.update(false,100,true)==ButtonEvent::None);assert(b.update(false,131,true)==ButtonEvent::Click);
 Button hold;hold.update(true,0,true);hold.update(true,31,true);assert(hold.update(true,732,true)==ButtonEvent::Hold);assert(hold.update(true,800,true)==ButtonEvent::None);hold.update(false,850,true);assert(hold.update(false,881,true)==ButtonEvent::ReleaseHold);
 Button bounce;bounce.update(true,0);bounce.update(false,10);assert(bounce.update(false,50)==ButtonEvent::None);
 assert(shiftDate("20260308",1)=="20260309");assert(shiftDate("20261101",1)=="20261102");assert(shiftDate("20261231",1)=="20270101");assert(!validDate("20260230"));assert(localDate(isoEpoch("2026-09-30T01:00Z"))=="20260929");
 std::ifstream file("tests/fixtures/cfb.json");JsonDocument d,filter;makeScoreFilter(filter);auto error=deserializeJson(d,file,DeserializationOption::Filter(filter),DeserializationOption::NestingLimit(30));if(error){std::cerr<<error.c_str()<<"\n";return 1;};Snapshot s;assert(decodeScores(d,3,"20260926",1790460000,s));assert(!s.games.empty());assert(!s.games[0].home.conf.empty());
 for(auto& g:s.games)assert(localDate(isoEpoch(g.start))=="20260926");
 JsonDocument cache;encodeCache(s,cache);Snapshot restored;assert(decodeCache(cache,restored));assert(restored.updated==s.updated);assert(restored.games.size()==s.games.size());assert(restored.games[0].home.id==s.games[0].home.id);
 Snapshot sentinel=s;JsonDocument bad;bad["error"]="unavailable";assert(!decodeScores(bad,3,"20260926",1790460000,sentinel));assert(sentinel.updated==s.updated);assert(sentinel.games.size()==s.games.size());
 JsonDocument empty;empty["events"].to<JsonArray>();assert(decodeScores(empty,0,"20260926",1790460000,sentinel));assert(sentinel.games.empty());
 assert(!fresh(s,s.updated+1,false,false));assert(!fresh(s,s.updated+1,true,true));assert(fresh(s,s.updated+60,true,false));assert(!fresh(s,s.updated+180,true,false));assert(!fresh(s,s.updated-1,true,false));
 Game game;game.state="pre";Team team;team.score="0";assert(score(game,team)=="--");game.state="in";assert(score(game,team)=="0");team.score="";assert(score(game,team)=="--");
 for(int i=0;i<4;i++){const char* files[]={"mlb","nfl","nba","cfb"};const char* dates[]={"20260929","20260927","20250401","20260926"};std::ifstream feed(std::string("tests/fixtures/")+files[i]+".json");JsonDocument parsed;assert(!deserializeJson(parsed,feed,DeserializationOption::Filter(filter),DeserializationOption::NestingLimit(30)));Snapshot snap;assert(decodeScores(parsed,i,dates[i],1790460000,snap));}
 // Recent-games feed: a window keeps earlier days, merges sort newest first, replace a refetched day, dedupe moved games, prune the window, and track coverage.
 {std::ifstream f("tests/fixtures/nfl.json");JsonDocument parsed;assert(!deserializeJson(parsed,f,DeserializationOption::Filter(filter),DeserializationOption::NestingLimit(30)));
  Snapshot exact,window;assert(decodeScores(parsed,1,"20260929",1790460000,exact));assert(exact.games.empty());
  assert(decodeScores(parsed,1,"20260929",1790460000,window,"20260915"));assert(window.games.size()==2);assert(!decodeScores(parsed,1,"20260929",1790460000,window,"20261001"));
  Snapshot feed;feed.league=1;mergeFeed(feed,window,"20260927","20260915","20260929",14);assert(feed.games.size()==2&&feed.span==14&&feed.date=="20260929");
  assert(covers(feed,"20260927")&&!covers(feed,"20260929"));
  Snapshot today;today.updated=1790470000;Game g;g.id="later";g.start="2026-09-29T23:15Z";g.state="in";g.status="3rd Qtr";g.away={"1","A","AAA","7"};g.home={"2","B","BBB","3"};today.games.push_back(g);
  Game moved=window.games[0];moved.start="2026-09-29T20:00Z";today.games.push_back(moved);
  mergeFeed(feed,today,"20260929","20260915","20260929",14);assert(feed.games.size()==3);assert(feed.games[0].id=="later");assert(feed.games[1].id==moved.id&&gameDate(feed.games[1])=="20260929");assert(feed.updated==1790470000);
  for(size_t i=1;i<feed.games.size();i++)assert(isoEpoch(feed.games[i-1].start)>=isoEpoch(feed.games[i].start));
  auto missing=feedMissing(feed,"20260915","20260929");assert(missing[0]=="20260929");assert(missing.size()==4&&missing[1]=="20260928"&&missing[2]=="20260926");assert(feedHasGaps(feed,"20260915","20260929"));
  Snapshot none;mergeFeed(feed,none,"20260928","20260915","20260929",14);assert(feed.games.size()==3&&covers(feed,"20260928"));assert(feedMissing(feed,"20260915","20260929")[1]=="20260926");
  assert(dayLabel(isoEpoch(feed.games[0].start),isoEpoch("2026-09-29T12:00Z"))=="TODAY");assert(dayLabel(isoEpoch(feed.games[2].start),isoEpoch("2026-09-29T12:00Z"))=="SUN 9/27");assert(compactStatus("10:23 - 3rd")=="10:23 3RD");assert(clockLabel(isoEpoch("2026-09-30T00:15Z"))=="8:15 PM");
  JsonDocument c;encodeCache(feed,c);Snapshot back;assert(decodeCache(c,back));assert(back.span==14&&back.covered.size()==feed.covered.size()&&back.games.size()==3);
  mergeFeed(feed,none,"20261101","20261018","20261101",14);assert(feed.games.empty()&&feed.covered.size()==1);}
 // Week numbers and season type survive decoding and the cache; only football leagues label weeks.
 {std::ifstream f("tests/fixtures/nfl-week.json");JsonDocument parsed;assert(!deserializeJson(parsed,f,DeserializationOption::Filter(filter),DeserializationOption::NestingLimit(30)));
  Snapshot s;assert(decodeScores(parsed,1,"20260925",1790460000,s,"20260901"));assert(!s.games.empty()&&s.games[0].week==3&&s.games[0].seasonType==2);
  assert(weekLabel(1,s.games[0])=="WEEK 3");assert(weekLabel(0,s.games[0]).empty());Game post=s.games[0];post.seasonType=3;post.week=5;assert(weekLabel(1,post)=="SUPER BOWL");post.week=0;assert(weekLabel(1,post).empty());
  JsonDocument c;encodeCache(s,c);Snapshot back;assert(decodeCache(c,back));assert(back.games[0].week==3&&back.games[0].seasonType==2);}
 // Team schedule: every date kept, nested scores read, season type from `seasonType`; the page keeps results plus one upcoming game.
 {std::ifstream f("tests/fixtures/nfl-schedule.json");JsonDocument parsed;assert(!deserializeJson(parsed,f,DeserializationOption::Filter(filter),DeserializationOption::NestingLimit(30)));
  Snapshot s;assert(decodeScores(parsed,1,"20260929",1790460000,s,"",true));assert(s.games.size()==3);
  int post=0,pre=0;for(auto& g:s.games){if(g.state=="post"){post++;assert(!g.home.score.empty()&&g.home.score!="--"&&g.seasonType==2&&g.week>0);}if(g.state=="pre")pre++;}assert(post==2&&pre==1);
  Game later=s.games[0];later.id="far";later.state="pre";later.start="2026-12-25T18:00Z";s.games.push_back(later);Game soon=later;soon.id="soon";soon.start="2026-09-30T01:00Z";s.games.push_back(soon);
  s.team="12";trimUpcoming(s);assert(s.games.size()==3);assert(s.games[0].id=="soon");int pres=0;for(auto& g:s.games)if(g.state=="pre")pres++;assert(pres==1);for(size_t i=1;i<s.games.size();i++)assert(isoEpoch(s.games[i-1].start)>=isoEpoch(s.games[i].start));
  JsonDocument c;encodeCache(s,c);Snapshot back;assert(decodeCache(c,back));assert(back.team=="12"&&back.games.size()==s.games.size());}
 // Matchup detail: records, line scores, sport-specific stat lines and headline from real scoreboard events.
 {const char* files[]={"nfl-detail","mlb-detail","nba-detail"};const char* ids[]={"401872953","401907965","401705663"};int lg[]={1,0,2};JsonDocument df;makeDetailFilter(df);
  for(int i=0;i<3;i++){std::ifstream f(std::string("tests/fixtures/")+files[i]+".json");JsonDocument parsed;assert(!deserializeJson(parsed,f,DeserializationOption::Filter(df),DeserializationOption::NestingLimit(30)));
   GameDetail d;assert(decodeDetail(parsed,ids[i],lg[i],d));assert(d.id==ids[i]&&!d.awayRecord.empty()&&!d.homeRecord.empty()&&!d.stats.empty());GameDetail none;assert(!decodeDetail(parsed,"nope",lg[i],none));
   if(i==0){assert(d.awayLine.size()==4&&d.homeLine.size()==4);assert(d.stats[0].label=="PASS"&&d.stats[0].away.empty()!=d.stats[0].home.empty()&&(d.stats[0].away+d.stats[0].home).find("226")!=std::string::npos);assert(!d.headline.empty());}
   if(i==1){assert(d.awayLine.size()>=8&&!d.awayHits.empty()&&!d.homeErrors.empty());}
   if(i==2){assert(d.stats[0].label=="FG%"&&d.awayLine.size()==4);}
  }
  assert(leaderText("J. Herbert","20/34, 226 YDS, 1 TD, 1 INT",1)=="Herbert 226");assert(leaderText("J. Cook III","24 CAR, 154 YDS, 1 TD",1)=="Cook III 154");assert(leaderText("T. Young","29",2)=="Young 29");assert(leaderText("A. Judge","1-3, HR, 3 RBI, R",0)=="Judge 1-3 HR");assert(leaderText("Bartholomew-Longname","12 REC, 140 YDS",3).size()<=13);assert(leaderText("J.K. Dobbins","12 CAR, 49 YDS",1)=="Dobbins 49");}
 // Football game summary: per-team leaders, totals, records, line scores, headline.
 {std::ifstream f("tests/fixtures/nfl-summary.json");JsonDocument sf;makeSummaryFilter(sf);JsonDocument parsed;assert(!deserializeJson(parsed,f,DeserializationOption::Filter(sf),DeserializationOption::NestingLimit(30)));
  GameDetail d;assert(decodeSummary(parsed,"401872953",1,d));assert(d.awayRecord=="0-3"&&d.homeRecord=="3-0");assert(d.awayLine.size()==4&&d.homeLine[3]==14);
  assert(d.stats.size()==5&&d.stats[0].label=="PASS"&&d.stats[0].away=="Herbert 226"&&d.stats[0].home=="Allen 204");assert(d.stats[1].home=="Cook III 154");assert(d.stats[3].label=="TOT YDS"&&d.stats[3].away=="348"&&d.stats[4].label=="TURNOVR"&&d.stats[4].home=="5");assert(d.headline.find("Bills")!=std::string::npos);}
 // Standings: divisions under conferences (pro), conferences (college), team lookup, group matching, cache round trip.
 {const char* files[]={"mlb","nfl","nba","cfb"};JsonDocument sf;makeStandingsFilter(sf);
  for(int l=0;l<4;l++){std::ifstream f(std::string("tests/fixtures/")+files[l]+"-standings.json");JsonDocument parsed;auto err=deserializeJson(parsed,f,DeserializationOption::Filter(sf),DeserializationOption::NestingLimit(30));if(err){std::cerr<<files[l]<<": "<<err.c_str()<<"\n";return 1;}
   Standings st;assert(decodeStandings(parsed,l,1790460000,st));
   if(l==1){assert(st.groups.size()==10);int d,c;teamGroups(st,"3",d,c);assert(d>=0&&c>=0&&st.groups[d].name=="NFC North"&&st.groups[c].abbr=="NFC"&&st.groups[d].rows.size()==4&&st.groups[c].rows.size()==16);
    assert(findGroup(st,"nfc north")==d&&findGroup(st,"NFC")==c&&findGroup(st,"afc east")>=0&&findGroup(st,"nowhere")==-1);const auto& r=st.groups[d].rows[0];assert(!r.record.empty()&&!r.pct.empty()&&r.record.find('-')!=std::string::npos);
    for(size_t i=1;i<st.groups[c].rows.size();i++)assert(st.groups[c].rows[i-1].seed<=st.groups[c].rows[i].seed||st.groups[c].rows[i].seed==0);}
   if(l==0){int d,c;teamGroups(st,"10",d,c);assert(d>=0&&st.groups[d].name=="American League East"&&st.groups[c].rows.size()==15&&!st.groups[d].rows[0].extra.empty());}
   if(l==2){int d,c;teamGroups(st,"2",d,c);assert(d>=0&&st.groups[d].name=="Atlantic"&&st.groups[c].abbr=="East");}
   if(l==3){std::ifstream sf2("tests/fixtures/cfb-sec-standings.json");JsonDocument one;assert(!deserializeJson(one,sf2,DeserializationOption::Filter(sf),DeserializationOption::NestingLimit(30)));Standings sec;assert(decodeStandings(one,3,1790460000,sec,"8"));assert(sec.groups.size()==1&&sec.scope=="8"&&sec.groups[0].abbr=="sec"&&sec.groups[0].rows.size()==16);
    assert(cfbConferenceId("SEC")=="8"&&cfbConferenceId("Big Ten")=="5"&&cfbConferenceId("the ACC standings")=="1"&&cfbConferenceId("nowhere").empty());
    int d,c;teamGroups(st,"333",d,c);assert(d==-1&&c>=0&&st.groups[c].abbr=="sec"&&st.groups[c].rows[0].record.find('-')!=std::string::npos&&st.groups[c].rows[0].extra.find('-')!=std::string::npos);assert(findGroup(st,"SEC")==c);
    int d2,c2;teamGroups(st,st.groups[findGroup(st,"Sun Belt - East")].rows[0].id,d2,c2);assert(d2>=0&&c2>=0&&st.groups[c2].abbr=="belt");}
   JsonDocument cache;encodeStandings(st,cache);Standings back;assert(decodeStandingsCache(cache,back));assert(back.groups.size()==st.groups.size()&&back.groups[0].rows.size()==st.groups[0].rows.size()&&back.groups[0].rows[0].record==st.groups[0].rows[0].record);}}
 // Home feed: played/in-progress games from every league, newest first, capped.
 {Snapshot a;a.league=0;Game g1;g1.id="a1";g1.start="2026-09-29T22:00Z";g1.state="in";Game g2;g2.id="a2";g2.start="2026-09-30T23:00Z";g2.state="pre";a.games={g2,g1};
  Snapshot b;b.league=1;Game g3;g3.id="b1";g3.start="2026-09-28T00:15Z";g3.state="post";Game g4;g4.id="b2";g4.start="2026-09-29T23:15Z";g4.state="post";b.games={g4,g3};
  const Snapshot* f[4]={&a,&b,nullptr,nullptr};auto rec=recentGames(f,10);assert(rec.size()==3&&rec[0].game.id=="b2"&&rec[1].game.id=="a1"&&rec[2].game.id=="b1"&&rec[0].league==1);assert(recentGames(f,2).size()==2);
  auto grp=recentGamesGrouped(f,1000);assert(grp.size()==3&&grp[0].league==1&&grp[1].league==1&&grp[2].league==0); // NFL had the newest game, so it leads
  auto tight=recentGamesGrouped(f,28+28+56+56);assert(tight.size()==2&&tight[0].league==1&&tight[1].league==0); // two rows fit: one per league, not two NFL
  auto three=recentGamesGrouped(f,28+28+56*3);assert(three.size()==3);}
 // Voice: JSON escaping, grounded context, reply parsing with team resolution, wrapping.
 {assert(jsonEscape("a\"b\\c\nd")=="a\\\"b\\\\c\\nd");
  Snapshot nfl;nfl.league=1;Game g;g.id="x";g.start="2026-09-27T20:25Z";g.state="post";g.status="Final";g.week=3;g.seasonType=2;g.away={"14","Los Angeles Rams","LAR","26"};g.home={"7","Denver Broncos","DEN","30"};nfl.games.push_back(g);
  const Snapshot* feeds[4]={nullptr,&nfl,nullptr,nullptr};std::vector<Favorite> favs{{1,"3","Chicago Bears"}};
  std::string ctx=voiceContext(feeds,favs,isoEpoch("2026-09-30T14:00Z"));
  assert(ctx.find("NFL: LAR (away) 26 at DEN (home) 30 (FINAL, SUN 9/27, week 3, DEN won)")!=std::string::npos);assert(ctx.find("MLB: nothing saved")!=std::string::npos);assert(ctx.find("Chicago Bears (NFL)")!=std::string::npos);
  const char* reply="{\"choices\":[{\"message\":{\"content\":\"{\\\"heard\\\": \\\"Rams score\\\", \\\"action\\\": \\\"open_game\\\", \\\"league\\\": \\\"NFL\\\", \\\"team\\\": \\\"lar\\\", \\\"opponent\\\": null, \\\"answer\\\": \\\"Opening the Rams game\\\"}\"}}]}";
  JsonDocument d;assert(!deserializeJson(d,reply));VoiceReply r=parseVoiceReply(d,feeds);
  assert(r.ok&&r.heard=="Rams score"&&r.action==VoiceAction::OpenGame&&r.league==1&&r.teamId=="14"&&r.teamName=="Los Angeles Rams"&&r.gameId=="x");
  JsonDocument d2;deserializeJson(d2,"{\"choices\":[{\"message\":{\"content\":\"{\\\"heard\\\":\\\"Bears next game\\\",\\\"action\\\":\\\"open_team\\\",\\\"league\\\":\\\"NFL\\\",\\\"team\\\":\\\"CHI\\\",\\\"answer\\\":\\\"ok\\\"}\"}}]}");
  VoiceReply t2=parseVoiceReply(d2,feeds);assert(t2.ok&&t2.action==VoiceAction::OpenLeague&&t2.league==1); // CHI is not in the saved games, so the league page
  JsonDocument d3;deserializeJson(d3,"{\"choices\":[{\"message\":{\"content\":\"{\\\"heard\\\":\\\"Rams vs Broncos\\\",\\\"action\\\":\\\"open_game\\\",\\\"league\\\":null,\\\"team\\\":\\\"LAR\\\",\\\"opponent\\\":\\\"KC\\\",\\\"answer\\\":\\\"ok\\\"}\"}}]}");
  VoiceReply t3=parseVoiceReply(d3,feeds);assert(t3.ok&&t3.action==VoiceAction::OpenTeam&&t3.teamId=="14"); // no LAR-KC game saved: the team page instead
  JsonDocument d4;deserializeJson(d4,"{\"choices\":[{\"message\":{\"content\":\"{\\\"heard\\\":\\\"who is taller\\\",\\\"action\\\":\\\"answer\\\",\\\"league\\\":null,\\\"team\\\":null,\\\"answer\\\":\\\"I don't have that.\\\"}\"}}]}");
  VoiceReply t4=parseVoiceReply(d4,feeds);assert(t4.ok&&t4.action==VoiceAction::Answer&&t4.answer=="I don't have that.");
  JsonDocument d5;deserializeJson(d5,"{\"choices\":[{\"message\":{\"content\":\"{\\\"heard\\\":\\\"1985 Super Bowl\\\",\\\"action\\\":\\\"fact\\\",\\\"league\\\":\\\"NFL\\\",\\\"team\\\":null,\\\"answer\\\":\\\"The 49ers won Super Bowl XIX in January 1985.\\\"}\"}}]}");
  VoiceReply t5=parseVoiceReply(d5,feeds);assert(t5.ok&&t5.action==VoiceAction::Fact&&t5.league==1&&t5.answer.find("49ers")!=std::string::npos);
  JsonDocument d6;deserializeJson(d6,"{\"choices\":[{\"message\":{\"content\":\"{\\\"heard\\\":\\\"x\\\",\\\"action\\\":\\\"fact\\\",\\\"answer\\\":\\\"\\\"}\"}}]}");assert(!parseVoiceReply(d6,feeds).ok);
  JsonDocument d7;deserializeJson(d7,"{\"choices\":[{\"message\":{\"content\":\"{\\\"heard\\\":\\\"last Super Bowl\\\",\\\"action\\\":\\\"fact\\\",\\\"answer\\\":\\\"I'm not sure about that one.\\\",\\\"needs_lookup\\\":true}\"}}]}");
  VoiceReply t7=parseVoiceReply(d7,feeds);assert(t7.ok&&t7.action==VoiceAction::Fact&&t7.lookup);
  JsonDocument l1;deserializeJson(l1,"{\"choices\":[{\"message\":{\"content\":\"```json\\n{\\n \\\"answer\\\": \\\"The Seattle Seahawks won Super Bowl LX in 2026, defeating the Patriots 29-13. [nbc.com](https://www.nbc.com/x), [espn.com](https://espn.com/y)\\\"}\\n```\"}}]}");
  assert(parseLookupReply(l1)=="The Seattle Seahawks won Super Bowl LX in 2026, defeating the Patriots 29-13.");
  JsonDocument l2;deserializeJson(l2,"{\"choices\":[{\"message\":{\"content\":\"The Dodgers won the 2025 World Series.\"}}]}");assert(parseLookupReply(l2)=="The Dodgers won the 2025 World Series.");
  assert(lookupRequestBody("who won?","Sep 30").find("\"plugins\":[{\"id\":\"web\"")!=std::string::npos);
  JsonDocument e;deserializeJson(e,"{\"error\":{\"message\":\"Invalid key\"}}");VoiceReply bad=parseVoiceReply(e,feeds);assert(!bad.ok&&bad.error=="Invalid key");
  JsonDocument n;deserializeJson(n,"{\"choices\":[{\"message\":{\"content\":\"not json\"}}]}");assert(!parseVoiceReply(n,feeds).ok);
  auto lines=wrapLines("The Rams lost to the Broncos thirty to twenty six on Sunday night",20,3);assert(lines.size()==3&&lines[0]=="The Rams lost to the"&&lines[2].size()<=20);
  assert(wrapLines("short",36,4).size()==1);assert(leagueIndex("cfb")==3&&leagueIndex("")==-1);
  // Spoken replies: base64 and the streamed audio extraction against a real SSE sample.
  uint8_t out[16];assert(base64Decode("aGVsbG8=",8,out,sizeof(out))==5&&!memcmp(out,"hello",5));
  std::ifstream sf("tests/fixtures/tts-stream.txt");std::string sse((std::istreambuf_iterator<char>(sf)),std::istreambuf_iterator<char>());assert(!sse.empty());
  size_t chunks=0,bytes=0;std::vector<uint8_t> pcm(200000);
  std::string said=sseAudio(sse.data(),sse.size(),[&](const char* b64,size_t n){chunks++;bytes+=base64Decode(b64,n,pcm.data()+bytes,pcm.size()-bytes);});
  assert(chunks>=1&&bytes>1000&&bytes%2==0);
  std::vector<int16_t> clip(16000*3,0);for(size_t i=16000;i<32000;i++)clip[i]=(int16_t)((i%40<20)?3000:-3000);
  auto cut=trimSilence(clip.data(),clip.size(),16000);assert(cut.first>=16000-8*320&&cut.first<=16000&&cut.first+cut.second>=32000&&cut.first+cut.second<=32000+12*320);
  std::vector<int16_t> quiet(16000,5);auto keep=trimSilence(quiet.data(),quiet.size(),16000);assert(keep.first==0&&keep.second==quiet.size());assert(ttsRequestBody("Go \"Bears\"").find("Read this out loud: Go \\\"Bears\\\"")!=std::string::npos);(void)said;}
  // Game-aware refresh: live or imminent games keep the 15-minute cadence; otherwise sleep until the next start, at most six hours.
  {const int64_t now=1790800000;Snapshot live;live.updated=now-60;Game a;a.state="in";a.start="2026-09-30T23:00Z";live.games={a};
   Snapshot soon;soon.updated=now-60;Game b;b.state="pre";b.start="2026-09-30T21:30:00Z";soon.games={b}; // 1790800000 is 2026-09-30T20:26:40Z
   Snapshot later;later.updated=now-60;Game c;c.state="pre";c.start="2026-10-01T17:00Z";later.games={c};
   Snapshot done;done.updated=now-60;Game d;d.state="post";d.start="2026-09-29T23:00Z";done.games={d};
   Snapshot old=done;old.updated=now-13*3600;
   assert(leagueActive(live,now)&&!leagueActive(soon,now)&&!leagueActive(later,now)&&!leagueActive(done,now));
   Game e;e.state="pre";e.start="2026-09-30T20:35Z";Snapshot imminent;imminent.updated=now;imminent.games={e};assert(leagueActive(imminent,now));
   Game f;f.state="pre";f.start="2026-09-30T18:00Z";Snapshot overdue=imminent;overdue.games={f};assert(leagueActive(overdue,now)); // should have started: the cache is behind
   assert(nextGameStart(soon,now)==isoEpoch("2026-09-30T21:30:00Z")&&nextGameStart(done,now)==0);
   const Snapshot* a1[4]={&live,&done,nullptr,&later};assert(refreshDelay(a1,now)==900&&refreshMask(a1,now)==(1|4));
   const Snapshot* a2[4]={&soon,&done,&later,nullptr};assert(refreshDelay(a2,now)==isoEpoch("2026-09-30T21:30:00Z")-now&&refreshMask(a2,now)==8);
   const Snapshot* a3[4]={&done,&done,&done,&done};assert(refreshDelay(a3,now)==6*3600&&refreshMask(a3,now)==0);
   const Snapshot* a4[4]={&old,&done,&done,&done};assert(refreshMask(a4,now)==1);
   const Snapshot* a5[4]={&later,&done,&done,&done};assert(refreshDelay(a5,now)==6*3600);}
  // Weather: Open-Meteo and ipinfo decoding, words, icons, cache roundtrip.
  {JsonDocument wj;deserializeJson(wj,"{\"current\":{\"time\":\"2026-09-30T20:45\",\"temperature_2m\":61.8,\"weather_code\":3,\"is_day\":0},\"daily\":{\"temperature_2m_max\":[64.7],\"temperature_2m_min\":[58.9],\"precipitation_probability_max\":[92]}}");
   Weather w;w.city="Williamsburg";assert(decodeWeather(wj,1790800000,w)&&w.valid&&w.temp==62&&w.high==65&&w.low==59&&w.rain==92&&w.code==3&&!w.day&&w.city=="Williamsburg");
   assert(weatherLine(w)=="HI 65  LO 59  RAIN 92%  CLOUDY");assert(weatherSpeech(w).rfind("now 62 F and cloudy in Williamsburg; today high 65, low 59, 92% chance of rain",0)==0);
   {std::ifstream wf("tests/fixtures/weather.json");JsonDocument full;assert(!deserializeJson(full,wf));Weather f;f.city="Painesville";assert(decodeWeather(full,1790800000,f)&&f.days.size()==7&&f.days[0].date=="2026-09-30"&&f.days[2].code==63&&f.days[2].high==73&&f.days[2].low==54&&f.days[2].rain==89);
    assert(weatherDayName(f.days[0].date,0)=="TODAY"&&weatherDayName(f.days[1].date,1)=="THU 10/1"&&weatherDayName("2026-10-04",4)=="SUN 10/4");
    Weather rt;assert(decodeWeatherCache(encodeWeatherCache(f),rt)&&rt.days.size()==7&&rt.days[6].date=="2026-10-06"&&rt.days[6].high==54&&rt.days[6].rain==1&&rt.city=="Painesville");
    assert(weatherSpeech(f).find("FRI 10/2 high 73 low 54 89% rain rain")!=std::string::npos);}
   assert(std::string(weatherWord(0))=="CLEAR"&&std::string(weatherWord(61))=="RAIN"&&std::string(weatherWord(95))=="THUNDERSTORMS"&&weatherIcon(0,false)==1&&weatherIcon(2,true)==2&&weatherIcon(80,true)==4);
   Weather back;assert(decodeWeatherCache(encodeWeatherCache(w),back)&&back.temp==62&&back.city=="Williamsburg"&&back.fetched==1790800000&&!back.day);assert(!decodeWeatherCache("",back));
   JsonDocument lj;deserializeJson(lj,"{\"city\":\"Williamsburg\",\"loc\":\"39.0542,-84.0530\"}");std::string la,lo,ci;assert(decodeLocation(lj,la,lo,ci)&&la=="39.0542"&&lo=="-84.0530"&&ci=="Williamsburg");
   assert(weatherUrl(la,lo).find("latitude=39.0542&longitude=-84.0530")!=std::string::npos);
   JsonDocument gj;deserializeJson(gj,"{\"results\":[{\"name\":\"Painesville\",\"latitude\":41.72449,\"longitude\":-81.24566,\"postcodes\":[\"44077\"]}]}");assert(decodeGeocode(gj,la,lo,ci)&&la=="41.7245"&&lo=="-81.2457"&&ci=="Painesville");
   JsonDocument ej;deserializeJson(ej,"{\"generationtime_ms\":0.1}");assert(!decodeGeocode(ej,la,lo,ci));assert(geocodeUrl("New York")=="https://geocoding-api.open-meteo.com/v1/search?name=New%20York&count=1&language=en&format=json");
   const Snapshot* nf[4]={nullptr,nullptr,nullptr,nullptr};assert(voiceContext(nf,{},1790800000,weatherSpeech(w)).find("Weather (7-day forecast): now 62 F")!=std::string::npos);}
  // Devotional: the model reply, the NVS roundtrip, the spoken text.
  {std::ifstream df("tests/fixtures/devotional.json");std::string fixture((std::istreambuf_iterator<char>(df)),std::istreambuf_iterator<char>());assert(!fixture.empty());
   JsonDocument wrap;wrap["choices"][0]["message"]["content"]=fixture;Devotional d;BibleRef ref{19,119,9};
   assert(parseDevotional(wrap,ref,"How can a young man keep his way pure? By guarding it according to Your word.",272,d)&&d.valid&&d.points.size()==3&&d.day==272);
   Devotional back;assert(decodeDevotional(encodeDevotional(d),back)&&back.valid&&back.ref.verse==9&&back.points[2]==d.points[2]&&back.prayer==d.prayer);
   const std::string speech=devotionalSpeech(d);assert(speech.find("Keep Your Way Pure")!=std::string::npos&&speech.find("Psalm 119:9 says")!=std::string::npos&&speech.find("Let's pray.")!=std::string::npos);
   assert(devotionalRequestBody(ref,d.verse,"context").find("Verse of the day: Psalm 119:9")!=std::string::npos);assert(!decodeDevotional("",back));
   const Snapshot* nf[4]={nullptr,nullptr,nullptr,nullptr};JsonDocument vj;deserializeJson(vj,"{\"choices\":[{\"message\":{\"content\":\"{\\\"heard\\\":\\\"read today's devotional\\\",\\\"action\\\":\\\"open_devotional\\\",\\\"read\\\":true,\\\"answer\\\":\\\"ok\\\"}\"}}]}");
   VoiceReply vr=parseVoiceReply(vj,nf);assert(vr.ok&&vr.action==VoiceAction::OpenDevotional&&vr.read);
   std::ifstream ff("devotionals/out/10-01.json");std::string file((std::istreambuf_iterator<char>(ff)),std::istreambuf_iterator<char>());assert(!file.empty());
   Devotional fd;auto verseFor=[](const BibleRef& r){return r.book==19&&r.chapter==119&&r.verse==9?std::string("How can a young man keep his way pure? By guarding it according to Your word."):std::string();};
   assert(decodeDevotionalFile(file,273,{43,3,16},"fallback",verseFor,fd)&&fd.valid&&fd.fromFile&&fd.ref.book==19&&fd.ref.verse==9&&fd.title=="Clean Heart, Clean Path"&&fd.points.size()==3);
   Devotional fb;assert(decodeDevotional(encodeDevotional(fd),fb)&&fb.fromFile);assert(!decodeDevotionalFile("{}",273,{43,3,16},"x",verseFor,fb));
   assert(devotionalFileUrl(10,1)=="https://raw.githubusercontent.com/WhatsNewSaes/pockle/main/devotionals/out/10-01.json");}
  // Bible: book lookup, references, layout, verse of the day.
  assert(bibleBookIndex("John")==43&&bibleBookIndex("1 John")==62&&bibleBookIndex("First John")==62&&bibleBookIndex("Psalms")==19&&bibleBookIndex("Song of Songs")==22&&bibleBookIndex("Judg")==7&&bibleBookIndex("Jude")==65&&bibleBookIndex("Rev")==66&&bibleBookIndex("xyz")==0&&bibleBookIndex("Phil")==50);
  {BibleRef r=parseBibleRef("John 3:16");assert(r.book==43&&r.chapter==3&&r.verse==16);r=parseBibleRef("1 Corinthians 13");assert(r.book==46&&r.chapter==13&&r.verse==0);
   r=parseBibleRef("psalm 23 verse 4");assert(r.book==19&&r.chapter==23&&r.verse==4);r=parseBibleRef("Jude 24");assert(r.book==65&&r.chapter==1&&r.verse==24);
   r=parseBibleRef("john 99");assert(r.book==43&&r.chapter==1);assert(parseBibleRef("nothing here").book==0);assert(biblePath(43,3)=="/bible/43/003.txt");}
  {std::ifstream jf("tests/fixtures/john3.txt");std::string text((std::istreambuf_iterator<char>(jf)),std::istreambuf_iterator<char>());auto verses=bibleVerses(text);assert(verses.size()==36);
   auto width=[](const std::string& s){return int(s.size())*6;};auto pages=paginateBible(verses,width,456,36);assert(pages.size()>=2);
   for(auto& p:pages){assert(p.lines.size()<=36);for(auto& l:p.lines){int w=0;bool firstSeg=true;for(auto& s:l.segs){w+=(firstSeg?0:6)+(s.verse?bibleMarkerWidth(s.verse):0)+width(s.text);firstSeg=false;assert(s.of>=1);}assert(w<=456);}}
   assert(pages[0].lines[0].segs[0].verse==1&&pages[0].firstVerse==1);const int p16=biblePageOf(pages,16);assert(p16>=0&&p16<(int)pages.size());
   std::string all;for(auto& p:pages)for(auto& l:p.lines)for(auto& s:l.segs)all+=s.text+" ";assert(all.find("For God so loved")!=std::string::npos);
   assert(bibleSpeakText(verses,{43,3,16}).rfind("For God so loved",0)==0);assert(bibleSpeakText(verses,{43,3,0}).size()<=620);
   auto wrapped=wrapWidth(verses[15],width,456,10);assert(wrapped.size()>=2&&wrapped[0].size()*6<=456);assert(wrapWidth("a b c",width,456,1).size()==1);}
  assert(verseOfDay(0).book==43&&verseOfDay(1).book==19&&verseOfDay(365).valid()&&verseOfDay(1000).valid());assert(bibleRefLabel({43,3,16})=="JOHN 3:16"&&bibleRefLabel({62,1,0},false)=="1 John 1");
  {const Snapshot* nf[4]={nullptr,nullptr,nullptr,nullptr};JsonDocument bj;deserializeJson(bj,"{\"choices\":[{\"message\":{\"content\":\"{\\\"heard\\\":\\\"read John 3:16\\\",\\\"action\\\":\\\"open_bible\\\",\\\"book\\\":\\\"John\\\",\\\"chapter\\\":\\\"3\\\",\\\"verse\\\":16,\\\"read\\\":true,\\\"daily\\\":false,\\\"answer\\\":\\\"Opening John 3:16\\\"}\"}}]}");
   VoiceReply v=parseVoiceReply(bj,nf);assert(v.ok&&v.action==VoiceAction::OpenBible&&v.bible.book==43&&v.bible.chapter==3&&v.bible.verse==16&&v.read&&!v.daily);
   JsonDocument dj;deserializeJson(dj,"{\"choices\":[{\"message\":{\"content\":\"{\\\"heard\\\":\\\"verse of the day\\\",\\\"action\\\":\\\"open_bible\\\",\\\"book\\\":null,\\\"daily\\\":true,\\\"answer\\\":\\\"ok\\\"}\"}}]}");
   VoiceReply d=parseVoiceReply(dj,nf);assert(d.ok&&d.action==VoiceAction::OpenBible&&d.daily&&d.bible.book==0);
   JsonDocument hj;deserializeJson(hj,"{\"choices\":[{\"message\":{\"content\":\"{\\\"heard\\\":\\\"go to Psalm 23\\\",\\\"action\\\":\\\"open_bible\\\",\\\"answer\\\":\\\"ok\\\"}\"}}]}");
   VoiceReply h=parseVoiceReply(hj,nf);assert(h.ok&&h.bible.book==19&&h.bible.chapter==23);
   JsonDocument wj2;deserializeJson(wj2,"{\"choices\":[{\"message\":{\"content\":\"{\\\"heard\\\":\\\"will it rain tomorrow\\\",\\\"action\\\":\\\"open_weather\\\",\\\"answer\\\":\\\"Tomorrow looks dry, high 81 with a 30% chance of rain.\\\"}\"}}]}");
   VoiceReply wv=parseVoiceReply(wj2,nf);assert(wv.ok&&wv.action==VoiceAction::OpenWeather&&wv.answer.rfind("Tomorrow",0)==0);}
 std::cout<<"PASS: debounce, hold exclusivity, dates/DST, ESPN normalization, cache roundtrip, malformed response retention, freshness, missing scores, recent-games feed merging, Bible references/layout, weather decoding, game-aware refresh planning, and the devotional\n";
}
