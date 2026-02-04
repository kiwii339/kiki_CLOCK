#include <WiFi.h>
#include <Wire.h>
#include <WiFiManager.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFiUdp.h>
#include <NTPClient.h>

/* ===== 字体文件（有文件则取消注释，无则保留注释） ===== */
#include "font/Orbitron_Medium_16.h"

/* ===== OLED配置 - 0.91寸 128*32 I2C 核心修正 ===== */
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 32
#define OLED_ADDR     0x3C  // 显示异常改0x3D 
#define OLED_RESET    -1    // 无复位引脚设为-1

/* ===== I2C引脚：换回ESP32硬件I2C，通讯稳定不丢包 ===== */
#define SDA_PIN  8  // ESP32硬件I2C SDA，必接这个引脚
#define SCL_PIN  9  // ESP32硬件I2C SCL，必接这个引脚

/* ===== LED引脚配置 - 保留你的定义，无冲突 ===== */
#define LED_GREEN   5    
#define LED_YELLOW  6    
#define LED_RED     7    

/* ===== 全局声明colonShow（你原代码的写法，保留） ===== */
bool colonShow = true;               // ★ 全局声明：所有函数都能直接用，无需传参
static unsigned long colonTimer = 0; // ★ 冒号闪烁定时器：全局静态，防止loop重置

/* ===== NTP时间配置 ===== */
WiFiUDP ntpUDP;
int gmtOffset = 8;                
NTPClient timeClient(ntpUDP, "pool.ntp.org", gmtOffset * 3600, 60000);

/* ===== LED闪烁状态 ===== */
unsigned long ledTimer = 0;
int ledStep = 0;
#define LED_FLASH_MS    200   
#define LED_OFF_SHORT   600   
#define LED_OFF_LONG    1000  
#define LED_PAUSE_MS    1000  

/* ===== WiFiManager配网配置 ===== */
WiFiManager wm;
WiFiManagerParameter param_gmt(
  "gmt",
  "GMT Offset (8=Beijing, -5=NY)",  
  "8",  
  4
);

const char* portal_html = R"(
<style>
body{background:#000;color:#0f0;font-family:Arial}
h1{color:#00ffd5;text-align:center}
p{text-align:center}
input,button{font-size:16px;padding:8px;width:100%}
button{background:#00ffd5;border:none;color:#000;font-weight:bold}
</style>
<h1>VOYAGER CLOCK</h1>
<p>WiFi Setup & Timezone</p>
)";

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

/* ===================================================== */
void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  /* ===== 引脚初始化 ===== */
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_RED, LOW);

  /* ===== 硬件I2C初始化 ===== */
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000); // 适配128×32 OLED标准频率
  Serial.printf("I2C init on SDA:%d SCL:%d\n", SDA_PIN, SCL_PIN);

  /* ===== OLED初始化 ===== */
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("OLED init failed! Check pins/addr/3.3V"));
    while (1) {
      digitalWrite(LED_RED, !digitalRead(LED_RED));
      delay(500);
    }
  }
  display.setRotation(0);    // ★ 核心：适配竖装显示
  display.setTextColor(SSD1306_WHITE);
  display.cp437(true);
  Serial.println(F("OLED init success!"));

  /* ===== 开机欢迎页 ===== */
  display.clearDisplay();
  display.setFont();
  display.setCursor(0, 10);
  display.print(F("KiKi"));
  display.setCursor(0, 25);
  display.print(F("CLOCK"));
  display.display();
  delay(1500);

  /* ===== 配网提示页 ===== */
  display.clearDisplay();
  display.setFont();
  display.setCursor(0, 10);
  display.print(F("WIFI SETUP"));
  display.setCursor(0, 25);
  display.print(F("AP:kiki-Clock"));
  display.display();
  Serial.println(F("Start WiFi AP: kiki-Clock"));

  /* ===== WiFiManager配网配置 ===== */
  wm.setCustomHeadElement(portal_html);
  wm.addParameter(&param_gmt);
  wm.setConfigPortalTimeout(180);
  wm.setAPStaticIPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  wm.setSaveConfigCallback(saveConfigCallback);

  /* ===== 自动配网 ===== */
  bool wifiConnected = wm.autoConnect("kiki-Clock");
  if (!wifiConnected) {
    Serial.println(F("WiFi timeout! Restart..."));
    display.clearDisplay();
    display.setFont();
    display.setCursor(0, 10);
    display.print(F("WIFI TIMEOUT"));
    display.setCursor(0, 25);
    display.print(F("RESTART..."));
    display.display();
    for(int i=0; i<3; i++){
      digitalWrite(LED_YELLOW, HIGH);
      delay(500);
      digitalWrite(LED_YELLOW, LOW);
      delay(500);
    }
    ESP.restart();
  }

  /* ===== WiFi成功提示 ===== */
  display.clearDisplay();
  display.setFont();
  display.setCursor(0, 10);
  display.print(F("WIFI OK"));
  display.setCursor(0, 25);
  display.print(WiFi.localIP());
  display.display();
  Serial.printf("WiFi OK! IP: %s\n", WiFi.localIP().toString().c_str());
  delay(2000);

  /* ===== NTP初始化 ===== */
  gmtOffset = atoi(param_gmt.getValue());
  timeClient.setTimeOffset(gmtOffset * 3600);
  timeClient.begin();
  Serial.printf("NTP init! GMT+%d\n", gmtOffset);

  /* ===== 同步NTP时间 ===== */
  display.clearDisplay();
  display.setFont();
  display.setCursor(0, 15);
  display.print(F("SYNC TIME..."));
  display.display();
  while(!timeClient.update()){
    timeClient.forceUpdate();
    delay(500);
  }
  Serial.println(F("NTP sync success!"));
}

