#include "../Arduino Sketch/src/pager_mailbox.h"
#include <string>
#include <cassert>
#include <iostream>
struct Message {
  unsigned addr = 0;
  std::string ricName, text;
  bool valid = false;
};
Message message(unsigned addr, const char* key, const char* text) {
  Message m; m.addr=addr; m.ricName=key; m.text=text; m.valid=true; return m;
}
int main() {
  Message p[2], w[3], a[2];
  PagerMailbox<Message> personal(p,2), weather(w,3), warnings(a,2);
  personal.push(message(123,"ME","keep"));
  for(int i=0;i<1000;++i) weather.push(message(4520,"R61:1","new"));
  assert(weather.count==3 && personal.count==1 && p[0].text=="keep");
  weather.clear();
  weather.push(message(4520,"R61:1","old"));
  weather.push(message(4520,"R80:1","temperature"));
  const int selected=weather.current;
  assert(weather.update(message(4520,"R61:1","new")));
  assert(weather.count==2 && w[0].text=="new" && w[1].text=="temperature");
  assert(weather.current==selected); // background updates preserve selection
  assert(!weather.update(message(4520,"R61:2","different station")));
  warnings.push(message(4520,"R39:1","smoke"));
  assert(warnings.containsText(message(4520,"R39:2","smoke")));
  assert(!warnings.containsText(message(4520,"R39:1","fire")));
  weather.clear();assert(weather.count==0 && personal.count==1 && warnings.count==1);
  // A deleted slot is reused without losing another message or corrupting count.
  personal.push(message(123,"ME","second"));
  p[1]=Message{}; --personal.count;
  personal.push(message(123,"ME","third"));
  assert(personal.count==2 && p[0].text=="keep" && p[1].text=="third");
  const unsigned wr[]={61,63,80}, ar[]={39};
  assert(skyperFolder(61,wr,3,ar,1)==WeatherInbox);
  assert(skyperFolder(80,wr,3,ar,1)==WeatherInbox);
  assert(skyperFolder(39,wr,3,ar,1)==WarningInbox);
  assert(skyperFolder(28,wr,3,ar,1)==-1);
  std::cout<<"PASS: separate capacity, weather replacement, warning deduplication, deletion and routing\n";
}
