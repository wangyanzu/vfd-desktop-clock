#include "clock/pending_messages.h"
#include <cassert>
#include <iostream>
using namespace clockapp;
IncomingMessage message(char text,uint8_t level,uint32_t generation=1) {
  IncomingMessage item; item.payload[0]=text; item.length=1; item.level=level; item.generation=generation; return item;
}
int main() {
  using P=PendingMessages::Push;
  PendingMessages queue; IncomingMessage item;
  assert(!queue.pop(item));
  auto invalid=message('x',4); assert(queue.push(invalid)==P::rejected);
  invalid=message('x',1); invalid.length=257; assert(queue.push(invalid)==P::rejected);
  assert(queue.push(message('a',2))==P::added);
  assert(queue.push(message('b',1))==P::added);
  assert(queue.push(message('c',2))==P::added);
  assert(queue.push(message('d',1))==P::added);
  assert(queue.push(message('e',3))==P::evicted); // oldest lowest: b
  assert(queue.push(message('e',3))==P::duplicate);
  assert(queue.push(message('f',2))==P::evicted); // next lowest: d
  assert(queue.push(message('g',1))==P::rejected);
  for (char expected : {'a','c','e','f'}) { assert(queue.pop(item)); assert(item.payload[0]==expected); }
  assert(!queue.pop(item));
  for (char c : {'a','b','c','d'}) assert(queue.push(message(c,3))==P::added);
  assert(queue.push(message('e',3))==P::rejected); // critical is never evicted
  assert(queue.pop(item) && item.payload[0]=='a');
  assert(queue.push(message('e',3))==P::added);
  PendingMessages generations;
  assert(generations.push(message('x',1,1))==P::added);
  assert(generations.push(message('x',1,2))==P::added); // preserve generation for main-thread filtering
  assert(generations.pop(item) && item.generation==1);
  assert(generations.pop(item) && item.generation==2);
  PendingMessages wrapped;
  for (int round=0;round<100;++round) {
    for (char c : {'a','b','c','d'}) assert(wrapped.push(message(c,1))==P::added);
    assert(wrapped.push(message('e',1))==P::evicted);
    for (char c : {'b','c','d','e'}) { assert(wrapped.pop(item)); assert(item.payload[0]==c); }
  }
  std::cout<<"PASS: MQTT staging priority eviction, critical protection, dedup, FIFO and config generations\n";
}
