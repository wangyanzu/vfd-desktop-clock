#include "clock/settings.h"
#include <cassert>
#include <algorithm>
#include <iostream>
using namespace clockapp;
int main() {
  Settings original;
  std::strcpy(original.ssid, "Desk WiFi"); std::strcpy(original.password, "initial-key");
  original.utcOffsetMinutes = -330;
  assert(validSettings(original));
  assert(validUtf8("桌面 WiFi"));
  assert(!validUtf8(std::string("\xff", 1)));
  assert(!validUtf8(std::string("\xc0\x80", 2)));
  assert(!validUtf8(std::string("\xed\xa0\x80", 3)));
  assert(!validUtf8(std::string("\xf4\x90\x80\x80", 4)));
  const auto encoded = encodeSettings(original);
  Settings decoded;
  assert(decodeSettings(encoded, decoded));
  assert(sameNetwork(original, decoded) && decoded.utcOffsetMinutes == -330);
  assert(decoded.brightness == 2 && std::strcmp(decoded.ntp, original.ntp) == 0);
  for (size_t byte = 0; byte < encoded.size(); ++byte) {
    auto corrupt = encoded; corrupt[byte] ^= 0x01;
    Settings unchanged; unchanged.brightness = 4;
    assert(!decodeSettings(corrupt, unchanged)); assert(unchanged.brightness == 4);
  }
  auto unterminated = encoded;
  for (size_t i = 4; i < 37; ++i) unterminated[i] = 'a';
  const auto checksum = crc32(unterminated.data(), 171);
  for (unsigned i = 0; i < 4; ++i) unterminated[171 + i] = (checksum >> (i * 8)) & 0xff;
  assert(!decodeSettings(unterminated, decoded));
  LegacySettingsRecord legacy{};
  std::copy_n(encoded.begin(),169,legacy.begin()); legacy[3]=1;
  const auto legacyCrc=crc32(legacy.data(),169);
  for (unsigned i=0;i<4;++i) legacy[169+i]=uint8_t(legacyCrc >> (8*i));
  decoded.sleepMinutes=15;
  assert(decodeSettings(legacy,decoded) && decoded.sleepMinutes==60);
  assert(sameNetwork(original,decoded) && decoded.utcOffsetMinutes==original.utcOffsetMinutes);
  for (size_t i=0;i<legacy.size();++i) {
    auto corrupt=legacy; corrupt[i]^=1;
    assert(!decodeSettings(corrupt,decoded));
  }
  for (uint16_t minutes : {0,15,30,60,120}) {
    Settings choice=original; choice.sleepMinutes=minutes;
    assert(decodeSettings(encodeSettings(choice),decoded) && decoded.sleepMinutes==minutes);
  }
  Settings invalidSleep=original; invalidSleep.sleepMinutes=45;
  assert(!decodeSettings(encodeSettings(invalidSleep),decoded));
  assert(validHost("ntp.aliyun.com") && validHost("192.168.1.1"));
  assert(!validHost("http://ntp.aliyun.com") && !validHost("a..b") && !validHost("a-.b"));
  assert(!validHost("-abc") && !validHost("a\r\nHeader"));
  assert(validPassword("") && validPassword("12345678") && validPassword(std::string(64, 'a')));
  assert(!validPassword("short") && !validPassword(std::string(64, 'z')) && !validPassword("1234567\n"));
  SettingsInput input{"Desk WiFi", "", "keep", "3", "480", "pool.ntp.org"};
  Settings next;
  assert(!applySettings(original, input, next));
  assert(std::strcmp(next.password, original.password) == 0 && next.brightness == 3);
  input.ssid = "Other";
  Settings unchanged = original;
  assert(applySettings(original, input, unchanged)); assert(sameNetwork(original, unchanged));
  input.passwordMode = "replace"; input.password = "new-password";
  assert(!applySettings(original, input, next)); assert(std::strcmp(next.password, "new-password") == 0);
  input.passwordMode = "open"; input.password.clear();
  assert(!applySettings(original, input, next) && next.password[0] == '\0');
  input.offset = "480minutes"; assert(applySettings(original, input, next));
  input.offset = "841"; assert(applySettings(original, input, next));
  input.offset = "480"; input.ssid = std::string(33, 'a'); assert(applySettings(original, input, next));
  assert(timezoneForOffset(480) == "UTC-8:00");
  assert(timezoneForOffset(-330) == "UTC+5:30");
  assert(jsonQuote("a\"b\\c\n") == "\"a\\\"b\\\\c\\u000a\"");
  const std::string body = "ssid=Desk+WiFi&password=&password_mode=keep&brightness=2&offset=480&ntp=ntp.aliyun.com";
  assert(parseSettingsForm(body, input)); assert(input.ssid == "Desk WiFi");
  assert(parseSettingsForm(body+"&sleep=120",input) && input.sleepMinutes=="120");
  assert(!applySettings(original,input,next) && next.sleepMinutes==120);
  input.sleepMinutes="45"; next=original;
  assert(applySettings(original,input,next) && next.sleepMinutes==60);
  assert(!parseSettingsForm(body+"&sleep=15&sleep=60",input));
  assert(!parseSettingsForm(body + "&ssid=duplicate", input));
  assert(!parseSettingsForm(body + "&unknown=value", input));
  assert(!parseSettingsForm(body + "&", input));
  assert(!parseSettingsForm("ssid=x", input));
  assert(!parseSettingsForm("ssid=%00&password=&password_mode=open&brightness=2&offset=480&ntp=host", input));
  assert(!parseSettingsForm(std::string(1025, 'a'), input));
  std::string value;
  assert(!decodeFormValue("abc%XX", value)); assert(!decodeFormValue("abc%", value));
  assert(decodeFormValue("%E6%A1%8C%E9%9D%A2%2B", value) && value == "桌面+");
  std::cout << "PASS: settings record integrity, transactional validation, password preservation, strict form parsing\n";
}
