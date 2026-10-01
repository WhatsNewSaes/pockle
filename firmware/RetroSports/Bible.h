#pragma once
#include <string>
#include <vector>
#include <functional>
#include <cstdio>
#include <cctype>
#include <cstdlib>
// The Berean Standard Bible (public domain) lives on LittleFS as one plain-text
// file per chapter, verses one per line: /bible/<book 01-66>/<chapter 001>.txt
// (built by tools/build_bible.py, flashed by tools/upload_bible.sh). This header
// is the part shared with the host tests: book table, reference parsing, page
// layout and the verse-of-the-day pick.
namespace retro {
struct BibleBook { const char* name; const char* abbr; int chapters; };
static const BibleBook bibleBooks[]={
 {"Genesis","GEN",50},{"Exodus","EXO",40},{"Leviticus","LEV",27},{"Numbers","NUM",36},{"Deuteronomy","DEU",34},{"Joshua","JOS",24},{"Judges","JDG",21},{"Ruth","RUT",4},
 {"1 Samuel","1SA",31},{"2 Samuel","2SA",24},{"1 Kings","1KI",22},{"2 Kings","2KI",25},{"1 Chronicles","1CH",29},{"2 Chronicles","2CH",36},{"Ezra","EZR",10},{"Nehemiah","NEH",13},
 {"Esther","EST",10},{"Job","JOB",42},{"Psalm","PSA",150},{"Proverbs","PRO",31},{"Ecclesiastes","ECC",12},{"Song of Solomon","SNG",8},{"Isaiah","ISA",66},{"Jeremiah","JER",52},
 {"Lamentations","LAM",5},{"Ezekiel","EZK",48},{"Daniel","DAN",12},{"Hosea","HOS",14},{"Joel","JOL",3},{"Amos","AMO",9},{"Obadiah","OBA",1},{"Jonah","JON",4},
 {"Micah","MIC",7},{"Nahum","NAM",3},{"Habakkuk","HAB",3},{"Zephaniah","ZEP",3},{"Haggai","HAG",2},{"Zechariah","ZEC",14},{"Malachi","MAL",4},{"Matthew","MAT",28},
 {"Mark","MRK",16},{"Luke","LUK",24},{"John","JHN",21},{"Acts","ACT",28},{"Romans","ROM",16},{"1 Corinthians","1CO",16},{"2 Corinthians","2CO",13},{"Galatians","GAL",6},
 {"Ephesians","EPH",6},{"Philippians","PHP",4},{"Colossians","COL",4},{"1 Thessalonians","1TH",5},{"2 Thessalonians","2TH",3},{"1 Timothy","1TI",6},{"2 Timothy","2TI",4},{"Titus","TIT",3},
 {"Philemon","PHM",1},{"Hebrews","HEB",13},{"James","JAS",5},{"1 Peter","1PE",5},{"2 Peter","2PE",3},{"1 John","1JN",5},{"2 John","2JN",1},{"3 John","3JN",1},{"Jude","JUD",1},{"Revelation","REV",22}};
constexpr int BIBLE_BOOKS=66;
// A place in the Bible. book is 1-66 (0 = none), verse 0 = the whole chapter.
struct BibleRef {
 int book=0,chapter=0,verse=0;
 bool valid()const{return book>=1&&book<=BIBLE_BOOKS&&chapter>=1&&chapter<=bibleBooks[book-1].chapters;}
};
inline std::string biblePath(int book,int chapter){char p[32];snprintf(p,sizeof(p),"/bible/%02d/%03d.txt",book,chapter);return p;}
inline std::string bibleRefLabel(const BibleRef& r,bool upper=true){
 if(!r.valid())return "";std::string s=bibleBooks[r.book-1].name;s+=" "+std::to_string(r.chapter);if(r.verse>0)s+=":"+std::to_string(r.verse);
 if(upper)for(auto& c:s)c=toupper((unsigned char)c);return s;
}
// Lower-case letters and digits only, with the spoken forms folded in
// ("First John" -> "1john", "Psalms" -> "psalm", "Song of Songs" -> "songofsolomon").
inline std::string bibleKey(const std::string& s){
 std::string k;for(unsigned char c:s)if(isalnum(c))k+=(char)tolower(c);
 auto swap=[&](const char* from,const char* to){if(k.rfind(from,0)==0)k=to+k.substr(std::string(from).size());};
 swap("first","1");swap("second","2");swap("third","3");swap("1st","1");swap("2nd","2");swap("3rd","3");
 if(k=="psalms"||k=="ps"||k=="pss")k="psalm";if(k=="songofsongs"||k=="canticles"||k=="song")k="songofsolomon";if(k=="revelations")k="revelation";
 return k;
}
// 1-based book index for a spoken or written name, 0 if nothing matches. Exact
// names and abbreviations win; otherwise the first book whose name starts with
// the text (at least three letters, so "Jud" is Judges and "Jude" is Jude).
inline int bibleBookIndex(const std::string& name){
 const std::string k=bibleKey(name);if(k.empty())return 0;
 for(int i=0;i<BIBLE_BOOKS;i++)if(bibleKey(bibleBooks[i].name)==k||bibleKey(bibleBooks[i].abbr)==k)return i+1;
 if(k.size()<3)return 0;
 for(int i=0;i<BIBLE_BOOKS;i++)if(bibleKey(bibleBooks[i].name).rfind(k,0)==0)return i+1;
 return 0;
}
// "John 3:16", "1 John 3", "Psalm 23 verse 1", "john 3 16": the book is
// everything before the trailing numbers.
inline BibleRef parseBibleRef(const std::string& text){
 std::vector<std::string> tokens;std::string cur;
 for(unsigned char c:text){if(isalnum(c))cur+=(char)c;else if(!cur.empty()){tokens.push_back(cur);cur.clear();}}
 if(!cur.empty())tokens.push_back(cur);
 auto numeric=[](const std::string& t){for(unsigned char c:t)if(!isdigit(c))return false;return !t.empty();};
 auto noise=[](std::string t){for(auto& c:t)c=tolower((unsigned char)c);return t=="chapter"||t=="verse"||t=="verses"||t=="ch"||t=="v"||t=="vs";};
 tokens.erase(std::remove_if(tokens.begin(),tokens.end(),noise),tokens.end());
 int last=-1;for(size_t i=0;i<tokens.size();i++)if(!numeric(tokens[i])||i==0)last=i; // a leading digit belongs to "1 John"
 BibleRef r;for(int start=0;start<=last&&!r.book;start++){std::string book;for(int i=start;i<=last;i++)book+=tokens[i]+" ";r.book=bibleBookIndex(book);} // "go to Psalm 23": skip the leading words
 if(!r.book)return BibleRef{};
 std::vector<int> nums;for(size_t i=last+1;i<tokens.size()&&nums.size()<2;i++)nums.push_back(atoi(tokens[i].c_str()));
 r.chapter=nums.empty()?1:nums[0];r.verse=nums.size()>1?nums[1]:0;
 if(bibleBooks[r.book-1].chapters==1&&nums.size()==1){r.verse=nums[0];r.chapter=1;} // "Jude 24"
 if(!r.valid())r.chapter=1;
 return r;
}
inline std::vector<std::string> bibleVerses(const std::string& text){
 std::vector<std::string> v;size_t p=0;
 while(p<text.size()){size_t q=text.find('\n',p);v.push_back(text.substr(p,q==std::string::npos?std::string::npos:q-p));if(q==std::string::npos)break;p=q+1;}
 while(!v.empty()&&v.back().empty())v.pop_back();
 return v;
}
// A page of flowing text: verse numbers sit inline before their first word.
struct BibleSeg { int verse,of; std::string text; }; // verse > 0: draw the number before the text; `of` is the verse the words belong to
struct BibleLine { std::vector<BibleSeg> segs; };
struct BiblePage { std::vector<BibleLine> lines; int firstVerse=0,lastVerse=0; };
inline int bibleMarkerWidth(int verse){return int(std::to_string(verse).size())*6+3;}
// Wraps the verses with a proportional-font width callback into pages of
// `linesPerPage` lines no wider than `maxWidth`.
inline std::vector<BiblePage> paginateBible(const std::vector<std::string>& verses,const std::function<int(const std::string&)>& width,int maxWidth,int linesPerPage){
 std::vector<BiblePage> pages;BiblePage page;BibleLine line;int lineWidth=0;const int space=width(" ");
 auto flushLine=[&](){if(line.segs.empty())return;page.lines.push_back(line);line=BibleLine{};lineWidth=0;if((int)page.lines.size()>=linesPerPage){pages.push_back(page);page=BiblePage{};}};
 for(size_t v=0;v<verses.size();v++){
  const int number=v+1;std::string word;size_t p=0;bool first=true;const std::string& text=verses[v];
  if(text.empty())continue;
  while(p<=text.size()){
   size_t q=text.find(' ',p);word=text.substr(p,q==std::string::npos?std::string::npos:q-p);p=q==std::string::npos?text.size()+1:q+1;
   if(word.empty())continue;
   const int marker=first?bibleMarkerWidth(number):0,w=width(word);
   const int need=(line.segs.empty()?0:space)+marker+w;
   if(!line.segs.empty()&&lineWidth+need>maxWidth)flushLine();
   const bool newSeg=first||line.segs.empty();
   if(newSeg){line.segs.push_back({first?number:0,number,word});lineWidth+=(line.segs.size()>1?space:0)+marker+w;}
   else{line.segs.back().text+=" "+word;lineWidth+=space+w;}
   if(first){if(!page.firstVerse)page.firstVerse=number;first=false;}
   page.lastVerse=number;
  }
 }
 flushLine();if(!page.lines.empty())pages.push_back(page);
 if(pages.empty())pages.push_back(BiblePage{});
 for(auto& pg:pages)if(!pg.firstVerse&&!pg.lines.empty()){for(const auto& l:pg.lines){for(const auto& s:l.segs)if(s.verse){pg.firstVerse=s.verse;break;}if(pg.firstVerse)break;}}
 return pages;
}
// Page index holding a verse (the page where it starts, or the page where the
// previous verse continues into it); 0 when the verse is not there.
inline int biblePageOf(const std::vector<BiblePage>& pages,int verse){
 for(size_t i=0;i<pages.size();i++)for(const auto& l:pages[i].lines)for(const auto& s:l.segs)if(s.verse==verse)return i;
 for(size_t i=0;i<pages.size();i++)if(pages[i].firstVerse<=verse&&verse<=pages[i].lastVerse)return i;
 return 0;
}
// Word-wraps with a proportional-font width callback; the last allowed line
// is cut with "..." when the text runs on.
inline std::vector<std::string> wrapWidth(const std::string& text,const std::function<int(const std::string&)>& width,int maxWidth,size_t maxLines){
 std::vector<std::string> lines;std::string line,word;size_t p=0;
 while(p<=text.size()){size_t q=text.find(' ',p);word=text.substr(p,q==std::string::npos?std::string::npos:q-p);p=q==std::string::npos?text.size()+1:q+1;if(word.empty())continue;
  std::string trial=line.empty()?word:line+" "+word;
  if(!line.empty()&&width(trial)>maxWidth){lines.push_back(line);line=word;if(lines.size()==maxLines)break;}else line=trial;}
 if(lines.size()<maxLines&&!line.empty())lines.push_back(line);
 else if(lines.size()==maxLines&&(p<=text.size()||!line.empty())){std::string& l=lines.back();while(!l.empty()&&width(l+"...")>maxWidth)l.pop_back();l+="...";}
 return lines;
}
// What to read aloud: the verse asked for, or the opening of the chapter.
inline std::string bibleSpeakText(const std::vector<std::string>& verses,const BibleRef& r,size_t limit=600){
 if(r.verse>0)return r.verse<=(int)verses.size()?verses[r.verse-1]:"";
 std::string out;for(const auto& v:verses){if(!out.empty()&&out.size()+v.size()>limit)break;if(!out.empty())out+=" ";out+=v;}
 return out;
}
// Verse of the day: well-known verses in a fixed order, chosen by the day of
// the year so every device agrees and it works offline.
struct BibleRefLit { uint8_t book,chapter,verse; };
static const BibleRefLit verseOfDayTable[]={
 {43,3,16},{19,23,1},{45,8,28},{50,4,13},{24,29,11},{20,3,5},{23,41,10},{40,11,28},{19,46,1},{45,12,2},{6,1,9},{48,5,22},{58,11,1},{46,13,4},{19,119,105},{23,40,31},
 {40,6,33},{43,14,6},{49,2,8},{55,1,7},{19,27,1},{45,5,8},{62,4,19},{20,22,6},{19,139,14},{40,28,19},{43,1,1},{1,1,1},{19,118,24},{25,3,22},{33,6,8},{45,6,23},
 {59,1,5},{50,4,6},{60,5,7},{19,37,4},{23,53,5},{40,5,16},{43,15,5},{51,3,23},{19,34,8},{45,15,13},{58,13,8},{19,91,1},{20,18,10},{43,16,33},{5,31,6},{19,100,4},
 {47,5,17},{49,6,10},{40,7,7},{19,121,1},{45,10,9},{52,5,16},{19,19,1},{23,9,6},{42,2,11},{43,8,12},{19,56,3},{20,16,3},{48,2,20},{19,62,1},{40,22,37},{46,10,13},
 {19,51,10},{58,4,12},{59,1,17},{19,145,18},{43,10,10},{45,8,38},{20,4,23},{19,16,11},{23,26,3},{40,5,14},{51,3,2},{19,32,8},{49,4,32},{19,103,12},{43,11,25},{45,3,23},
 {19,30,5},{20,17,17},{47,12,9},{19,90,12},{40,19,26},{62,1,9},{19,55,22},{56,3,5},{58,12,1},{19,42,1},{23,43,2},{42,1,37},{49,3,20},{19,73,26},{36,3,17},{45,8,1},
 {20,15,1},{19,1,1},{43,13,34},{60,2,9},{19,84,11},{23,55,8},{40,6,34},{48,6,9},{19,40,1},{14,7,14},{45,12,12},{19,147,3},{59,4,8},{34,1,7},{19,63,1},{43,14,27},
 {49,5,1},{19,130,5},{35,3,18},{40,5,9},{46,16,14},{19,25,4},{28,6,6},{42,6,31},{19,9,9},{45,8,18},{20,11,25},{19,143,8},{2,14,14},{40,18,20},{58,10,23},{19,86,5},
 {23,12,2},{21,3,1},{19,94,19},{43,6,35},{50,1,6},{19,28,7},{4,6,24},{40,4,4},{62,3,1},{19,104,24},{23,30,21},{42,12,7},{19,107,1},{65,1,24},{20,27,17},{19,66,1},
 {53,3,3},{45,15,4},{19,138,8},{29,2,13},{41,10,27},{19,4,8},{49,1,7},{19,150,6},{38,4,6},{40,25,40},{19,3,3},{51,2,6},{19,111,10},{27,3,17},{43,4,24},{19,5,3},
 {66,21,4},{19,36,5},{39,3,6},{54,4,12},{19,71,5},{23,61,1},{40,6,9},{19,133,1},{30,5,24},{58,13,5},{19,113,3},{32,2,9},{42,11,9},{19,29,11},{8,1,16},{45,13,10},
 {19,24,1},{1,28,15},{59,5,16},{19,68,19},{9,16,7},{40,5,44},{19,89,1},{26,36,26},{49,2,10},{19,33,4},{5,6,5},{43,20,29},{19,20,7},{18,19,25},{61,3,9},{19,105,1},
 {23,6,8},{42,19,10},{19,18,2},{20,19,21},{44,1,8},{19,128,1},{57,1,6},{66,3,20},{19,8,1},{40,16,24},{19,95,1},{1,50,20},{46,15,58},{19,123,1},{16,8,10},{41,12,30},
 {19,122,1},{58,6,19},{20,31,25},{19,146,5},{44,4,12},{19,78,4},{56,2,11},{19,65,8},{23,1,18},{42,10,27},{19,112,7},{45,1,16},{19,96,1},{47,4,16},{19,141,3},{40,14,27},
 {19,37,23},{43,12,46},{19,116,1},{48,3,28},{19,75,1},{24,33,3},{19,57,10},{66,22,20},{19,148,13},{23,58,11},{20,10,12},{19,15,1},{52,5,18},{19,67,1},{3,19,18},{19,108,4},
 {40,7,12},{19,52,8},{10,22,31},{19,85,8},{17,4,14},{21,4,9},{19,106,1},{19,136,1},{66,4,8},{40,5,3},{19,23,4},{19,121,7},{23,40,8},{43,15,13},{45,8,31},{19,119,11},
 {58,4,16},{19,27,14},{62,4,7},{19,34,18},{20,3,6},{43,14,1},{19,46,10},{40,6,21},{23,43,1},{19,139,23},{42,6,38},{19,100,5},{55,3,16},{19,92,1},{51,3,15},{19,31,24},
 {43,1,12},{19,119,9},{60,3,15},{19,37,5},{42,18,27},{19,103,1},{49,4,2},{19,50,15},{23,54,10},{19,119,130},{40,24,35},{19,119,165},{45,12,10},{19,126,5},{66,1,8},{19,41,1},
 {41,9,23},{19,119,114}};
inline BibleRef verseOfDay(int dayOfYear){
 const int n=sizeof(verseOfDayTable)/sizeof(verseOfDayTable[0]);const BibleRefLit& l=verseOfDayTable[((dayOfYear%n)+n)%n];
 BibleRef r;r.book=l.book;r.chapter=l.chapter;r.verse=l.verse;return r;
}
}
