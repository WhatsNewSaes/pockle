#pragma once
#include <ArduinoJson.h>
#include <cstdlib>
#include "Core.h"
namespace retro {
inline void makeScoreFilter(JsonDocument& f){
 auto e=f["events"][0];e["id"]=true;e["date"]=true;e["status"]=true;e["week"]["number"]=true;e["season"]["type"]=true;e["seasonType"]["type"]=true;
 auto c=e["competitions"][0];c["status"]=true;c["venue"]["fullName"]=true;
 auto t=c["competitors"][0];t["homeAway"]=true;t["score"]=true;
 for(auto k:{"id","displayName","abbreviation","conferenceId"})t["team"][k]=true;
}

inline std::string js(JsonVariantConst v,const char* fallback="") {return v.is<const char*>()?v.as<const char*>():fallback;}
// Matchup detail keeps more of one event: records, line scores, team statistics, leaders, headline.
inline void makeDetailFilter(JsonDocument& f){
 auto e=f["events"][0];e["id"]=true;
 auto c=e["competitions"][0];c["headlines"][0]["shortLinkText"]=true;
 auto cl=c["leaders"][0];cl["name"]=true;cl["leaders"][0]["displayValue"]=true;cl["leaders"][0]["athlete"]["shortName"]=true;cl["leaders"][0]["team"]["id"]=true;
 auto t=c["competitors"][0];t["homeAway"]=true;t["team"]["id"]=true;t["records"][0]["type"]=true;t["records"][0]["summary"]=true;t["linescores"][0]["value"]=true;
 t["statistics"][0]["name"]=true;t["statistics"][0]["displayValue"]=true;
 auto tl=t["leaders"][0];tl["name"]=true;tl["leaders"][0]["displayValue"]=true;tl["leaders"][0]["athlete"]["shortName"]=true;
}
inline bool decodeDetail(JsonVariantConst root,const std::string& gameId,int league,GameDetail& out){
 if(!root["events"].is<JsonArrayConst>()||gameId.empty())return false;
 for(JsonObjectConst e:root["events"].as<JsonArrayConst>()){
  if(js(e["id"])!=gameId)continue;
  GameDetail d;d.id=gameId;auto c=e["competitions"][0];if(!c["competitors"].is<JsonArrayConst>())return false;
  std::string ids[2];std::vector<std::pair<std::string,std::string>> stats[2],leads[2];
  for(JsonObjectConst p:c["competitors"].as<JsonArrayConst>()){
   int t=js(p["homeAway"])=="home"?1:0;ids[t]=js(p["team"]["id"]);
   std::string rec;for(JsonObjectConst r:p["records"].as<JsonArrayConst>())if(rec.empty()||js(r["type"])=="total")rec=js(r["summary"]);
   (t?d.homeRecord:d.awayRecord)=clean(rec,8);
   auto& line=t?d.homeLine:d.awayLine;for(JsonVariantConst v:p["linescores"].as<JsonArrayConst>())if(line.size()<20)line.push_back((int)v["value"].as<float>());
   for(JsonObjectConst st:p["statistics"].as<JsonArrayConst>())stats[t].push_back({js(st["name"]),clean(js(st["displayValue"]),8)});
   for(JsonObjectConst l:p["leaders"].as<JsonArrayConst>()){auto top=l["leaders"][0];if(!top.isNull())leads[t].push_back({js(l["name"]),leaderText(js(top["athlete"]["shortName"]),js(top["displayValue"]),league)});}
  }
  auto pick=[](const std::vector<std::pair<std::string,std::string>>& v,const char* k){for(auto& p:v)if(p.first==k)return p.second;return std::string();};
  auto stat=[&](const char* k,const char* label){std::string a=pick(stats[0],k),h=pick(stats[1],k);if(!a.empty()||!h.empty())d.stats.push_back({label,a,h});};
  auto lead=[&](const char* k,const char* label){std::string a=pick(leads[0],k),h=pick(leads[1],k);if(!a.empty()||!h.empty())d.stats.push_back({label,a,h});};
  if(league==1||league==3){ // football leaders live on the competition, tagged by team
   const char* keys[]={"passingYards","rushingYards","receivingYards"};const char* labels[]={"PASS","RUSH","REC"};
   for(int k=0;k<3;k++){StatLine sl{labels[k],"",""};
    for(JsonObjectConst l:c["leaders"].as<JsonArrayConst>()){if(js(l["name"])!=keys[k])continue;
     for(JsonObjectConst x:l["leaders"].as<JsonArrayConst>()){std::string tid=js(x["team"]["id"]),txt=leaderText(js(x["athlete"]["shortName"]),js(x["displayValue"]),league);
      if(tid==ids[0]&&sl.away.empty())sl.away=txt;else if(tid==ids[1]&&sl.home.empty())sl.home=txt;}}
    if(!sl.away.empty()||!sl.home.empty())d.stats.push_back(sl);}
  }else if(league==2){stat("fieldGoalPct","FG%");stat("threePointPct","3PT%");stat("rebounds","REBS");stat("assists","ASSISTS");lead("points","PTS LDR");lead("rebounds","REB LDR");}
  else{d.awayHits=pick(stats[0],"hits");d.homeHits=pick(stats[1],"hits");d.awayErrors=pick(stats[0],"errors");d.homeErrors=pick(stats[1],"errors");lead("homeRuns","HR");lead("RBIs","RBI");lead("avg","BATTING");}
  d.headline=clean(js(c["headlines"][0]["shortLinkText"]),108);
  out=std::move(d);return true;
 }
 return false;
}
// `start` widens the accepted window to [start, date]; empty keeps the exact day;
// `allDates` keeps everything (team schedules span a season).
inline bool decodeScores(JsonVariantConst root,int league,const std::string& date,int64_t updated,Snapshot& output,const std::string& start="",bool allDates=false){
 if(!root["events"].is<JsonArrayConst>() || league<0||league>3||!validDate(date)||updated<1700000000)return false;
 const std::string first=start.empty()?date:start;if(!allDates&&(!validDate(first)||first>date))return false;
 Snapshot next;next.league=league;next.date=date;next.updated=updated;
 auto events=root["events"].as<JsonArrayConst>();if(events.size()>200)return false;
 for(JsonObjectConst e:events){
  Game g;g.id=js(e["id"]);g.start=js(e["date"]);
  const auto epoch=isoEpoch(g.start);
  auto c=e["competitions"][0]; auto status=e["status"];
  if(status.isNull())status=c["status"];
  g.state=js(status["type"]["state"]);g.status=clean(js(status["type"]["shortDetail"],"Status unavailable"));
  g.complete=status["type"]["completed"]|false;
  g.week=e["week"]["number"]|0;g.seasonType=e["season"]["type"]|0;if(!g.seasonType)g.seasonType=e["seasonType"]["type"]|0;if(g.week<0||g.week>30)g.week=0;
  g.venue=clean(js(c["venue"]["fullName"]));
  if(g.id.empty()||!epoch||g.state.empty()||!c["competitors"].is<JsonArrayConst>())return false;
  const std::string day=localDate(epoch);if(!allDates&&(day<first||day>date))continue; // NFL/college feeds return a whole week.
  for(JsonObjectConst p:c["competitors"].as<JsonArrayConst>()){
   Team t;t.id=js(p["team"]["id"]);t.name=clean(js(p["team"]["displayName"]));t.abbr=clean(js(p["team"]["abbreviation"]),10);t.conf=clean(js(p["team"]["conferenceId"]),6);t.score=clean(p["score"].is<JsonObjectConst>()?js(p["score"]["displayValue"]):js(p["score"]),8); // schedules nest the score
   if(js(p["homeAway"])=="home")g.home=t;else if(js(p["homeAway"])=="away")g.away=t;
  }
  if(g.home.id.empty()||g.away.id.empty()||g.home.name.empty()||g.away.name.empty())return false;
  next.games.push_back(g);
 }
 output=std::move(next);return true;
}
// Football matchups use ESPN's game summary: leaders per team, team totals, and the recap headline.
inline void makeSummaryFilter(JsonDocument& f){
 auto c=f["header"]["competitions"][0];auto t=c["competitors"][0];t["homeAway"]=true;t["team"]["id"]=true;t["record"][0]["summary"]=true;t["linescores"][0]["displayValue"]=true;
 auto l=f["leaders"][0];l["team"]["id"]=true;l["leaders"][0]["name"]=true;l["leaders"][0]["leaders"][0]["displayValue"]=true;l["leaders"][0]["leaders"][0]["athlete"]["shortName"]=true;
 auto b=f["boxscore"]["teams"][0];b["team"]["id"]=true;b["statistics"][0]["name"]=true;b["statistics"][0]["displayValue"]=true;
 f["article"]["headline"]=true;
}
inline bool decodeSummary(JsonVariantConst root,const std::string& gameId,int league,GameDetail& out){
 auto c=root["header"]["competitions"][0];if(!c["competitors"].is<JsonArrayConst>()||gameId.empty())return false;
 GameDetail d;d.id=gameId;std::string ids[2];
 for(JsonObjectConst p:c["competitors"].as<JsonArrayConst>()){int t=js(p["homeAway"])=="home"?1:0;ids[t]=js(p["team"]["id"]);
  (t?d.homeRecord:d.awayRecord)=clean(js(p["record"][0]["summary"]),8);
  auto& line=t?d.homeLine:d.awayLine;for(JsonVariantConst v:p["linescores"].as<JsonArrayConst>())if(line.size()<20)line.push_back(atoi(js(v["displayValue"],"0").c_str()));
 }
 auto side=[&](JsonVariantConst id){std::string s=js(id);return s==ids[1]?1:(s==ids[0]?0:-1);};
 StatLine pass{"PASS","",""},rush{"RUSH","",""},rec{"REC","",""};
 for(JsonObjectConst tl:root["leaders"].as<JsonArrayConst>()){int t=side(tl["team"]["id"]);if(t<0)continue;
  for(JsonObjectConst l:tl["leaders"].as<JsonArrayConst>()){auto top=l["leaders"][0];if(top.isNull())continue;std::string n=js(l["name"]);
   StatLine* sl=n=="passingYards"?&pass:n=="rushingYards"?&rush:n=="receivingYards"?&rec:nullptr;if(!sl)continue;
   (t?sl->home:sl->away)=leaderText(js(top["athlete"]["shortName"]),js(top["displayValue"]),league);}}
 for(auto* sl:{&pass,&rush,&rec})if(!sl->away.empty()||!sl->home.empty())d.stats.push_back(*sl);
 StatLine yds{"TOT YDS","",""},to{"TURNOVR","",""};
 for(JsonObjectConst bt:root["boxscore"]["teams"].as<JsonArrayConst>()){int t=side(bt["team"]["id"]);if(t<0)continue;
  for(JsonObjectConst st:bt["statistics"].as<JsonArrayConst>()){std::string n=js(st["name"]),v=clean(js(st["displayValue"]),8);if(n=="totalYards")(t?yds.home:yds.away)=v;else if(n=="turnovers")(t?to.home:to.away)=v;}}
 for(auto* sl:{&yds,&to})if(!sl->away.empty()||!sl->home.empty())d.stats.push_back(*sl);
 d.headline=clean(js(root["article"]["headline"]),108);
 if(d.awayRecord.empty()&&d.awayLine.empty()&&d.stats.empty())return false;
 out=std::move(d);return true;
}
// ESPN v2 standings: a tree of children with `standings.entries`; keep names,
// team ids, and the few stats the table shows. Three levels cover every league.
inline void standingsFilterNode(JsonObject n){
 n["name"]=true;n["abbreviation"]=true;JsonObject e=n["standings"]["entries"].add<JsonObject>();
 e["team"]["id"]=true;e["team"]["abbreviation"]=true;e["team"]["displayName"]=true;
 JsonObject st=e["stats"].add<JsonObject>();st["name"]=true;st["displayValue"]=true;st["value"]=true;
}
inline void makeStandingsFilter(JsonDocument& f){
 JsonObject root=f.to<JsonObject>();standingsFilterNode(root);
 JsonObject c1=root["children"].add<JsonObject>();standingsFilterNode(c1);
 JsonObject c2=c1["children"].add<JsonObject>();standingsFilterNode(c2);
 JsonObject c3=c2["children"].add<JsonObject>();standingsFilterNode(c3);
}
inline void standingsRows(JsonVariantConst node,int league,StandingsGroup& g){
 for(JsonObjectConst e:node["standings"]["entries"].as<JsonArrayConst>()){
  StandingRow r;r.id=js(e["team"]["id"]);r.abbr=clean(js(e["team"]["abbreviation"]),6);r.name=clean(js(e["team"]["displayName"]),40);
  std::string wins,losses,ties,overall,confRec;int seenWins=0;
  for(JsonObjectConst st:e["stats"].as<JsonArrayConst>()){std::string n=js(st["name"]),v=clean(js(st["displayValue"]),10);
   if(n=="wins"){if(seenWins++==0)wins=v;}else if(n=="losses")losses.empty()?losses=v:losses;else if(n=="ties")ties=v;else if(n=="winPercent")r.pct=v;
   else if(n=="gamesBehind"){if(r.extra.empty())r.extra=v;}else if(n=="streak"){if(r.streak.empty()||r.streak=="-")r.streak=v;}else if(n=="playoffSeed"){if(!r.seed)r.seed=atoi(v.c_str());}else if(n=="overall")overall=v;else if(n=="vs. Conf.")confRec=v;
   else if(n=="leagueWinPercent"&&r.winPct==0)r.winPct=st["value"].as<double>();
   if(n=="winPercent")r.winPct=st["value"].as<double>();}
  if(league==3){r.record=overall.empty()?wins+"-"+losses:overall;r.extra=confRec;r.pct="";} // college: overall, conference record
  else{r.record=wins+"-"+losses+(ties.empty()||ties=="0"?"":"-"+ties);if(league==1)r.extra="";}   // NFL shows pct, others games behind
  if(r.id.empty()||r.abbr.empty())continue;g.rows.push_back(r);if(g.rows.size()>=20)break;
 }
}
inline void standingsWalk(JsonVariantConst node,int league,int parent,Standings& out){
 if(out.groups.size()>=32)return;
 StandingsGroup g;g.name=clean(js(node["name"]),40);g.abbr=clean(js(node["abbreviation"]),10);g.parent=parent;standingsRows(node,league,g);
 out.groups.push_back(g);const int me=out.groups.size()-1;
 for(JsonObjectConst ch:node["children"].as<JsonArrayConst>())standingsWalk(ch,league,me,out);
}
inline bool decodeStandings(JsonVariantConst root,int league,int64_t now,Standings& out,const std::string& scope=""){
 Standings st;st.league=league;st.updated=now;st.scope=scope;
 if(root["standings"]["entries"].is<JsonArrayConst>())standingsWalk(root,league,-1,st); // a single conference
 else if(root["children"].is<JsonArrayConst>())for(JsonObjectConst ch:root["children"].as<JsonArrayConst>())standingsWalk(ch,league,-1,st);
 else return false;
 // Conferences without rows of their own: merge their divisions, best seed first.
 for(size_t g=0;g<st.groups.size();g++){auto& conf=st.groups[g];if(conf.parent!=-1||!conf.rows.empty())continue;
  for(size_t d=0;d<st.groups.size();d++)if(st.groups[d].parent==(int)g)for(const auto& r:st.groups[d].rows)conf.rows.push_back(r);
  std::stable_sort(conf.rows.begin(),conf.rows.end(),[](const StandingRow& a,const StandingRow& b){if(a.seed&&b.seed&&a.seed!=b.seed)return a.seed<b.seed;return a.winPct>b.winPct;});
 }
 st.groups.erase(std::remove_if(st.groups.begin(),st.groups.end(),[](const StandingsGroup& g){return g.rows.empty();}),st.groups.end());
 // Re-point parents after the removal: match by name.
 std::vector<std::string> names;for(auto& g:st.groups)names.push_back(g.name);
 (void)names;
 if(st.groups.empty())return false;out=std::move(st);return true;
}
inline void encodeStandings(const Standings& s,JsonDocument& doc){
 doc["version"]=1;doc["league"]=s.league;doc["updated"]=s.updated;doc["scope"]=s.scope;auto gs=doc["groups"].to<JsonArray>();
 for(const auto& g:s.groups){auto j=gs.add<JsonObject>();j["name"]=g.name;j["abbr"]=g.abbr;j["parent"]=g.parent;auto rs=j["rows"].to<JsonArray>();
  for(const auto& r:g.rows){auto x=rs.add<JsonObject>();x["id"]=r.id;x["abbr"]=r.abbr;x["name"]=r.name;x["rec"]=r.record;x["pct"]=r.pct;x["extra"]=r.extra;x["strk"]=r.streak;x["seed"]=r.seed;x["wp"]=r.winPct;}}
}
inline bool decodeStandingsCache(JsonVariantConst j,Standings& out){
 if(j["version"]!=1||!j["groups"].is<JsonArrayConst>())return false;
 Standings s;s.league=j["league"]|-1;s.updated=j["updated"]|int64_t(0);s.scope=js(j["scope"]);if(s.league<0||s.league>3)return false;
 for(JsonObjectConst g:j["groups"].as<JsonArrayConst>()){StandingsGroup sg;sg.name=js(g["name"]);sg.abbr=js(g["abbr"]);sg.parent=g["parent"]|-1;
  for(JsonObjectConst r:g["rows"].as<JsonArrayConst>()){StandingRow x;x.id=js(r["id"]);x.abbr=js(r["abbr"]);x.name=js(r["name"]);x.record=js(r["rec"]);x.pct=js(r["pct"]);x.extra=js(r["extra"]);x.streak=js(r["strk"]);x.seed=r["seed"]|0;x.winPct=r["wp"]|0.0;if(!x.id.empty())sg.rows.push_back(x);}
  s.groups.push_back(sg);}
 if(s.groups.empty())return false;out=std::move(s);return true;
}
inline void encodeCache(const Snapshot& s,JsonDocument& doc){
 doc["version"]=3;doc["league"]=s.league;doc["date"]=s.date;doc["updated"]=s.updated;doc["span"]=s.span;doc["team"]=s.team;
 auto c=doc["covered"].to<JsonArray>();for(const auto& d:s.covered)c.add(d);
 auto a=doc["games"].to<JsonArray>();for(const auto& g:s.games){auto j=a.add<JsonObject>();
 j["id"]=g.id;j["start"]=g.start;j["status"]=g.status;j["state"]=g.state;j["venue"]=g.venue;j["complete"]=g.complete;j["week"]=g.week;j["stype"]=g.seasonType;
 const Team* ts[]={&g.away,&g.home};const char* keys[]={"away","home"};for(int i=0;i<2;i++){auto t=j[keys[i]].to<JsonObject>();t["id"]=ts[i]->id;t["name"]=ts[i]->name;t["abbr"]=ts[i]->abbr;t["score"]=ts[i]->score;if(!ts[i]->conf.empty())t["conf"]=ts[i]->conf;}
 }
}
inline bool decodeCache(JsonVariantConst j,Snapshot& out){
 if(j["version"]!=3||!j["games"].is<JsonArrayConst>())return false;
 Snapshot s;s.league=j["league"]|-1;s.date=js(j["date"]);s.updated=j["updated"]|int64_t(0);
 if(s.league<0||s.league>3||!validDate(s.date)||s.updated<1700000000||j["games"].size()>200)return false;
 s.span=j["span"]|0;if(s.span<0||s.span>60)return false;s.team=clean(js(j["team"]),12);
 if(j["covered"].is<JsonArrayConst>())for(JsonVariantConst v:j["covered"].as<JsonArrayConst>()){std::string d=js(v);if(validDate(d)&&s.covered.size()<64)s.covered.push_back(d);}
 for(JsonObjectConst v:j["games"].as<JsonArrayConst>()){
  Game g;g.id=js(v["id"]);g.start=js(v["start"]);g.status=js(v["status"]);g.state=js(v["state"]);g.venue=js(v["venue"]);g.complete=v["complete"]|false;g.week=v["week"]|0;g.seasonType=v["stype"]|0;
  Team* ts[]={&g.away,&g.home};const char* ks[]={"away","home"};for(int i=0;i<2;i++){auto t=v[ks[i]];*ts[i]={js(t["id"]),js(t["name"]),js(t["abbr"]),js(t["score"]),js(t["conf"])};}
  if(g.id.empty()||g.away.id.empty()||g.home.id.empty()||g.state.empty())return false;
  s.games.push_back(g);
 }
 out=std::move(s);return true;
}
}
