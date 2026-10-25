#pragma once
#include "clock/core.h"
#include <cstdlib>

namespace clockapp {
struct OutdoorWeather {
  float temperature=0, humidity=0;
  bool valid=false, humidityValid=false;
  uint32_t receivedAt=0, sourceAgeSeconds=0;
  char city[65]{}, text[65]{}, updated[32]{};
  int code=-1;
  bool fresh(uint32_t now) const {
    return valid && uint64_t(sourceAgeSeconds)+uint32_t(now-receivedAt)/1000 < 5400;
  }
};
inline bool weatherNumber(const char* value,float minimum,float maximum,float& output) {
  if (!value || !*value || std::strlen(value)>16) return false;
  // Full decimal values only, including JSON string representations.
  bool digit=false, dot=false; const char* p=value;
  if (*p=='-' || *p=='+') ++p;
  for (;*p;++p) {
    if (*p=='.' && !dot) { dot=true; continue; }
    if (*p<'0' || *p>'9') return false;
    digit=true;
  }
  if (!digit) return false;
  const float parsed=std::strtof(value,nullptr);
  if (!std::isfinite(parsed) || parsed<minimum || parsed>maximum) return false;
  output=parsed; return true;
}
// Validate the provider's observation timestamp, including its timezone.
inline bool weatherEpoch(const char* value,int64_t& output) {
  if (!value) return false;
  const size_t n=std::strlen(value);
  if (n!=20 && n!=25) return false;
  for (size_t i=0;i<n;++i) {
    if (i==4 || i==7) { if (value[i]!='-') return false; }
    else if (i==10) { if (value[i]!='T') return false; }
    else if (i==13 || i==16 || (n==25 && i==22)) { if (value[i]!=':') return false; }
    else if (i==19) { if (n==20 ? value[i]!='Z' : (value[i]!='+' && value[i]!='-')) return false; }
    else if (value[i]<'0' || value[i]>'9') return false;
  }
  const auto two=[&](size_t at) { return (value[at]-'0')*10+value[at+1]-'0'; };
  const int year=two(0)*100+two(2),month=two(5),day=two(8),hour=two(11),minute=two(14),second=two(17);
  const auto leap=[](int y) { return y%4==0 && (y%100!=0 || y%400==0); };
  const int lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};
  if (year<2020 || year>2099 || month<1 || month>12 || day<1 || day>lengths[month-1]+(month==2 && leap(year)) || hour>23 || minute>59 || second>59) return false;
  int64_t days=0;
  for (int y=1970;y<year;++y) days+=365+leap(y);
  for (int m=1;m<month;++m) days+=lengths[m-1]+(m==2 && leap(year));
  days+=day-1;
  int offset=0;
  if (n==25) {
    if (two(20)>14 || two(23)>59 || (two(20)==14 && two(23))) return false;
    offset=(two(20)*60+two(23))*(value[19]=='-'?-1:1);
  }
  output=days*86400+hour*3600+minute*60+second-offset*60; return true;
}
inline const char* weatherLabel(int code) {
  if (code>=0 && code<=3) return "SUNNY";
  if (code>=4 && code<=8) return "CLOUDY";
  if (code==9) return "OVERCAST";
  if (code>=10 && code<=19) return "RAIN";
  if (code>=20 && code<=25) return "SNOW";
  if (code==30) return "FOG";
  if (code==31) return "HAZE";
  return "WEATHER";
}
inline Frame renderComparison(const Environment& indoor,const OutdoorWeather& outdoor,uint32_t now,bool weatherOnly=false) {
  Frame frame; char text[40];
  if (weatherOnly) {
    if (!outdoor.valid) frame.set(1,"Weather unavailable");
    else { std::snprintf(text,sizeof(text),"OUT:%.1fC %s",outdoor.temperature,weatherLabel(outdoor.code)); frame.set(1,text); }
  } else {
    char in[10]="--.-",out[10]="--.-";
    if (indoor.valid) std::snprintf(in,sizeof(in),"%.1f",indoor.temperature);
    if (outdoor.valid) std::snprintf(out,sizeof(out),"%.1f",outdoor.temperature);
    std::snprintf(text,sizeof(text),"IN:%s OUT:%sC",in,out); frame.set(1,text);
  }
  if ((!weatherOnly && !indoor.fresh(now)) || !outdoor.fresh(now)) frame.lines[1][19]='*';
  return frame;
}
}
