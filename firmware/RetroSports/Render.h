#pragma once
#include <Adafruit_GFX.h>
#include "InterSmall.h"
#include "InterRead.h"
#include "InterRow.h"
#include "Bible.h"
#include "Weather.h"
#include "Core.h"
#include "Voice.h"
#include "TeamLogos.h"
namespace retro {
enum class Page {Home,Games,Detail,Date,Favorites,Settings,Wifi,Voice,Standings,Bible,BibleBooks,BibleChapters,Launcher,BibleHome,Weather,Update};
// The launcher is the first screen: the verse of the day (press: the Bible) over the latest scores (press: the sports scoreboard).
// Launcher selections: the weather strip, the Bible row, the five score tabs, and the settings gear.
constexpr int LAUNCH_WEATHER=0,LAUNCH_BIBLE=1,LAUNCH_TAB0=2,LAUNCH_GEAR=7,LAUNCH_COUNT=8;
// Sports Home selections: tabs 0-4 (ALL, four leagues), the gear, then the rows.
constexpr int HOME_TABS=5,HOME_GEAR=5,HOME_ALL_ROW=6,HOME_PREV=6,HOME_NEXT=7,HOME_ROW=8;
// Bible reader state: the loaded chapter laid out in pages, the page shown, a verse to highlight, and the picker cursors.
struct BibleView { int book=43,chapter=1,page=0,verse=0,pick=0,pickBook=43; std::vector<BiblePage> pages; };
// Reading text: Inter Medium at a 20 px em (capitals about the height of the size-2 pixel font) on 26 px lines.
constexpr int READ_LINE=26,READ_LINES=27;
inline int readWidth(const std::string& s){int w=0;for(unsigned char ch:s)if(ch>=32&&ch<=126)w+=InterReadGlyphs[ch-32].xAdvance;return w;}
enum class VoiceState {Idle,Listening,Thinking,Answer,Error};
// Where a team page came from, so BOOT returns there.
struct Origin { Page page=Page::Home; int league=0; std::string date,gameId; bool feed=false; };
struct UI {
 Page page=Page::Home,returnPage=Page::Home;int selected=0,league=0,gameIndex=0,dateOffset=0;Origin origin;
 std::string date="",filter,filterName,apName,apPass,notice,ip;
 bool online=false,fetching=false,failed=false,clockValid=false,storage=true,ap=false,feed=false;
 int64_t now=0,offlineSince=0;Snapshot snapshot;std::vector<Favorite> favorites;GameDetail detail;
 std::vector<RecentGame> recent;int tab=0,listPage=0,battery=-1;bool detailFromHome=false,dark=false,nightSleep=true;
 // Standings for the current league; the page shows `standingsGroup`, up/down flips to `standingsAlt` (-1 = none).
 Standings standings;int standingsGroup=-1,standingsAlt=-1;bool standingsLoading=false;std::string standingsWant; /* 0 = all sports, 1..4 = league+1 */ bool speak=true;VoiceState voice=VoiceState::Idle;std::string voiceHeard,voiceAnswer,voiceNote,voiceTeamId,voiceTeamName;int voiceLeague=-1;
 BibleView bible;BibleRef votd;std::string votdText;Weather weather;std::string updateNote,version;
};
// Launcher geometry shared by the renderer and the recent-games builder: the
// verse takes up to seven lines, the SPORTS bar follows, rows fill the rest.
inline std::vector<std::string> launcherVerseLines(const UI& u){return u.votd.valid()?wrapWidth(u.votdText,readWidth,456,7):std::vector<std::string>{};}
inline int launcherSportsBar(const UI& u){return 110+std::max(1,(int)launcherVerseLines(u).size())*READ_LINE;}
inline int launcherScoresTop(const UI& u){return launcherSportsBar(u)+48;}
inline std::vector<int> visibleGames(const UI& u){std::vector<int> a;for(size_t i=0;i<u.snapshot.games.size();i++)if(matches(u.snapshot.games[i],u.filter))a.push_back(i);return a;}
inline bool isFavorite(const UI& u,const Team& t){for(const auto& f:u.favorites)if(f.league==u.league&&f.id==t.id)return true;return false;}
// Scoreboard list layout shared by the renderer and the navigation: visible
// games, week dividers, and page boundaries packed by height.
struct GamesLayout {
 struct Item{int pos;std::string header;};
 int top=0,avail=0;bool team=false,feedLeague=false;std::vector<int> ids;std::vector<Item> items;std::vector<int> pages;
 static constexpr int rowH=56,hdrH=28;
 int pageOf(int pos)const{int item=0;for(size_t i=0;i<items.size();i++)if(items[i].pos==pos)item=i;int p=0;for(size_t i=0;i<pages.size();i++)if(pages[i]<=item)p=i;return p;}
 int firstGame(int page)const{if(pages.empty())return -1;int from=pages[std::min(page,(int)pages.size()-1)],to=page+1<(int)pages.size()?pages[page+1]:(int)items.size();for(int i=from;i<to;i++)if(items[i].pos>=0)return items[i].pos;return -1;}
};
inline GamesLayout layoutGames(const UI& u){
 GamesLayout L;int y=u.online?20:86;L.team=!u.filter.empty();L.feedLeague=u.feed&&!L.team;
 if(u.page==Page::Home){L.top=68;L.avail=768-L.top;} // league tab on Home: the list starts right under the tab row
 else if(u.page==Page::Launcher){L.top=launcherScoresTop(u);L.avail=780-L.top;} // launcher: under the verse and the tab strip
 else{L.top=L.team?y+76:L.feedLeague?y+50:y+78;L.avail=(L.team?620:768)-L.top;}
 L.ids=visibleGames(u);
 std::string last;for(size_t p=0;p<L.ids.size();p++){std::string h=weekLabel(u.league,u.snapshot.games[L.ids[p]]);if(!h.empty()&&h!=last){L.items.push_back({-1,h});last=h;}L.items.push_back({(int)p,""});}
 L.pages.push_back(0);int used=0;
 for(size_t i=0;i<L.items.size();i++){int ih=L.items[i].pos<0?L.hdrH:L.rowH,need=L.items[i].pos<0?L.hdrH+L.rowH:L.rowH;if(used+need>L.avail){L.pages.push_back(i);used=0;}used+=ih;}
 return L;
}
class Renderer {
 GFXcanvas1& c;
 void text(int x,int y,std::string s,int size=2,int color=0,int max=36){s=clean(s,max);c.setTextSize(size);c.setTextColor(color);c.setCursor(x,y);c.print(s.c_str());}
 // While a matchup's details load: the league's ball bouncing along a dotted arc.
 void loadingArt(int y,int league){
  const int x0=100,x1=330,top=y+8,base=y+70; // ball boxes are 50 px; the arc runs through their centers
  for(int x=x0+25;x<=x1+25;x+=6){double t=double(x-x0-25)/(x1-x0);int cy=base+24-int((base-top)*4*t*(1-t));c.drawPixel(x,cy,0);}
  const int bx[]={x0,(x0+x1)/2,x1},by[]={base,top,base};for(int i=0;i<3;i++)ball(bx[i],by[i],league,0);
  c.drawFastHLine(x0-4,base+50,x1-x0+58,0); // the ground
  center(base+64,"LOADING STATS...",2);
 }
 // The pixel font has no bold face: drawing twice one pixel apart stands in for it.
 void bold(int x,int y,const std::string& s,int size=2,int color=0,int max=36){text(x,y,s,size,color,max);text(x+1,y,s,size,color,max);}
 // Secondary text in Inter Medium at 12 px (proportional, ~9 px caps): easier
 // to read than a pixel font at this size and still lighter than a size-2 label.
 int smallWidth(const std::string& s){int w=0;for(unsigned char ch:s)if(ch>=32&&ch<=126)w+=InterSmallGlyphs[ch-32].xAdvance;return w;}
 std::string fitSmall(std::string s,int maxWidth){while(!s.empty()&&smallWidth(s)>maxWidth)s.pop_back();return s;}
 void small(int x,int y,std::string s,int color=0,int maxWidth=432){
  s=fitSmall(clean(s,120),maxWidth);c.setFont(&InterSmall);c.setTextSize(1);c.setTextColor(color);c.setCursor(x,y+InterSmallAscent);c.print(s.c_str());c.setFont(nullptr);
 }
 // Body text for reading (see READ_LINE).
 void read(int x,int y,std::string s,int color=0,int maxWidth=456){
  s=clean(s,400);while(!s.empty()&&readWidth(s)>maxWidth)s.pop_back();
  c.setFont(&InterRead);c.setTextSize(1);c.setTextColor(color);c.setCursor(x,y+InterReadAscent);c.print(s.c_str());c.setFont(nullptr);
 }
 // Weather glyphs, 28 px times `s` (see weatherIcon for the numbering); `ink` is the drawing color.
 void weatherGlyph(int x,int y,int kind,int s=1,int ink=0){
  const int cx=x+14*s,cy=y+14*s,paper=ink?0:1;
  auto cloud=[&](int dx,int dy){c.fillCircle(cx+(dx-5)*s,cy+(dy+3)*s,5*s,ink);c.fillCircle(cx+(dx+1)*s,cy+(dy-1)*s,6*s,ink);c.fillCircle(cx+(dx+7)*s,cy+(dy+4)*s,4*s,ink);c.fillRect(cx+(dx-5)*s,cy+(dy+3)*s,12*s,6*s,ink);};
  auto sun=[&](int sx,int sy,int r){c.fillCircle(sx,sy,r*s,ink);for(int i=0;i<8;i++){double a=i*3.14159/4;for(int t=0;t<s;t++)c.drawLine(sx+int((r+3)*s*cos(a))+t,sy+int((r+3)*s*sin(a)),sx+int((r+6)*s*cos(a))+t,sy+int((r+6)*s*sin(a)),ink);}};
  switch(kind){
   case 0:sun(cx,cy,6);break;
   case 1:c.fillCircle(cx,cy,9*s,ink);c.fillCircle(cx+5*s,cy-4*s,8*s,paper);break;
   case 2:sun(cx-4*s,cy-4*s,4);cloud(3,3);break;
   case 3:cloud(0,0);break;
   case 4:cloud(0,-4);for(int i=0;i<3;i++)for(int t=0;t<s;t++)c.drawLine(cx+(-6+i*6)*s+t,cy+8*s,cx+(-8+i*6)*s+t,cy+13*s,ink);break;
   case 5:cloud(0,-4);for(int i=0;i<3;i++)c.fillRect(cx+(-7+i*6)*s,cy+10*s,2*s,2*s,ink);break;
   case 6:cloud(0,-4);c.fillTriangle(cx+1*s,cy+5*s,cx-4*s,cy+12*s,cx+1*s,cy+11*s,ink);c.fillTriangle(cx+1*s,cy+9*s,cx+5*s,cy+7*s,cx-1*s,cy+15*s,ink);break;
   default:for(int i=0;i<3;i++)c.fillRect(cx+(-10+(i==1?3:0))*s,cy+(-5+i*5)*s,(i==1?14:20)*s,s,ink);break;
  }
 }
 // The score tabs and the settings gear; `cursor` is 0-4 for a tab, 5 for the gear, -1 for none.
 void tabStrip(int y,int activeTab,int cursor){
  static const char* tabs[]={"ALL","MLB","NFL","NBA","CFB"};
  for(int i=0;i<HOME_TABS;i++){int x=12+i*79;bool active=activeTab==i,cur=cursor==i;
   c.fillRect(x,y,79,40,active?0:1);c.drawRect(x,y,79,40,0);if(cur&&!active)c.drawRect(x+2,y+2,75,36,0);
   if(cur&&active)c.drawRect(x+3,y+3,73,34,1);
   int w=int(strlen(tabs[i]))*12;text(x+(79-w)/2,y+12,tabs[i],2,active?1:0);}
  const bool sel=cursor==HOME_GEAR;const int x=407,cx=437,cy=y+20,color=sel?1:0;c.fillRect(x,y,61,40,sel?0:1);c.drawRect(x,y,61,40,0);
  for(int t=0;t<8;t++){double a=t*3.14159/4;int tx=cx+int(11*cos(a)),ty=cy+int(11*sin(a));c.fillRect(tx-2,ty-2,5,5,color);}
  c.fillCircle(cx,cy,9,color);c.fillCircle(cx,cy,4,sel?0:1);
 }
 // One page of a league scoreboard (week dividers and rows) with no cursor, for the launcher.
 void leaguePage(const UI& u){
  const GamesLayout L=layoutGames(u);
  if(L.ids.empty()){center(L.top+90,u.snapshot.updated?"NO GAMES SAVED":"NO SAVED SCORES",3);center(L.top+150,u.online?(u.fetching?"FETCHING THE LATEST...":"NOTHING IN THIS WINDOW"):"CONNECT WI-FI TO LOAD SCORES",2);return;}
  const int last=L.pages.size()>1?L.pages[1]:(int)L.items.size();int ry=L.top;
  for(int i=0;i<last;i++){const auto& it=L.items[i];
   if(it.pos<0){std::string s=" "+it.header+" ";int w=int(s.size())*12;c.fillRect(16,ry+13,448,3,0);c.fillRect(240-w/2,ry+6,w+1,16,1);text(240-w/2,ry+6,s,2);text(240-w/2+1,ry+6,s,2);ry+=L.hdrH;continue;}
   const bool beforeDivider=i+1<last&&L.items[i+1].pos<0;
   gameRow(ry,u.league,u.snapshot.games[L.ids[it.pos]],false,state(u,u.snapshot.games[L.ids[it.pos]]),!beforeDivider);ry+=L.rowH;}
 }
 // Recent games grouped by league with dividers; selBase < 0 draws them without a cursor.
 void recentList(const UI& u,int ry,int selBase){
  int lastLeague=-1;
  for(size_t i=0;i<u.recent.size();i++){const auto& rg=u.recent[i];const int64_t start=isoEpoch(rg.game.start);
   if(rg.league!=lastLeague){std::string s=std::string(" ")+leagues[rg.league].name+" ";int w=int(s.size())*12;c.fillRect(16,ry+13,448,3,0);c.fillRect(240-w/2,ry+6,w+1,16,1);text(240-w/2,ry+6,s,2);text(240-w/2+1,ry+6,s,2);ry+=28;lastLeague=rg.league;}
   if(ry+56>770)break;
   std::string status=dayLabel(start,u.now)+" "+(rg.game.state=="in"&&!u.online?"SAVED ":"")+compactStatus(rg.game.status);
   const bool beforeDivider=i+1<u.recent.size()&&u.recent[i+1].league!=rg.league;
   gameRow(ry,rg.league,rg.game,selBase>=0&&u.selected==selBase+(int)i,status,!beforeDivider);ry+=56;}
  if(u.recent.empty()){center(ry+70,"NO RECENT GAMES YET",3);center(ry+130,u.online?"LOADING SCORES...":"CONNECT WI-FI TO LOAD SCORES",2);}
 }
 // The day/status line under a matchup: Inter SemiBold at 13 px, a step heavier than the captions.
 int rowWidth(const std::string& s){int w=0;for(unsigned char ch:s)if(ch>=32&&ch<=126)w+=InterRowGlyphs[ch-32].xAdvance;return w;}
 void rowStatus(int cx,int y,std::string s,int color,int maxWidth){
  s=clean(s,120);while(!s.empty()&&rowWidth(s)>maxWidth)s.pop_back();
  c.setFont(&InterRow);c.setTextSize(1);c.setTextColor(color);c.setCursor(cx-rowWidth(s)/2,y+InterRowAscent);c.print(s.c_str());c.setFont(nullptr);
 }
 void smallCenter(int cx,int y,const std::string& s,int color=0,int maxWidth=432){std::string t=fitSmall(clean(s,120),maxWidth);small(cx-smallWidth(t)/2,y,t,color,maxWidth);}
 void center(int y,std::string s,int size=2){s=clean(s,440/(6*size));text((480-int(s.size())*6*size)/2,y,s,size);}
 void row(int y,std::string label,bool selected,int size=2){
  c.fillRect(12,y,456,54,selected?0:1);c.drawRect(12,y,456,54,0);
  if(selected)text(22,y+18,">",2,1);text(46,y+18,label,size,selected?1:0,32);
 }
 void ball(int x,int y,int kind,int color){
  if(kind==1||kind==3){c.drawRoundRect(x,y+8,50,30,13,color);c.drawFastHLine(x+13,y+23,24,color);for(int i=0;i<4;i++)c.drawFastVLine(x+18+i*5,y+18,10,color);}
  else {c.drawCircle(x+25,y+24,23,color);c.drawCircle(x+25,y+24,22,color);if(kind==0){for(int i=0;i<5;i++){c.drawFastHLine(x+10,y+8+i*8,7,color);c.drawFastHLine(x+34,y+8+i*8,7,color);}}else {c.drawFastVLine(x+25,y+2,45,color);c.drawFastHLine(x+3,y+24,45,color);c.drawRect(x+13,y+8,24,32,color);}}
 }
 void logo(int x,int y,int size,int league,const std::string& id,const std::string& abbr){
  // White logo badges stay recognizable on both normal and selected cards.
  c.fillRect(x,y,size,size,1);
  const uint8_t* bits=findLogo(league,id);
  if(bits){for(int yy=0;yy<size;yy++)for(int xx=0;xx<size;xx++){
   int sx=xx*96/size,sy=yy*96/size;if(bits[sy*12+sx/8]&(0x80>>(sx%8)))c.drawPixel(x+xx,y+yy,0);
  }}else{c.drawRect(x+2,y+2,size-4,size-4,0);auto label=clean(abbr.empty()?"?":abbr,4);int scale=size>=96?3:1;text(x+(size-label.size()*6*scale)/2,y+size/2-4*scale,label,scale);}
 }
 // The only banner: shown while Wi-Fi is down, with when it dropped and how old the scores are.
 void offline(const UI& u,int y){
  c.fillRect(8,y,464,56,0);std::string since="OFFLINE - SAVED SCORES";
  if(u.offlineSince){std::string day=dayLabel(u.offlineSince,u.now);since="OFFLINE SINCE "+(day=="TODAY"?"":day+" ")+clockLabel(u.offlineSince);}
  text(20,y+9,since,2,1);text(20,y+36,"SCORES FROM "+stamp(u.snapshot.updated),1,1,48);
 }
 // Fits 17 characters at size 2: day (in the feed) plus a compact status.
 std::string state(const UI& u,const Game& g){
  const int64_t start=isoEpoch(g.start);const bool multiDay=u.snapshot.span>0||!u.snapshot.team.empty();
  const std::string day=multiDay?dayLabel(start,u.now)+" ":"";
  if(g.state=="pre")return day+clockLabel(start);
  if(g.state=="in"&&!fresh(u.snapshot,u.now,u.online,u.failed))return "SAVED "+compactStatus(g.status);
  return day+compactStatus(g.status);
 }
 // One scoreboard row: away score / logo / matchup + status / logo / home score.
 void gameRow(int ry,int league,const Game& g,bool sel,const std::string& status,bool underline=true){
  const int rowH=56,color=sel?1:0; // 56 px: a few pixels of air above and below the matchup line
  if(sel)c.fillRect(12,ry,456,rowH,0);else if(underline)c.drawFastHLine(16,ry+rowH-1,448,0);
  auto a=score(g,g.away),h=score(g,g.home);
  text(84-int(a.size())*18,ry+16,a,3,color,4);logo(88,ry+7,42,league,g.away.id,g.away.abbr);
  logo(350,ry+7,42,league,g.home.id,g.home.abbr);text(396,ry+16,h,3,color,4);
  std::string vs=clean(g.away.abbr,4)+" @ "+clean(g.home.abbr,4);text(240-int(vs.size())*6,ry+10,vs,2,color,13);
  rowStatus(240,ry+32,status,color,216);
 }
 static std::string upperAbbrLabel(std::string s){for(auto& c:s)c=toupper((unsigned char)c);return s;}
 // Standings table: rank, logo, team, record, pct or games behind, streak.
 void standingsPage(const UI& u){
  int y=20;if(!u.online){offline(u,y);y+=66;}
  if(u.standingsGroup<0||u.standingsGroup>=(int)u.standings.groups.size()){
   center(y+180,"STANDINGS",3);center(y+250,u.standingsLoading?"LOADING...":u.online?"NOT AVAILABLE YET":"CONNECT WI-FI TO LOAD",2);if(!u.standingsWant.empty())center(y+300,upperAbbrLabel(u.standingsWant),2);return;}
  const auto& g=u.standings.groups[u.standingsGroup];
  // Conferences go by abbreviation (NFC, AL), divisions by name; the hint names the other view.
  auto label=[&](const StandingsGroup& x){return upperAbbrLabel(x.parent<0&&x.abbr.size()>=2?x.abbr:x.name);};
  c.fillRect(12,y,456,40,0);std::string title=clean(label(g),18);text(24,y+12,title,2,1,18);
  if(u.standingsAlt>=0){std::string hint=clean("UP/DOWN: "+label(u.standings.groups[u.standingsAlt]),(440-int(title.size())*12-24)/12);text(460-int(hint.size())*12,y+12,hint,2,1,36);}
  const bool college=u.league==3,nfl=u.league==1;
  small(20,y+48,"#");small(88,y+48,"TEAM");small(224,y+48,college?"OVERALL":"W-L");small(304,y+48,college?"CONF":nfl?"PCT":"GB");small(388,y+48,"STRK");
  c.drawFastHLine(16,y+62,448,0);
  int ry=y+66;const int rowH=36;
  for(size_t i=0;i<g.rows.size()&&ry+rowH<=790;i++){const auto& r=g.rows[i];const bool mine=r.id==u.filter;
   if(mine)c.fillRect(12,ry,456,rowH,0);const int color=mine?1:0;
   text(20,ry+10,std::to_string(i+1),2,color,2);logo(48,ry+3,30,u.league,r.id,r.abbr);text(88,ry+10,clean(r.abbr,6),2,color,6);
   text(224,ry+10,r.record,2,color,7);text(304,ry+10,college?r.extra:nfl?r.pct:r.extra,2,color,6);text(388,ry+10,r.streak,2,color,4);
   if(!mine)c.drawFastHLine(16,ry+rowH-1,448,0);ry+=rowH;}
  small(20,776,stamp(u.standings.updated).insert(0,"AS OF "));
 }
 // Matchup page status always carries the day.
 std::string fullState(const UI& u,const Game& g){
  const int64_t start=isoEpoch(g.start);const std::string day=dayLabel(start,u.now)+" ";
  if(g.state=="pre")return day+clockLabel(start);
  if(g.state=="in"&&!fresh(u.snapshot,u.now,u.online,u.failed))return "SAVED "+compactStatus(g.status);
  return day+compactStatus(g.status);
 }
 // Line score, sport-specific stat lines, and the headline at the 12x16 size.
 void statBlock(const UI& u,const Game& g,int& y,int limit){
  const GameDetail& d=u.detail;const size_t n=std::min(std::max(d.awayLine.size(),d.homeLine.size()),size_t(12));const bool baseball=u.league==0;const int x0=28,lh=24;
  auto rule=[&](){y+=10;c.drawFastHLine(16,y,448,0);y+=16;}; // breathing room on both sides of each rule
  rule();
  if(n&&y+3*lh<=limit){
   // Centered by its real width: label column, one column per period, then the totals.
   const int cw=n>6?24:36,width=64+int(n)*cw+12+(baseball?96:36),lx=std::max(20,240-width/2),x1=lx+64,xt=x1+int(n)*cw+12;
   for(size_t i=0;i<n;i++)text(x1+int(i)*cw,y,std::to_string(i+1),2);
   text(xt,y,baseball?"R":"T",2);if(baseball){text(xt+36,y,"H",2);text(xt+72,y,"E",2);}y+=lh;
   const Team* ts[]={&g.away,&g.home};const std::vector<int>* ls[]={&d.awayLine,&d.homeLine};const std::string* hs[]={&d.awayHits,&d.homeHits};const std::string* es[]={&d.awayErrors,&d.homeErrors};
   for(int t=0;t<2;t++){bold(lx,y,clean(ts[t]->abbr,4),2);for(size_t i=0;i<ls[t]->size()&&i<n;i++)text(x1+int(i)*cw,y,std::to_string((*ls[t])[i]),2);
    text(xt,y,score(g,*ts[t]),2);if(baseball){text(xt+36,y,*hs[t],2,0,2);text(xt+72,y,*es[t],2,0,2);}y+=lh;}
   rule();
  }
  if(!d.stats.empty()&&y+2*lh<=limit){ // label | away | home, abbreviations as the header row
   bold(116,y,clean(g.away.abbr,4),2);bold(300,y,clean(g.home.abbr,4),2);y+=lh;
   for(const auto& s:d.stats){if(y+lh>limit)return;bold(x0,y,s.label,2,0,7);text(116,y,s.away.empty()?"-":s.away,2,0,13);text(300,y,s.home.empty()?"-":s.home,2,0,13);y+=lh;}
  }
  if(!d.headline.empty()&&y+2*lh+14<=limit){rule();std::string h=d.headline;
   for(int l=0;l<3&&!h.empty()&&y+lh<=limit;l++){size_t cut=h.size()<=36?h.size():h.rfind(' ',36);if(cut==std::string::npos||cut==0)cut=36;std::string line=h.substr(0,cut);h=h.size()>cut?h.substr(cut+(h[cut]==' '?1:0)):"";
    if(!h.empty()&&(l==2||y+2*lh>limit))line=clean(line+" "+h,36),h.clear();text(x0,y,line,2,0,36);y+=lh;}}
 }
 public:
 explicit Renderer(GFXcanvas1& canvas):c(canvas){c.setRotation(3);}
 // What the panel shows while asleep: the verse of the day, large and centered
 // under the top banner the caller adds (falls back to the current page).
 void sleepVerse(const UI& u){
  if(!u.votd.valid()){render(u);return;}
  c.fillScreen(1);c.setTextWrap(false);
  const auto lines=wrapLines(u.votdText,36,16);const int total=76+int(lines.size())*24;int y=52+(748-total)/2;
  center(y,"VERSE OF THE DAY",2);center(y+30,bibleRefLabel(u.votd),3);y+=76;
  for(const auto& line:lines){center(y,line,2);y+=24;}
 }
 void render(const UI& u){
  c.fillScreen(1);c.setTextWrap(false); // no outer frame: the panel edge is the border
  if(u.page==Page::Home){
   // Tabs: all sports or one league. Moving onto a tab switches the list; pressing a league tab opens its full scoreboard.
   const bool inLeagueList=u.tab>0&&u.selected>=HOME_PREV; // the paging bar replaces the tab row while browsing a league
   if(!inLeagueList)tabStrip(20,u.tab,u.selected<=HOME_GEAR?u.selected:-1);
   if(u.tab==0)recentList(u,68,HOME_ALL_ROW);
   else{ // a league tab: the full scoreboard with PREV / NEXT buttons, then the rows
    const GamesLayout L=layoutGames(u);const int pages=(int)L.pages.size();
    const int page=u.selected>HOME_NEXT?L.pageOf(u.selected-HOME_ROW):std::min(u.listPage,pages-1);
    std::string label=std::string(leagues[u.league].name)+" "+std::to_string(page+1)+"/"+std::to_string(pages);
    if(inLeagueList){ // the paging buttons take the tab row once you press into the tab
     c.drawRect(12,20,456,40,0);const bool prev=u.selected==HOME_PREV,next=u.selected==HOME_NEXT;
     c.fillRect(12,20,120,40,prev?0:1);c.drawRect(12,20,120,40,0);text(36,32,"< PREV",2,prev?1:0);
     c.fillRect(348,20,120,40,next?0:1);c.drawRect(348,20,120,40,0);text(372,32,"NEXT >",2,next?1:0);
     text(240-int(label.size())*6,32,label,2,0,17);
    }
    if(L.ids.empty()){center(300,u.snapshot.updated?"NO GAMES SAVED":"NO SAVED SCORES",3);center(360,u.online?(u.fetching?"FETCHING THE LATEST...":"NOTHING IN THIS WINDOW"):"CONNECT WI-FI TO LOAD SCORES",2);}
    else{
     int first=L.pages[page],last=page+1<pages?L.pages[page+1]:(int)L.items.size(),ry=L.top,gFirst=-1,gLast=-1;
     for(int i=first;i<last;i++){const auto& it=L.items[i];
      if(it.pos<0){std::string s=" "+it.header+" ";int w=int(s.size())*12;c.fillRect(16,ry+13,448,3,0);c.fillRect(240-w/2,ry+6,w+1,16,1);text(240-w/2,ry+6,s,2);text(240-w/2+1,ry+6,s,2);ry+=L.hdrH;continue;}
      if(gFirst<0)gFirst=it.pos;gLast=it.pos;const bool beforeDivider=i+1<last&&L.items[i+1].pos<0;
      gameRow(ry,u.league,u.snapshot.games[L.ids[it.pos]],u.selected==it.pos+HOME_ROW,state(u,u.snapshot.games[L.ids[it.pos]]),!beforeDivider);ry+=L.rowH;}
     if(gFirst>=0)text(16,776,"GAMES "+std::to_string(gFirst+1)+"-"+std::to_string(gLast+1)+" OF "+std::to_string(L.ids.size())+"   PAGE "+std::to_string(page+1)+"/"+std::to_string(pages),1);
    }
   }
   if(!u.online)small(380,776,"WI-FI OFF");
  }else if(u.page==Page::Games){
   int y=20;if(!u.online){offline(u,y);y+=66;}
   const GamesLayout L=layoutGames(u);const int pages=(int)L.pages.size();
   const int page=u.selected>0?L.pageOf(u.selected-1):std::min(u.listPage,pages-1);
   if(L.team){ // Team page: small logo, name, record and place; standings rows sit at the bottom.
    logo(12,y,60,u.league,u.filter,u.filterName.substr(0,3));text(84,y+8,u.filterName,2,0,31);
    int d=-1,c=-1;if(u.standings.league==u.league)teamGroups(u.standings,u.filter,d,c);const int n=(int)L.ids.size();
    std::string sub=leagues[u.league].name;
    if(d>=0||c>=0){const auto& grp=u.standings.groups[d>=0?d:c];for(size_t i=0;i<grp.rows.size();i++)if(grp.rows[i].id==u.filter){
      const int p=i+1;const char* suf=(p%100>=11&&p%100<=13)?"TH":p%10==1?"ST":p%10==2?"ND":p%10==3?"RD":"TH";
      sub=grp.rows[i].record+"  -  "+std::to_string(p)+suf+" IN "+upperAbbrLabel(grp.parent<0&&grp.abbr.size()>=2?grp.abbr:grp.name);break;}}
    small(84,y+36,sub,0,372);
    if(d>=0||c>=0){int ry=632;if(d>=0){row(ry,upperAbbrLabel(u.standings.groups[d].name)+" STANDINGS",u.selected==n+1);ry+=58;}
     if(c>=0)row(ry,upperAbbrLabel(u.standings.groups[c].abbr.size()>=2?u.standings.groups[c].abbr:u.standings.groups[c].name)+" STANDINGS",u.selected==n+(d>=0?2:1));}
    else row(690,u.standingsLoading?"STANDINGS: LOADING...":"STANDINGS",u.selected==n+1);
   }else if(L.feedLeague){ // League feed: a page bar instead of a banner (selection 0 cycles pages).
    const bool sel=u.selected==0;c.fillRect(12,y,456,40,sel?0:1);c.drawRect(12,y,456,40,0);
    text(24,y+12,std::string(leagues[u.league].name)+"  PAGE "+std::to_string(page+1)+"/"+std::to_string(pages),2,sel?1:0,20);
    std::string hint=pages>1?"PRESS: NEXT PAGE":"";text(460-int(hint.size())*12,y+12,hint,2,sel?1:0,16);
   }else{
    row(y,std::string(leagues[u.league].name)+" / "+prettyDate(u.date),u.selected==0);
    text(20,y+63,"SELECT DATE ABOVE TO CHANGE DAY",1);
   }
   if(L.ids.empty()){
    center(y+170,u.snapshot.updated?(u.feed?"NO RECENT GAMES":"NO GAMES TODAY"):"NO SAVED SCORES",3);
    center(y+229,u.filter.empty()?"UP/DOWN: PICK ANOTHER DAY":"NO GAMES FOR THIS TEAM YET",2);
    center(y+281,u.online?(u.fetching?"FETCHING THE LATEST...":"SELECT DATE OR CHECK WI-FI"):"CONNECT WI-FI TO LOAD SCORES",2);
   }else{
    // Dense scoreboard: one row per game; football leagues get a divider wherever the week changes.
    int first=L.pages[page],last=page+1<pages?L.pages[page+1]:(int)L.items.size(),ry=L.top,gFirst=-1,gLast=-1;
    for(int i=first;i<last;i++){
     const auto& it=L.items[i];
     if(it.pos<0){std::string s=" "+it.header+" ";int w=int(s.size())*12;c.fillRect(16,ry+13,448,3,0);c.fillRect(240-w/2,ry+6,w+1,16,1);text(240-w/2,ry+6,s,2);text(240-w/2+1,ry+6,s,2);ry+=L.hdrH;continue;}
     if(gFirst<0)gFirst=it.pos;gLast=it.pos;
     const bool beforeDivider=i+1<last&&L.items[i+1].pos<0; // the week divider separates on its own
     gameRow(ry,u.league,u.snapshot.games[L.ids[it.pos]],u.selected==it.pos+1,state(u,u.snapshot.games[L.ids[it.pos]]),!beforeDivider);
     ry+=L.rowH;
    }
    if(gFirst>=0)text(16,776,"GAMES "+std::to_string(gFirst+1)+"-"+std::to_string(gLast+1)+" OF "+std::to_string(L.ids.size())+"   PAGE "+std::to_string(page+1)+"/"+std::to_string(pages),1);
   }
  }else if(u.page==Page::Detail){
   int y=20;if(!u.online){offline(u,y);y+=66;}
   if(u.gameIndex<(int)u.snapshot.games.size()){
    const auto& g=u.snapshot.games[u.gameIndex];const bool has=u.detail.id==g.id;
    // Logos near the edges leave a 144 px column between them for the day, "@", and venue.
    logo(16,y+8,144,u.league,g.away.id,g.away.abbr);logo(320,y+8,144,u.league,g.home.id,g.home.abbr);
    text(228,y+62,"@",5); // the middle column holds only the "@": size 5 is 25 px wide, so x=228 centers it on 240
    // Up/down highlights a team's logo; select opens that team's page.
    if(u.selected==1||u.selected==2){int fx=u.selected==1?16:320;for(int b=6;b<=9;b++)c.drawRect(fx-b,y+8-b,144+2*b,144+2*b,0);}
    if(has){const std::string ar=clean(u.detail.awayRecord,8),hr=clean(u.detail.homeRecord,8);text(88-int(ar.size())*6,y+170,ar,2);text(392-int(hr.size())*6,y+170,hr,2);}
    std::string aa=clean(g.away.abbr,6),hh=clean(g.home.abbr,6);text(88-int(aa.size())*9,y+198,aa,3);text(392-int(hh.size())*9,y+198,hh,3);
    auto a=score(g,g.away),h=score(g,g.home);text(88-int(a.size())*21,y+238,a,7);text(392-int(h.size())*21,y+238,h,7);
    // The status sits between the scores, centered on that gap.
    {const int left=88+int(a.size())*21+8,right=392-int(h.size())*21-8,mid=(left+right)/2,width=std::max(60,right-left);
     const int64_t start=isoEpoch(g.start);const bool saved=g.state=="in"&&!fresh(u.snapshot,u.now,u.online,u.failed);
     // Stacked on the scores' centerline: the date, the status or kickoff time, then the venue.
     std::vector<std::pair<std::string,bool>> lines{{clean(dayLabel(start,u.now),width/12),true},{clean(g.state=="pre"?clockLabel(start):compactStatus(g.status),width/12),true}};
     if(saved)lines.push_back({"SAVED SCORE",false});
     {std::string rest=g.venue;for(int l=0;l<2&&!rest.empty();l++){std::string line,word;size_t p=0;
       while(p<=rest.size()){size_t q=rest.find(' ',p);word=rest.substr(p,q==std::string::npos?std::string::npos:q-p);std::string trial=line.empty()?word:line+" "+word;if(!line.empty()&&rowWidth(trial)>width)break;line=trial;p=q==std::string::npos?rest.size()+1:q+1;}
       rest=p>rest.size()?"":rest.substr(p);if(l==1&&!rest.empty())line=line+" "+rest;while(!line.empty()&&rowWidth(line)>width)line.pop_back();lines.push_back({line,false});}}
     int height=0;for(auto& ln:lines)height+=ln.second?26:18;int ty=y+266-height/2; // roomier lines; the venue in the bold row face
     for(auto& ln:lines){if(ln.second){text(mid-int(ln.first.size())*6,ty,ln.first,2);ty+=26;}else{rowStatus(mid,ty,ln.first,0,width);ty+=18;}}}
    int sy=y+308;if(has)statBlock(u,g,sy,776);else if(u.online)loadingArt(sy+40,u.league);
   }
  }else if(u.page==Page::Date){
   center(138,"PICK A GAME DAY",3);center(302,prettyDate(shiftDate(u.date,u.dateOffset)),4);
   center(405,"UP / DOWN: CHANGE DAY",2);center(451,"PRESS: SHOW GAMES",2);center(493,"BOOT: CANCEL",2);center(580,"UP TO 30 DAYS EITHER WAY",2);
  }else if(u.page==Page::Favorites){
   center(124,"YOUR ALL-STAR ROSTER",3);
   if(u.favorites.empty()){center(300,"NO FAVORITES YET",3);center(380,"OPEN A GAME AND",2);center(414,"FAVORITE A TEAM",2);center(480,"YOUR TEAMS WILL APPEAR HERE",2);}
   int start=(u.selected/6)*6;
   for(int i=0;i<6&&start+i<(int)u.favorites.size();i++){
    auto& f=u.favorites[start+i];int y=190+i*88;bool sel=u.selected==start+i;c.fillRect(12,y,456,80,sel?0:1);c.drawRect(12,y,456,80,0);
    logo(30,y+8,64,f.league,f.id,"");text(110,y+15,leagues[f.league].name,1,sel?1:0);text(110,y+40,f.name,2,sel?1:0,28);
   }
  }else if(u.page==Page::Settings){
   center(128,"LOCKER ROOM",3);row(190,"CONNECT / CHANGE WI-FI",u.selected==0);row(250,"REFRESH SAVED SCORES",u.selected==1);row(310,"SLEEP DISPLAY",u.selected==2);
   row(370,std::string("SPOKEN REPLIES: ")+(u.speak?"ON":"OFF"),u.selected==3);row(430,std::string("DARK MODE: ")+(u.dark?"ON":"OFF"),u.selected==4);
   row(490,std::string("SLEEP 11PM-6:30AM: ")+(u.nightSleep?"ON":"OFF"),u.selected==5);row(550,"CHECK FOR UPDATES  (v"+u.version+")",u.selected==6);
   center(632,"DATA: ESPN / COLLEGE: FBS",2);center(664,"HOLD ROCKER TO ASK A QUESTION",2);
   center(696,(u.battery>=0?"BATTERY "+std::to_string(u.battery)+"%  -  ":std::string())+(u.storage?"SCORES SAVED":"STORAGE ERROR"),2);center(724,u.clockValid?stamp(u.now):"CONNECT WI-FI TO SET CLOCK",2);
  }else if(u.page==Page::Update){ // over-the-air update: a status line, then what to do
   center(150,"UPDATE",4);{int y=300;for(const auto& line:wrapLines(u.updateNote,30,5)){center(y,line,2);y+=36;}}
   center(560,"INSTALLED: v"+u.version,2);
  }else if(u.page==Page::Wifi){
   center(132,"WI-FI SETUP",3);
   if(u.online){center(285,"CONNECTED!",4);center(377,u.clockValid?"READY TO LOAD SCORES":"SETTING THE CLOCK...",2);center(457,"PRESS BOOT TWICE",2);center(491,"TO CHOOSE A LEAGUE",2);}
   else if(u.ap){text(20,218,"1. JOIN WI-FI ON YOUR PHONE",2);text(20,259,u.apName,3);text(20,315,"PASSWORD: "+u.apPass,2);text(20,393,"2. OPEN IN YOUR BROWSER",2);text(20,436,"http://192.168.4.1",3);text(20,520,"3. ENTER HOME WI-FI",2);text(64,554,"AND YOUR TIMEZONE",2);}
   else {center(290,"SETUP CLOSED",4);center(398,"PRESS TO START WI-FI SETUP",2);}
   center(654,u.notice,1);center(700,"USE A 2.4 GHz NETWORK",2);
  }else if(u.page==Page::Launcher){ // two doors: the verse of the day and the latest scores
   auto bar=[&](int y,const char* label,bool sel){c.fillRect(12,y,456,40,sel?0:1);c.drawRect(12,y,456,40,0);if(sel)text(24,y+12,">",2,1);text(sel?48:24,y+12,label,2,sel?1:0);text(468-12-6*12,y+12,"OPEN >",2,sel?1:0);};
   { // weather strip (selection 0): glyph, temperature and condition; press opens the forecast
    const bool sel=u.selected==LAUNCH_WEATHER;const int ink=sel?1:0;if(sel)c.fillRect(12,4,456,36,0);
    if(u.weather.valid){const Weather& w=u.weather;weatherGlyph(16,8,weatherIcon(w.code,w.day),1,ink);
     const std::string t=std::to_string(w.temp);text(54,16,t,2,ink);const int dx=54+int(t.size())*12+3;c.drawCircle(dx,18,2,ink);c.drawCircle(dx,18,1,ink);
     text(dx+12,16,weatherWord(w.code),2,ink,14); // the same pixel size as the battery figure
    }else text(16,16,u.online?"WEATHER LOADING...":"WEATHER: CONNECT WI-FI",2,ink);
    if(u.battery>=0){ // battery on the far right: percentage, then a cell outline filled to the level
     const std::string pct=std::to_string(u.battery)+"%";const int bx=468-8-30;text(bx-8-int(pct.size())*12,16,pct,2,ink);
     c.drawRect(bx,15,27,16,ink);c.fillRect(bx+27,19,3,8,ink);const int fill=(23*std::min(100,u.battery)+50)/100;if(fill>0)c.fillRect(bx+2,17,fill,12,ink);
    }
    c.drawFastHLine(16,46,448,0); // rule between the weather and the verse
   }
   { // Bible row (selection 1): the reference on the left, OPEN BIBLE on the right, the verse beneath
    const bool sel=u.selected==LAUNCH_BIBLE;const int ink=sel?1:0;if(sel)c.fillRect(12,54,456,36,0);
    std::string ref=u.votd.valid()?bibleRefLabel(u.votd):"BIBLE";
    if(ref.size()>15&&u.votd.valid())ref=upperText(bibleBooks[u.votd.book-1].abbr)+" "+std::to_string(u.votd.chapter)+":"+std::to_string(u.votd.verse); // long book names use the abbreviation here
    text(20,60,ref,3,ink,15);text(468-8-12*12,64,"OPEN BIBLE >",2,ink);
    if(u.votd.valid()){int y=98;for(const auto& line:launcherVerseLines(u)){read(12,y+1,line);y+=READ_LINE;}}
    else read(12,98,"Bible files missing: run tools/upload_bible.sh");
   }
   tabStrip(launcherSportsBar(u),u.tab,u.selected>=LAUNCH_TAB0?u.selected-LAUNCH_TAB0:-1); // press a tab to open its scoreboard
   if(u.tab==0)recentList(u,launcherScoresTop(u),-1);else leaguePage(u);
   if(!u.online)small(380,776,"WI-FI OFF");
  }else if(u.page==Page::Weather){ // now, then seven days
   const Weather& w=u.weather;text(12,24,"WEATHER",2);if(!w.city.empty())text(468-int(std::min<size_t>(w.city.size(),20))*12,24,upperText(w.city),2,0,20);c.drawFastHLine(12,50,456,0);
   if(!w.valid){center(360,"NO WEATHER YET",3);center(420,u.online?"FETCHING THE FORECAST...":"CONNECT WI-FI FOR WEATHER",2);}
   else{
    weatherGlyph(20,66,weatherIcon(w.code,w.day),3);const std::string t=std::to_string(w.temp);c.setTextSize(7);c.setTextColor(0);c.setCursor(128,76);c.print(t.c_str());c.drawCircle(128+int(t.size())*42+8,84,6,0);c.drawCircle(128+int(t.size())*42+8,84,5,0);
    text(128,138,weatherWord(w.code),2,0,24);small(128,162,"HIGH "+std::to_string(w.high)+"   LOW "+std::to_string(w.low)+"   RAIN "+std::to_string(w.rain)+"%");
    int y=196;c.drawFastHLine(12,y-6,456,0);
    for(size_t i=0;i<w.days.size()&&i<7;i++){const WeatherDay& d=w.days[i];
     text(12,y+22,weatherDayName(d.date,i),2,0,9);weatherGlyph(118,y+2,weatherIcon(d.code,true),2);text(188,y+12,weatherWord(d.code),2,0,13);text(188,y+38,"RAIN "+std::to_string(d.rain)+"%",2);
     const std::string hi=std::to_string(d.high),lo=std::to_string(d.low);text(468-int(lo.size())*12,y+28,lo,2);text(468-int(lo.size())*12-18-int(hi.size())*18,y+20,hi,3);
     y+=78;if(i+1<w.days.size()&&i<6)c.drawFastHLine(12,y-8,456,0);}
    small(12,776,"UPDATED "+clockLabel(w.fetched));
   }
  }else if(u.page==Page::BibleHome){ // the Bible's own home: resume, today's verse, or browse
   center(60,"BIBLE",3);smallCenter(240,96,"BEREAN STANDARD BIBLE - PUBLIC DOMAIN");
   const BibleView& b=u.bible;std::string cont="CONTINUE "+bibleRefLabel({b.book,b.chapter,0});if(b.pages.size()>1)cont+="  "+std::to_string(std::min(b.page,(int)b.pages.size()-1)+1)+"/"+std::to_string(b.pages.size());
   row(150,cont,u.selected==0);row(220,"VERSE OF THE DAY",u.selected==1);row(290,"BOOKS",u.selected==2);
   if(u.votd.valid()){text(12,380,bibleRefLabel(u.votd),2);int y=408;for(const auto& line:launcherVerseLines(u)){read(12,y+1,line);y+=READ_LINE;}}
   small(12,690,"HOLD THE ROCKER: \"GO TO PSALM 23\" OR \"READ JOHN 3:16\"");
  }else if(u.page==Page::Bible){ // the reader: chapter title and page counter, then flowing verses
   const BibleView& b=u.bible;const int pages=std::max(1,(int)b.pages.size()),page=std::min(b.page,pages-1);
   text(12,24,bibleRefLabel({b.book,b.chapter,0}),2,0,28);std::string pg=std::to_string(page+1)+"/"+std::to_string(pages);text(468-int(pg.size())*12,24,pg,2);c.drawFastHLine(12,50,456,0);
   if(b.pages.empty()){center(360,"NO BIBLE FILES",3);center(420,"RUN TOOLS/UPLOAD_BIBLE.SH",2);}
   else{int y=60;
    for(const auto& line:b.pages[page].lines){int x=12;
     for(const auto& s:line.segs){if(x>12)x+=readWidth(" ");
      const bool hl=b.verse>0&&s.of==b.verse;const int marker=s.verse?bibleMarkerWidth(s.verse):0,w=marker+readWidth(s.text);
      if(hl)c.fillRect(x-2,y,w+4,READ_LINE,0);
      if(s.verse){c.setTextSize(1);c.setTextColor(hl?1:0);c.setCursor(x,y+5);c.print(std::to_string(s.verse).c_str());}
      read(x+marker,y+1,s.text,hl?1:0,468-x-marker);x+=w;}
     y+=READ_LINE;}
   }
  }else if(u.page==Page::BibleBooks){ // 66 books in three columns
   text(12,24,"BOOKS",2);text(468-15*12,24,"PRESS: CHAPTERS",2);c.drawFastHLine(12,50,456,0);
   for(int i=0;i<BIBLE_BOOKS;i++){const int x=12+(i/22)*152,y=60+(i%22)*30;const bool sel=u.bible.pick==i;
    std::string name=bibleBooks[i].name;if(readWidth(name)>142){size_t t=name.find("Thessalonians");if(t!=std::string::npos)name=name.substr(0,t)+"Thess.";else if(name=="Song of Solomon")name="Song of Sol.";}
    if(sel)c.fillRect(x,y,152,30,0);read(x+6,y+4,name,sel?1:0,142);}
  }else if(u.page==Page::BibleChapters){ // chapter grid for the picked book
   const BibleBook& bk=bibleBooks[std::max(1,std::min(BIBLE_BOOKS,u.bible.pickBook))-1];std::string t=bk.name;for(auto& ch:t)ch=toupper((unsigned char)ch);
   text(12,24,t,2,0,20);text(468-8*12,24,"CHAPTERS",2);c.drawFastHLine(12,50,456,0);
   for(int i=0;i<bk.chapters;i++){const int x=12+(i%10)*45,y=60+(i/10)*40;const bool sel=u.bible.pick==i;
    if(sel)c.fillRect(x,y,45,40,0);const std::string n=std::to_string(i+1);text(x+(45-int(n.size())*12)/2,y+12,n,2,sel?1:0);}
  }else if(u.page==Page::Standings){
   standingsPage(u);
  }else if(u.page==Page::Voice){
   if(u.voice==VoiceState::Listening){
    center(150,"LISTENING",4);c.fillRoundRect(214,230,52,90,26,0);c.fillRect(230,330,20,30,0);c.fillRect(200,360,80,8,0);
    center(410,"ASK ABOUT A TEAM,",2);center(446,"A SCORE, OR A LEAGUE",2);center(530,"LET GO OF THE ROCKER",2);center(566,"WHEN YOU ARE DONE",2);
   }else if(u.voice==VoiceState::Thinking){
    center(150,"THINKING",4);if(u.voiceNote.empty())center(300,"ONE MOMENT...",2);else center(300,u.voiceNote,2);
   }else if(u.voice==VoiceState::Answer){
    int y=28;text(20,y,"YOU ASKED",2);y+=26;
    for(const auto& line:wrapWidth(u.voiceHeard,readWidth,440,3)){read(20,y,line,0,440);y+=READ_LINE;} // the question in the reading font
    y+=8;c.drawFastHLine(16,y,448,0);y+=16;
    for(const auto& line:wrapLines(u.voiceAnswer,36,10)){text(20,y,line,2,0,36);y+=24;}
    if(!u.voiceTeamId.empty()||u.voiceLeague>=0){
     std::string target=!u.voiceTeamId.empty()?u.voiceTeamName:std::string(leagues[u.voiceLeague].name);
     row(560,"OPEN "+target,true); // press opens the page the question was about
    }
   }else{
    center(150,"VOICE",4);{int y=300;for(const auto& line:wrapLines(u.voiceNote,30,4)){center(y,line,2);y+=36;}}
    center(520,u.voice==VoiceState::Error?"PRESS BOOT TO GO BACK":"RELEASE TO GO BACK",2);
   }
  }
  // The scoreboard uses the full height; other pages keep the control hints.
  if(u.page!=Page::Games&&u.page!=Page::Detail&&u.page!=Page::Home&&u.page!=Page::Standings&&u.page!=Page::Bible&&u.page!=Page::Launcher&&u.page!=Page::Weather&&u.page!=Page::Voice){c.drawFastHLine(12,746,456,0);center(757,"UP/DOWN MOVE   PRESS SELECT",2);center(777,"BOOT BACK    HOLD FOR VOICE",1);}
 }
};
}