/* ===================================================== */
void loop() {
  static unsigned long lastNtpUpdate = 0;
  unsigned long now = millis();

  // NTP60秒更新一次
  if (now - lastNtpUpdate >= 60000) {
    timeClient.update();
    lastNtpUpdate = now;
  }

  // ★ 冒号闪烁核心逻辑：修改全局变量colonShow（500ms切换一次）
  if (now - colonTimer >= 500) {
    colonShow = !colonShow;  // 取反：显示→隐藏，隐藏→显示
    colonTimer = now;        // 重置定时器
  }

  
  drawClockVertical();
  // LED闪烁
  ledPattern();
}

/* =====================================================
   核心：128×32竖屏（setRotation(1)）时钟绘制 - 修复所有排版问题
   ===================================================== */
void drawClockVertical() {
  display.clearDisplay();
  // 二选一：用自定义字体 或 默认字体+3倍放大
  // 方案1：自定义Orbitron字体（有文件则取消注释，无则用方案2）
  //display.setFont(&Orbitron_Medium_16);
  // 方案2：默认字体（无文件依赖，稳定）
  display.setFont();
  display.setTextSize(4);
  
  display.setTextColor(SSD1306_WHITE);

  // 获取时分并补0
  int h = timeClient.getHours();
  int m = timeClient.getMinutes();
  char hh[3], mm[3];
  sprintf(hh, "%02d", h);
  sprintf(mm, "%02d", m);

  /* ★ 竖屏（setRotation(1)）精准坐标：x(0~127)，y(0~32)，时分上下排列+冒号居中 */
  int x = 8;  // 水平居中偏移，避免贴边
  // 1. 打印小时：竖屏上半部分
  display.setCursor(x, 4);
  display.print(hh);
  // 2. 打印冒号：小时和分钟中间（随colonShow闪烁）
  //display.setCursor(x + 12, 65); // 冒号居中分隔
  if (colonShow) display.print(F(":")); else display.print(F(" "));
  // 3. 打印分钟：竖屏下半部分
  //display.setCursor(x, 90);
  display.print(mm);

  // 刷新显示（所有内容生效）
  display.display();
}

/* =====================================================
   LED闪烁函数 - 保留你的原逻辑，无修改
   ===================================================== */
void ledPattern() {
  unsigned long now = millis();

  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_RED, LOW);

  switch (ledStep) {
    case 0:
      digitalWrite(LED_YELLOW, HIGH);
      digitalWrite(LED_RED, HIGH);
      if (now - ledTimer >= LED_FLASH_MS) {
        ledTimer = now;
        ledStep = 1;
      }
      break;
    case 1:
      if (now - ledTimer >= LED_OFF_SHORT) {
        ledTimer = now;
        ledStep = 2;
      }
      break;
    case 2:
      digitalWrite(LED_YELLOW, HIGH);
      digitalWrite(LED_RED, HIGH);
      if (now - ledTimer >= LED_FLASH_MS) {
        ledTimer = now;
        ledStep = 3;
      }
      break;
    case 3:
      if (now - ledTimer >= LED_OFF_LONG) {
        ledTimer = now;
        ledStep = 4;
      }
      break;
    case 4:
      digitalWrite(LED_GREEN, HIGH);
      if (now - ledTimer >= LED_FLASH_MS) {
        ledTimer = now;
        ledStep = 5;
      }
      break;
    case 5:
      if (now - ledTimer >= LED_PAUSE_MS) {
        ledTimer = now;
        ledStep = 0;
      }
      break;
  }
}

/* =====================================================
   配网保存回调函数
   ===================================================== */
void saveConfigCallback () {
  Serial.println(F("Config saved!"));
  gmtOffset = atoi(param_gmt.getValue());
  Serial.printf("New GMT Offset: %d\n", gmtOffset);
}