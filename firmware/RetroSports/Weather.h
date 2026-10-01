#pragma once
#include <ArduinoJson.h>
#include <string>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <vector>
#include "Core.h"
// Today's weather for the launcher strip. Open-Meteo (free, no key) answers for
// the board's location, which is looked up once from the public IP (ipinfo.io)
// and kept in NVS. Both hosts chain to ISRG Root X1, already in Certificates.h.
namespace retro {
struct WeatherDay { std::string date; int code=0,high=0,low=0,rain=0; }; // date YYYY-MM-DD, local
struct Weather { bool valid=false; int temp=0,high=0,low=0,rain=0,code=0; bool day=true; std::string city; int64_t fetched=0; std::vector<WeatherDay> days; };
inline std::string weatherUrl(const std::string& lat,const std::string& lon){
 return "https://api.open-meteo.com/v1/forecast?latitude="+lat+"&longitude="+lon+"&current=temperature_2m,weather_code,is_day&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max&temperature_unit=fahrenheit&timezone=auto&forecast_days=7";
}
// WMO weather codes as words, and as an icon: 0 sun, 1 moon, 2 sun and cloud, 3 cloud, 4 rain, 5 snow, 6 storm, 7 fog.
inline const char* weatherWord(int code){
 if(code==0)return "CLEAR";if(code<=2)return "PARTLY CLOUDY";if(code==3)return "CLOUDY";if(code<=48)return "FOG";if(code<=57)return "DRIZZLE";
 if(code<=67)return "RAIN";if(code<=77)return "SNOW";if(code<=82)return "SHOWERS";if(code<=86)return "SNOW SHOWERS";return "THUNDERSTORMS";
}
inline int weatherIcon(int code,bool day){
 if(code==0)return day?0:1;if(code<=2)return day?2:3;if(code==3)return 3;if(code<=48)return 7;if(code<=67)return 4;if(code<=77)return 5;if(code<=82)return 4;if(code<=86)return 5;return 6;
}
// Open-Meteo geocoding for a typed ZIP or city: the first result's coordinates and name.
inline std::string geocodeUrl(const std::string& query){
 std::string q;for(unsigned char c:query){if(isalnum(c))q+=(char)c;else if(c==' ')q+="%20";}
 return "https://geocoding-api.open-meteo.com/v1/search?name="+q+"&count=1&language=en&format=json";
}
inline bool decodeGeocode(JsonVariantConst root,std::string& lat,std::string& lon,std::string& city){
 JsonVariantConst r=root["results"][0];if(r.isNull())return false;
 char b[24];snprintf(b,sizeof(b),"%.4f",r["latitude"].as<double>());lat=b;snprintf(b,sizeof(b),"%.4f",r["longitude"].as<double>());lon=b;
 const char* n=r["name"].as<const char*>();city=n?n:"";return true;
}
// ipinfo.io/json: {"city":"Williamsburg","loc":"39.0542,-84.0530",...}
inline bool decodeLocation(JsonVariantConst root,std::string& lat,std::string& lon,std::string& city){
 const char* loc=root["loc"].as<const char*>();if(!loc)return false;std::string s=loc;size_t comma=s.find(',');if(comma==std::string::npos)return false;
 lat=s.substr(0,comma);lon=s.substr(comma+1);const char* c=root["city"].as<const char*>();city=c?c:"";
 return !lat.empty()&&!lon.empty();
}
inline bool decodeWeather(JsonVariantConst root,int64_t now,Weather& out){
 JsonVariantConst cur=root["current"],daily=root["daily"];if(cur.isNull()||daily.isNull())return false;
 Weather w;w.temp=(int)lround(cur["temperature_2m"].as<double>());w.code=cur["weather_code"].as<int>();w.day=cur["is_day"].as<int>()!=0;
 w.high=(int)lround(daily["temperature_2m_max"][0].as<double>());w.low=(int)lround(daily["temperature_2m_min"][0].as<double>());w.rain=daily["precipitation_probability_max"][0].as<int>();
 JsonArrayConst times=daily["time"].as<JsonArrayConst>();for(size_t i=0;i<times.size()&&i<7;i++){WeatherDay d;d.date=times[i].as<const char*>()?times[i].as<const char*>():"";
  d.code=daily["weather_code"][i].as<int>();d.high=(int)lround(daily["temperature_2m_max"][i].as<double>());d.low=(int)lround(daily["temperature_2m_min"][i].as<double>());d.rain=daily["precipitation_probability_max"][i].as<int>();w.days.push_back(d);}
 w.city=out.city;w.fetched=now;w.valid=true;out=w;return true;
}
// "TODAY" for the first row, then "THU 10/1".
inline std::string weatherDayName(const std::string& date,int index){
 if(index==0)return "TODAY";if(date.size()<10)return "";
 static const char* names[]={"SUN","MON","TUE","WED","THU","FRI","SAT"};const int y=atoi(date.substr(0,4).c_str()),m=atoi(date.substr(5,2).c_str()),d=atoi(date.substr(8,2).c_str());
 const int64_t days=utcEpoch(y,m,d)/86400;const int wday=(int)(((days%7)+7+4)%7); // 1970-01-01 was a Thursday
 return std::string(names[wday])+" "+std::to_string(m)+"/"+std::to_string(d);
}
inline std::string upperText(std::string s){for(auto& c:s)c=toupper((unsigned char)c);return s;}
// The strip after the temperature: "HI 65  LO 59  RAIN 92%  CLOUDY".
inline std::string weatherLine(const Weather& w){
 return "HI "+std::to_string(w.high)+"  LO "+std::to_string(w.low)+"  RAIN "+std::to_string(w.rain)+"%  "+weatherWord(w.code);
}
inline std::string lowerText(std::string s){for(auto& c:s)c=tolower((unsigned char)c);return s;}
// For the voice context: now, then each forecast day.
inline std::string weatherSpeech(const Weather& w){
 if(!w.valid)return "";
 std::string s="now "+std::to_string(w.temp)+" F and "+lowerText(weatherWord(w.code))+(w.city.empty()?"":" in "+w.city)+"; today high "+std::to_string(w.high)+", low "+std::to_string(w.low)+", "+std::to_string(w.rain)+"% chance of rain";
 for(size_t i=1;i<w.days.size();i++){const WeatherDay& d=w.days[i];s+="; "+weatherDayName(d.date,i)+" high "+std::to_string(d.high)+" low "+std::to_string(d.low)+" "+std::to_string(d.rain)+"% rain "+lowerText(weatherWord(d.code));}
 return s;
}
// NVS cache: "temp|high|low|rain|code|day|fetched|city|date,code,high,low,rain;...".
inline std::string encodeWeatherCache(const Weather& w){
 std::string s=std::to_string(w.temp)+"|"+std::to_string(w.high)+"|"+std::to_string(w.low)+"|"+std::to_string(w.rain)+"|"+std::to_string(w.code)+"|"+(w.day?"1":"0")+"|"+std::to_string((long long)w.fetched)+"|"+w.city+"|";
 for(const auto& d:w.days)s+=d.date+","+std::to_string(d.code)+","+std::to_string(d.high)+","+std::to_string(d.low)+","+std::to_string(d.rain)+";";
 return s;
}
inline bool decodeWeatherCache(const std::string& s,Weather& out){
 std::vector<std::string> f;size_t p=0;while(f.size()<9){size_t q=s.find('|',p);if(q==std::string::npos||f.size()==8){f.push_back(s.substr(p));break;}f.push_back(s.substr(p,q-p));p=q+1;}
 if(f.size()<8)return false;Weather w;w.temp=atoi(f[0].c_str());w.high=atoi(f[1].c_str());w.low=atoi(f[2].c_str());w.rain=atoi(f[3].c_str());w.code=atoi(f[4].c_str());w.day=f[5]=="1";w.fetched=strtoll(f[6].c_str(),nullptr,10);w.city=f[7];
 if(f.size()>8){size_t a=0;const std::string& ds=f[8];while(a<ds.size()){size_t e=ds.find(';',a);std::string item=ds.substr(a,e==std::string::npos?std::string::npos:e-a);if(item.empty())break;
   WeatherDay d;int v[4]={0,0,0,0};size_t c=item.find(',');d.date=item.substr(0,c);for(int k=0;k<4&&c!=std::string::npos;k++){size_t n=item.find(',',c+1);v[k]=atoi(item.substr(c+1,n==std::string::npos?std::string::npos:n-c-1).c_str());c=n;}
   d.code=v[0];d.high=v[1];d.low=v[2];d.rain=v[3];w.days.push_back(d);if(e==std::string::npos)break;a=e+1;}}
 w.valid=w.fetched>0;out=w;return w.valid;
}
}
