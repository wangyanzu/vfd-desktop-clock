#include "clock/w4home_identity.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace clockapp::w4home;
int main() {
  Binding b{};
  std::strcpy(b.mac,"02000000a1b2");
  std::strcpy(b.server,"https://w4.h.p5.pw:88");
  std::memset(b.token,'a',64);
  assert(matches(b,b.mac,b.server));
  assert(!matches(b,"02000000a1b3",b.server));
  assert(!matches(b,b.mac,"https://other.example"));
  b.version=2; assert(!valid(b)); b.version=1;
  b.token[64]='a'; assert(!valid(b)); b.token[64]=0;
  b.token[5]='G'; assert(!valid(b)); b.token[5]='a';
  b.mac[0]='A'; assert(!valid(b)); b.mac[0]='0';
  std::memset(b.server,'x',sizeof(b.server)); assert(!valid(b));
  std::strcpy(b.server,"ftp://example.com"); assert(!valid(b));
  assert(!valid(Binding{}));
  std::cout << "PASS: versioned credential binding rejects wrong MAC/server and corrupt records\n";
}
