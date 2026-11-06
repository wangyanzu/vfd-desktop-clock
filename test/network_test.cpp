#include "clock/network_policy.h"
#include <cassert>
#include <iostream>
using namespace clockapp;
int main(){
 NetworkPolicy fresh;fresh.configure(0,false);assert(fresh.tick(0,false).openSetup);assert(!fresh.tick(600000,false).closeSetup);
 NetworkPolicy configured;configured.configure(0,true);assert(!configured.tick(0,false).openSetup);assert(configured.tick(10000,false).retry);assert(!configured.tick(30000,false).openSetup);
 configured.requestSetup();assert(configured.tick(30001,false).openSetup);assert(!configured.tick(330000,false).closeSetup);assert(configured.tick(330001,false).closeSetup);assert(!configured.tick(360000,false).openSetup);
 configured.requestSetup();assert(configured.tick(400000,true).openSetup);assert(!configured.tick(460000,true).closeSetup);assert(configured.tick(700000,true).closeSetup);
 NetworkPolicy wrap;uint32_t start=UINT32_MAX-100;wrap.configure(start,true);wrap.requestSetup();assert(wrap.tick(start,false).openSetup);assert(wrap.tick(start+300000,false).closeSetup);
 std::cout<<"PASS: first setup remains accessible, configured devices retry silently, explicit maintenance expiry and rollover\n";
}
