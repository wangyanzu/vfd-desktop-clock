// Sanitized historical reference only; excluded from the PlatformIO build.
#include "m202md28aa.h"
#include "UTF8ToGB2312.h"
#include "WiFi.h"
#include <MQTT.h>
#include <Adafruit_AHTX0.h>
#include <math.h>
#include "time.h"

// board led
#define LED4 12
#define LED5 13
// uart1
#define TX1 0
#define RX1 1
// button
#define BTN 8

// aht10
Adafruit_AHTX0 aht;

// net-clock
const char* ntpServer = "ntp.aliyun.com";
const long  gmtOffset_sec = 8*3600;
const int   daylightOffset_sec = 0;

int heartCount = 0;

void sendVFDCmd(const char *cmd) {
  for (int i = 0; i < 5; i++) {
    if (cmd[i] == 0x0) {
      continue;
    }
    Serial1.write(cmd[i]);
  }
}

void setVFDCursor(char x, char y){
  const char VFD_Cursor_set[5]={0x1F,0x24,0x0,x,y};
  sendVFDCmd(&VFD_Cursor_set[0]);
}

// mqtt
WiFiClient net;
MQTTClient client;

// millis
unsigned long currentMillis = 0;
unsigned long timeMillis = 0;
unsigned long tempMillis = 0;
unsigned long lastMessageMillis = 0;
// layout
char layoutTimeCenter = 1;
// send blink
char secondBlink = 1;

void connect() {
  Serial.println("connecting...");
  while (!client.connect("YOUR_MQTT_CLIENT_ID", "", "")) {
    Serial.print(".");
    delay(1000);
  }
  Serial.println("connected!");
  client.subscribe("YOUR_MQTT_TOPIC");
  // client.unsubscribe("YOUR_MQTT_TOPIC");
}

void playLoveHeart(){
  sendVFDCmd(&VFD_2_byte_character_type_Korea[0]);
  for (int x = 1; x <20; x++){
    setVFDCursor(x,0x2);
    Serial1.write(0xa2);
    Serial1.write(0xbe);
    delay(300 / x);
    sendVFDCmd(&VFD_Line_Clear[0]);
  }
  heartCount++;
  setVFDCursor(18,1);
  // displayHeart
  Serial1.write(0xa2);
  Serial1.write(0xbe);
  Serial1.printf("%02d", heartCount % 100);
  sendVFDCmd(&VFD_2_byte_character_type_Chinese[0]);
  //printLocalTime();
}

void messageReceived(String &topic, String &payload) {
  // 标记消息接收时间
  lastMessageMillis = millis();
  digitalWrite(LED4, HIGH);
  Serial.println("incoming: " + topic + " - " + payload);
  setVFDCursor(0x1,0x2);
  sendVFDCmd(&VFD_Line_Clear[0]);
  delay(100);
  digitalWrite(LED4, LOW);
  if (payload == "love-heart"){
    playLoveHeart();
    return;
  }
  Serial1.println(GB.get(payload));
}

void smart_config() {
  // 进入配网模式
  sendVFDCmd(&VFD_Display_Clear[0]);
  Serial1.print(GB.get("微信搜索 一键配网 为设备设置网络."));
  WiFi.mode(WIFI_AP_STA);
  WiFi.beginSmartConfig();
  // 等待配网
  while (!WiFi.smartConfigDone()) {
    delay(500);
  }
  // 清屏更新消息
  sendVFDCmd(&VFD_Display_Clear[0]);
  Serial1.print(GB.get("接收到配网信息，正在连接."));
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial1.print(".");
  }
  WiFi.setAutoReconnect(true);
}

bool connect_wifi() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin();
  sendVFDCmd(&VFD_Display_Clear[0]);
  Serial1.print(GB.get("正在连接网络."));
  int retry_count = 0;
  while (retry_count < 10) {
    if (WiFi.status() == WL_CONNECTED) {
      return true;
    }
    retry_count++;
    delay(500)
  }
  return false;
}

