#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include <ctime>
#include <cstdio>
#include <cctype>
#include <algorithm>

namespace retro {
struct League { const char* name; const char* path; const char* subtitle; };
static const League leagues[] = {
 {"MLB", "baseball/mlb", "MAJOR LEAGUE BASEBALL"},
 {"NFL", "football/nfl", "NATIONAL FOOTBALL LEAGUE"},
 {"NBA", "basketball/nba", "NATIONAL BASKETBALL ASSN."},
 {"COLLEGE FB", "football/college-football", "NCAA DIVISION I FBS"}
};
struct Team { std::string id, name, abbr, score, conf; int rank=0; }; // conf: college conference id; rank: Top-25 rank or 0
struct Game { std::string id, start, status, state, venue; Team away, home; bool complete=false; int week=0, seasonType=0; };
// Recent-games window per league, in days back from today. ESPN's scoreboard
// rejects date ranges, so the device walks days and merges them into one feed.
// The NFL window spans four weeks so the week headers have something to divide.
static const int feedDays[] = {7, 28, 7, 8};
// Football leagues group the scoreboard by week; other leagues have no week.
inline std::string weekLabel(int league,const Game& g){
 if(g.week<=0||(league!=1&&league!=3))return "";
 if(g.seasonType==1)return "PRESEASON WEEK "+std::to_string(g.week);
 if(g.seasonType==3){static const char* rounds[]={"","WILD CARD","DIVISIONAL ROUND","CONFERENCE FINALS","PRO BOWL","SUPER BOWL"};if(league==1&&g.week>=1&&g.week<=5)return rounds[g.week];return "POSTSEASON "+std::to_string(g.week);}
 return "WEEK "+std::to_string(g.week);
}
// A feed snapshot (span>0) holds games from the last `span` days sorted newest
// first; `covered` lists the days already fetched so gaps can be backfilled.
// `team` marks a team page: that team's season results plus its next game.
struct Snapshot { int league=0; std::string date; int64_t updated=0; std::vector<Game> games; int span=0; std::vector<std::string> covered; std::string team; int64_t upcomingAt=0; }; // upcomingAt: when the schedule ahead was last fetched
// How far ahead each league's feed carries upcoming games (days): MLB, NFL, NBA, CFB.
static const int aheadDays[] = {2, 14, 2, 8};
struct Favorite { int league; std::string id,name; };
// On-demand matchup detail from the scoreboard: records, line scores, a few
// headline stats per sport, and ESPN's one-line story.
struct StatLine { std::string label, away, home; };
struct GameDetail { std::string id,awayRecord,homeRecord,awayHits,homeHits,awayErrors,homeErrors,headline; std::vector<int> awayLine,homeLine; std::vector<StatLine> stats; };
inline std::string clean(std::string s, size_t limit=60) {
 for(char &c:s) if((unsigned char)c<32 || (unsigned char)c>126)c='?';
 if(s.size()>limit)s=s.substr(0,limit-3)+"...";
 return s;
}
inline int64_t utcEpoch(int y,unsigned m,unsigned d,int h=0,int min=0,int sec=0) {
 y-=m<=2; const int era=(y>=0?y:y-399)/400; const unsigned yo=y-era*400;
 const unsigned doy=(153*(m+(m>2?-3:9))+2)/5+d-1;
 return ((int64_t)era*146097+yo*365+yo/4-yo/100+doy-719468)*86400+h*3600+min*60+sec;
}
inline int64_t isoEpoch(const std::string& s) {
 int y,m,d,h,n; if(sscanf(s.c_str(),"%d-%d-%dT%d:%d",&y,&m,&d,&h,&n)!=5)return 0;
 if(y<2020||y>2099||m<1||m>12||d<1||d>31||h<0||h>23||n<0||n>59)return 0;
 return utcEpoch(y,m,d,h,n);
}
inline std::string localDate(int64_t epoch) {time_t t=epoch;tm b{};localtime_r(&t,&b);char s[16];strftime(s,sizeof(s),"%Y%m%d",&b);return s;}
inline std::string stamp(int64_t epoch) {if(!epoch)return "NEVER";time_t t=epoch;tm b{};localtime_r(&t,&b);char s[48];strftime(s,sizeof(s),"%b %d %I:%M %p %Z",&b);return s;}
inline std::string shiftDate(const std::string& d,int offset) {
 int y,m,day;if(sscanf(d.c_str(),"%4d%2d%2d",&y,&m,&day)!=3)return d;
 tm b{};b.tm_year=y-1900;b.tm_mon=m-1;b.tm_mday=day+offset;b.tm_hour=12;b.tm_isdst=-1;
 return localDate(mktime(&b));
}
inline std::string prettyDate(const std::string& s){if(s.size()!=8)return "DATE UNKNOWN";return s.substr(4,2)+"/"+s.substr(6,2)+"/"+s.substr(0,4);}
inline bool validDate(const std::string& d){if(d.size()!=8)return false;for(char c:d)if(c<'0'||c>'9')return false;return shiftDate(d,0)==d;}
inline bool fresh(const Snapshot& s,int64_t now,bool online,bool failed) {return online&&!failed&&s.updated>0&&now>=s.updated&&now-s.updated<180;}
inline std::string score(const Game& g,const Team& t){return g.state=="pre"||t.score.empty()?"--":t.score;}
inline bool matches(const Game& g,const std::string& id){return id.empty()||g.home.id==id||g.away.id==id;}
inline std::string gameDate(const Game& g){return localDate(isoEpoch(g.start));}
// Game-aware refreshing. A league is "active" while a game is on or about to start
// (or should have started but the cache still says so); otherwise its scores cannot change.
inline bool leagueActive(const Snapshot& s,int64_t now){
 for(const auto& g:s.games){if(g.state=="in")return true;if(g.state=="pre"){const int64_t st=isoEpoch(g.start);if(st<=now+900&&st>=now-5*3600)return true;}}
 return false;
}
inline int64_t nextGameStart(const Snapshot& s,int64_t now){
 int64_t best=0;for(const auto& g:s.games)if(g.state=="pre"){const int64_t st=isoEpoch(g.start);if(st>now+900&&(!best||st<best))best=st;}
 return best;
}
// Seconds until the next refresh wake is worth it: 15 minutes while anything is
// active, otherwise at the next scheduled start, and at most every six hours.
inline int64_t refreshDelay(const Snapshot* feeds[4],int64_t now){
 bool active=false;int64_t next=0;
 for(int l=0;l<4;l++){if(!feeds[l])continue;if(leagueActive(*feeds[l],now))active=true;const int64_t n=nextGameStart(*feeds[l],now);if(n&&(!next||n<next))next=n;}
 if(active)return 900;if(next)return std::max<int64_t>(900,std::min<int64_t>(next-now,6*3600));return 6*3600;
}
// Which feeds a wake should fetch: active leagues, plus any cache older than
// twelve hours (or missing) so schedule changes still arrive.
inline int refreshMask(const Snapshot* feeds[4],int64_t now){
 int m=0;for(int l=0;l<4;l++)if(!feeds[l]||now-feeds[l]->updated>12*3600||leagueActive(*feeds[l],now))m|=1<<l;
 return m;
}
inline std::string dayLabel(int64_t epoch,int64_t now){
 if(!epoch)return "DATE UNKNOWN";if(localDate(epoch)==localDate(now))return "TODAY";
 time_t t=epoch;tm b{};localtime_r(&t,&b);char s[16];strftime(s,sizeof(s),"%a %m/%d",&b);for(char& c:s)c=toupper((unsigned char)c);
 std::string out=s;if(out.size()>4&&out[4]=='0')out.erase(4,1);return out; // "SUN 9/27"
}
// ESPN's short status in scoreboard form: "Final/OT", "10:23 3RD", "TOP 7TH".
inline std::string compactStatus(std::string s){
 for(char& c:s)c=toupper((unsigned char)c);
 for(size_t p;(p=s.find(" - "))!=std::string::npos;)s.replace(p,3," ");
 return s;
}
inline std::string clockLabel(int64_t epoch){
 if(!epoch)return "TBD";time_t t=epoch;tm b{};localtime_r(&t,&b);char s[16];strftime(s,sizeof(s),"%I:%M %p",&b);return s[0]=='0'?s+1:s;
}
// Compact leader cell for the matchup table (14 characters): surname plus the
// yards for football ("Cook III 154"), surname plus the first two items
// otherwise ("Judge 1-3 HR", "Young 29").
inline std::string leaderText(std::string name,const std::string& value,int league){
 std::vector<std::string> parts;size_t s=0;for(;;){size_t p=value.find(", ",s);parts.push_back(value.substr(s,p==std::string::npos?std::string::npos:p-s));if(p==std::string::npos)break;s=p+2;}
 {size_t p=name.rfind(". ");if(p!=std::string::npos&&p<4)name=name.substr(p+2);} // "J.K. Dobbins" -> "Dobbins"
 if(name.empty())name="?";const size_t max=13;
 if(league==1||league==3){for(auto& p:parts)if(p.find(" YDS")!=std::string::npos){name+=" "+p.substr(0,p.find(' '));break;}}
 else{int n=0;for(auto& p:parts){if(n++==2||p.empty()||name.size()+1+p.size()>max)break;name+=" "+p;}}
 if(name.size()>max){size_t sp=name.rfind(' ');std::string tail=sp==std::string::npos?"":name.substr(sp);std::string head=name.substr(0,sp==std::string::npos?name.size():sp);if(head.size()+tail.size()>max)head=head.substr(0,max-tail.size());name=head+tail;}
 return clean(name,max);
}
inline bool covers(const Snapshot& s,const std::string& d){return std::find(s.covered.begin(),s.covered.end(),d)!=s.covered.end();}
// Days a feed should fetch next: today always, yesterday while it still has
// games that are not over, then uncovered days newest first, a few per cycle.
inline std::vector<std::string> feedMissing(const Snapshot& s,const std::string& start,const std::string& end,size_t limit=4){
 std::vector<std::string> out{end};std::string y=shiftDate(end,-1);
 if(y>=start){bool open=false;for(const auto& g:s.games)if(gameDate(g)==y&&g.state!="post")open=true;if(open||!covers(s,y))out.push_back(y);}
 for(std::string d=shiftDate(end,-2);d>=start&&out.size()<limit;d=shiftDate(d,-1))if(!covers(s,d))out.push_back(d);
 return out;
}
inline bool feedHasGaps(const Snapshot& s,const std::string& start,const std::string& end){
 for(std::string d=start;d<end;d=shiftDate(d,1))if(!covers(s,d))return true;return false;
}
// A team page lists results newest first and keeps only the next scheduled game.
inline void trimUpcoming(Snapshot& s){
 std::stable_sort(s.games.begin(),s.games.end(),[](const Game& a,const Game& b){return isoEpoch(a.start)>isoEpoch(b.start);});
 int64_t next=0;for(const auto& g:s.games)if(g.state=="pre"){int64_t e=isoEpoch(g.start);if(!next||e<next)next=e;}
 s.games.erase(std::remove_if(s.games.begin(),s.games.end(),[&](const Game& g){return g.state=="pre"&&isoEpoch(g.start)!=next;}),s.games.end());
}
// Replace the feed's upcoming games (dated after `today`) with a fresh set, kept to the horizon.
inline void mergeUpcoming(Snapshot& feed,const Snapshot& part,const std::string& today,int ahead,int64_t now,size_t cap=150){
 const std::string horizon=shiftDate(today,ahead);
 feed.games.erase(std::remove_if(feed.games.begin(),feed.games.end(),[&](const Game& g){return gameDate(g)>today;}),feed.games.end());
 for(const auto& p:part.games){const std::string d=gameDate(p);bool dup=false;for(const auto& g:feed.games)if(g.id==p.id)dup=true;if(!dup&&d>today&&d<=horizon)feed.games.push_back(p);}
 std::stable_sort(feed.games.begin(),feed.games.end(),[](const Game& a,const Game& b){return isoEpoch(a.start)>isoEpoch(b.start);});
 if(feed.games.size()>cap)feed.games.resize(cap);
 feed.upcomingAt=now;
}
// The week a football league is in now: the lowest week with a game still to play, else the last played week plus one.
inline int currentWeek(const Snapshot& s){
 int open=0,played=0;for(const auto& g:s.games){if(g.seasonType!=2||g.week<=0)continue;if(g.state!="post"){if(!open||g.week<open)open=g.week;}else played=std::max(played,g.week);}
 return open?open:played?played+1:0;
}
// List order for a scoreboard: live games first, then what is next (soonest first), then results (newest first).
enum class Section { Live, Next, Results };
struct Ordered { Section section; int pos; }; // pos indexes the caller's id list
inline std::vector<Ordered> sectionedOrder(const Snapshot& s,const std::vector<int>& ids){
 std::vector<Ordered> live,next,results;
 for(size_t p=0;p<ids.size();p++){const Game& g=s.games[ids[p]];if(g.state=="in")live.push_back({Section::Live,(int)p});else if(g.state=="pre")next.push_back({Section::Next,(int)p});else results.push_back({Section::Results,(int)p});}
 auto by=[&](bool asc){return [&,asc](const Ordered& a,const Ordered& b){const int64_t x=isoEpoch(s.games[ids[a.pos]].start),y=isoEpoch(s.games[ids[b.pos]].start);return asc?x<y:x>y;};};
 std::stable_sort(live.begin(),live.end(),by(true));std::stable_sort(next.begin(),next.end(),by(true));std::stable_sort(results.begin(),results.end(),by(false));
 std::vector<Ordered> out=live;out.insert(out.end(),next.begin(),next.end());out.insert(out.end(),results.begin(),results.end());return out;
}
inline const char* sectionName(Section s){return s==Section::Live?"LIVE":s==Section::Next?"UP NEXT":"RESULTS";}
// Fold one day's response into the feed: replace that day, drop games that
// moved, keep the window, newest first, and record what has been covered.
inline void mergeFeed(Snapshot& feed,const Snapshot& part,const std::string& fetched,const std::string& start,const std::string& end,int span,size_t cap=150){
 feed.games.erase(std::remove_if(feed.games.begin(),feed.games.end(),[&](const Game& g){
  std::string d=gameDate(g);if(d==fetched||d<start||d>end)return true;
  for(const auto& p:part.games)if(p.id==g.id)return true;return false;}),feed.games.end());
 for(const auto& p:part.games){std::string d=gameDate(p);if(d>=start&&d<=end)feed.games.push_back(p);}
 std::stable_sort(feed.games.begin(),feed.games.end(),[](const Game& a,const Game& b){return isoEpoch(a.start)>isoEpoch(b.start);});
 if(feed.games.size()>cap)feed.games.resize(cap);
 std::vector<std::string> days{fetched};for(const auto& p:part.games)days.push_back(gameDate(p));for(const auto& c:feed.covered)days.push_back(c);
 feed.covered.clear();for(const auto& d:days)if(d>=start&&d<=end&&!covers(feed,d))feed.covered.push_back(d);
 feed.date=end;feed.span=span;if(part.updated)feed.updated=part.updated;
}
// Home page feed: the newest games already played (or in progress) across all
// leagues, drawn from the cached league feeds.
struct RecentGame { int league; Game game; bool next=false; }; // next: the league's soonest upcoming game, shown above its results
inline std::vector<RecentGame> recentGames(const Snapshot* feeds[4],size_t limit){
 std::vector<RecentGame> out;
 for(int l=0;l<4;l++){if(!feeds[l])continue;for(const auto& g:feeds[l]->games)if(g.state!="pre")out.push_back({l,g});}
 std::stable_sort(out.begin(),out.end(),[](const RecentGame& a,const RecentGame& b){return isoEpoch(a.game.start)>isoEpoch(b.game.start);});
 if(out.size()>limit)out.resize(limit);
 return out;
}
// Same games grouped by league (each group newest first, groups ordered by
// their newest game). The rows that fit `heightPx` are shared evenly between
// the leagues that have games, leftovers going to the most recent league, so
// one busy league cannot crowd the others off the page.
inline std::vector<RecentGame> recentGamesGrouped(const Snapshot* feeds[4],int heightPx,int rowH=56,int dividerH=28){
 std::vector<RecentGame> all=recentGames(feeds,200);std::vector<int> order;
 for(const auto& rg:all)if(std::find(order.begin(),order.end(),rg.league)==order.end())order.push_back(rg.league); // first appearance = newest game
 // Each league's soonest upcoming game leads its group as a NEXT row.
 for(int l=0;l<4;l++){if(!feeds[l])continue;const Game* soonest=nullptr;for(const auto& g:feeds[l]->games)if(g.state=="pre"&&(!soonest||isoEpoch(g.start)<isoEpoch(soonest->start)))soonest=&g;
  if(soonest){RecentGame n{l,*soonest,true};all.insert(all.begin(),n);if(std::find(order.begin(),order.end(),l)==order.end())order.push_back(l);}}
 if(order.empty())return {};
 std::vector<int> have(4,0),take(4,0);for(const auto& rg:all)have[rg.league]++;
 int rows=std::max(0,(heightPx-int(order.size())*dividerH)/rowH);
 for(int round=0;rows>0;round++){bool any=false;for(int l:order){if(take[l]<have[l]&&rows>0){take[l]++;rows--;any=true;}}if(!any)break;}
 std::vector<RecentGame> out;
 for(int l:order){int n=0;for(const auto& rg:all){if(rg.league!=l||n>=take[l])continue;out.push_back(rg);n++;}}
 return out;
}
// Standings: conferences (parent -1) and their divisions, each a ranked table.
// Pro conferences are assembled from their divisions by playoff seed; college
// conferences come ranked from the feed.
struct StandingRow { std::string id,abbr,name,record,pct,extra,streak; int seed=0; double winPct=0; };
struct StandingsGroup { std::string name,abbr; int parent=-1; std::vector<StandingRow> rows; };
struct Standings { int league=-1; int64_t updated=0; std::vector<StandingsGroup> groups; std::string scope; }; // scope: college conference id
inline std::string groupKey(std::string s){std::string o;for(unsigned char c:s)if(isalnum(c))o+=(char)toupper(c);return o;}
// ESPN's conference ids for the spoken names a child is likely to use.
inline std::string cfbConferenceId(const std::string& text){
 const std::string k=groupKey(text);if(k.empty())return "";
 static const char* table[][2]={{"SEC","8"},{"SOUTHEASTERN","8"},{"BIGTEN","5"},{"BIG10","5"},{"B1G","5"},{"ACC","1"},{"ATLANTICCOAST","1"},{"BIG12","4"},{"BIGTWELVE","4"},{"PAC12","9"},{"PACTWELVE","9"},{"AMERICAN","151"},{"AAC","151"},{"MOUNTAINWEST","17"},{"MWC","17"},{"MAC","15"},{"MIDAMERICAN","15"},{"CONFERENCEUSA","12"},{"CUSA","12"},{"SUNBELT","37"},{"INDEPENDENT","18"},{"INDEPENDENTS","18"}};
 for(auto& row:table)if(k.find(row[0])!=std::string::npos)return row[1];
 return "";
}
// The division and conference a team belongs to (either may be -1).
inline void teamGroups(const Standings& st,const std::string& teamId,int& division,int& conference){
 division=conference=-1;
 for(size_t g=0;g<st.groups.size();g++)for(const auto& r:st.groups[g].rows)if(r.id==teamId){
  if(st.groups[g].parent>=0){division=g;conference=st.groups[g].parent;return;} // a division wins over its conference
  if(conference<0)conference=g;}
}
// Best match for a spoken group name: exact key, then a group whose key contains it.
inline int findGroup(const Standings& st,const std::string& text){
 const std::string k=groupKey(text);if(k.empty())return -1;
 for(size_t g=0;g<st.groups.size();g++)if(groupKey(st.groups[g].name)==k||groupKey(st.groups[g].abbr)==k)return g;
 for(size_t g=0;g<st.groups.size();g++){std::string n=groupKey(st.groups[g].name);if(n.find(k)!=std::string::npos||k.find(n)!=std::string::npos)return g;}
 return -1;
}
// Debounce and recognize hold before release; never emit both click and hold.
enum class ButtonEvent { None,Click,Hold,ReleaseHold };
struct Button {
 bool raw=false,stable=false,held=false; uint32_t changed=0,pressed=0;
 ButtonEvent update(bool down,uint32_t now,bool enableHold=false) {
  if(down!=raw){raw=down;changed=now;}
  if(stable!=raw && uint32_t(now-changed)>=30){
   stable=raw;
   if(stable){pressed=now;held=false;}
   else {if(held)return ButtonEvent::ReleaseHold;return ButtonEvent::Click;}
  }
  if(enableHold&&stable&&!held&&uint32_t(now-pressed)>=700){held=true;return ButtonEvent::Hold;}
  return ButtonEvent::None;
 }
};
}