void printLocalTime(){
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  // 没有接收过消息，则居中显示
  if (heartCount == 0){
    setVFDCursor(0x3,0x1);
  }else{
    // 清空原位置显示
    if (layoutTimeCenter == 1){
      setVFDCursor(16,0x1);
      Serial1.print("  ");
      layoutTimeCenter = 0;
    }
    setVFDCursor(0x1,0x1);
  }
  if (secondBlink == 0){
    secondBlink = 1;
    Serial1.print(&timeinfo, "%m-%d %H %M %a");
  }else{
    secondBlink = 0;
    Serial1.print(&timeinfo, "%m-%d %H:%M %a");
  }
}

void printTempHumidity(){
    sensors_event_t humidity, temp;
    aht.getEvent(&humidity, &temp);
    setVFDCursor(0x1,0x2);
    //sendVFDCmd(&VFD_Line_Clear[0]);
    Serial1.print("    "); Serial1.printf("%2.0f",temp.temperature); Serial1.print(GB.get("°C"));
    Serial1.print("   "); Serial1.printf("%2.0f",humidity.relative_humidity); Serial1.print("%rH    ");
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  // 初始化
  Serial1.begin(9600, SERIAL_8N1, RX1, TX1);
  // 等屏幕初始化
  if (! aht.begin()) {
    Serial.println("Could not find AHT? Check wiring");
    while (1) delay(10);
  }
  Serial.println("AHT10 or AHT20 found");
  delay(500);
  // sendVFDCmd(&VFD_Initialize_display[0]);
  // sendVFDCmd(&VFD_Blink_Screen[0]);
  // 降低亮度
  sendVFDCmd(&VFD_Brightness_level_setting_50[0]);
  // 显示全角
  sendVFDCmd(&VFD_2_byte_character_Chinese[0]);
  // 选择中文字符
  sendVFDCmd(&VFD_2_byte_character_type_Chinese[0]);
  // 复写模式
  sendVFDCmd(&VFD_Over_write_mode[0]);

  // 持续按钮3秒，则进入配网模式
  pinMode(BTN,INPUT_PULLUP);
  while(digitalRead(BTN) == LOW){
    if (millis() > 3000){
        Serial.print("press btn");
        smart_config();
    }
    delay(1000);
  }
  // 长时间无法联网，也进入配网模式
  if (!connect_wifi()) {
    smart_config();
  }
  sendVFDCmd(&VFD_Display_Clear[0]);
  Serial1.println(GB.get("已连接."));
  Serial1.print("IP: ");
  Serial1.println(WiFi.localIP());
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  sendVFDCmd(&VFD_Display_Clear[0]);
  // 初始化mqtt连接
  Serial1.println(GB.get("正在获取网络时间."));
  Serial1.println(GB.get("正在连接MQTT服务器."));
  client.begin("bemfa.com", 9501, net);
  client.onMessage(messageReceived);
  connect();
  sendVFDCmd(&VFD_Display_Clear[0]);
  // 初始化显示
  printLocalTime();
  printTempHumidity();
}

void loop() {
  // 循环指示灯
  digitalWrite(LED5, HIGH);
  currentMillis = millis();
  if (currentMillis - timeMillis >= 500){
    timeMillis = currentMillis;
    // 更新时间
    printLocalTime();
  }
  // 显示的消息保持60分钟
  if ((currentMillis - lastMessageMillis >= 60 * 60 * 1000 || lastMessageMillis == 0) &&  currentMillis - tempMillis >= 3000){
    tempMillis = currentMillis;
    // 更新温湿度
    printTempHumidity();
  }
  client.loop();

  // 判断重连
  if (!client.connected()) {
    setVFDCursor(0x1,0x2);
    Serial1.println(GB.get("正在重连MQTT服务器."));
    connect();
    sendVFDCmd(&VFD_Line_Clear[0]);
  }
  delay(50);
  digitalWrite(LED5, LOW);
  delay(100);
}
