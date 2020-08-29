#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "ESP32_MailClient.h"
#include "esp_camera.h"
#include "SPI.h"
#include <FS.h>
#include <SPIFFS.h>

#define CAMERA_MODEL_AI_THINKER

#define GMAIL_SMTP_SEVER "smtp.gmail.com"
#define GMAIL_SMTP_USERNAME ""
#define GMAIL_SMTP_PORT 465
#define GMAIL_SMTP_PASSWORD ""
#define FILE_PHOTO "/photo.jpg"

#include "camera_pins.h"

const char* host = "esp32";
const char* ssid = "";
const char* password = "";

const int buttonPin = 13;
int wifiConnected = 0;

void takePhotoAndSend() {
  sendEmail(false, "test", "TAKING PHOTO");
  camera_fb_t * fb = NULL;
  bool ok = 0;
  fb = esp_camera_fb_get();
  if (!fb) {
    sendEmail(false, "!fb", "CAPTURE IMAGE FAILED");
    return;
  }
  File file = SPIFFS.open(FILE_PHOTO, FILE_WRITE);
  if (!file) {
    sendEmail(false, "!file", "CAPTURE IMAGE FAILED");
    file.close();
    return;
  }
  file.write(fb->buf, fb->len);
  file.close();
  esp_camera_fb_return(fb);
  ok = checkPhoto(SPIFFS);
  if (!ok) {
    sendEmail(false, "!ok", "CAPTURE IMAGE FAILED");
    return;
  }
  sendEmail(true,"PHOTO TAKEN", "CAPTURE IMAGE SUCCESS");
  return;
}

bool checkPhoto( fs::FS &fs ) {
  File f_pic = fs.open( FILE_PHOTO );
  unsigned int pic_sz = f_pic.size();
  return ( pic_sz > 100 );
}

void sendEmail(bool withPhoto, char *body, char *subject) { 
  SMTPData data;
  data.setLogin(GMAIL_SMTP_SEVER, GMAIL_SMTP_PORT, GMAIL_SMTP_USERNAME, GMAIL_SMTP_PASSWORD);
  data.setSender("ESP32", GMAIL_SMTP_USERNAME);
  data.setSubject(subject);
  data.setMessage(body, false);
  data.addRecipient("");
  (if (withPhoto) {
    data.setFileStorageType(MailClientStorageType::SPIFFS);
  }
  MailClient.sendMail(data);
}

void setup() {
  Serial.begin(115200);
  Serial.println("Booting");
  pinMode(buttonPin, INPUT);

  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

  ArduinoOTA.begin();

  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  char IP[] = "xxx.xxx.xxx.xxx";          // buffer
  IPAddress ip = WiFi.localIP();
  ip.toString().toCharArray(IP, 16);
  sendEmail(false, IP, "CONNECTED");

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  //init with high specs to pre-allocate larger buffers
  if(psramFound()){
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 1;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 1;
    config.fb_count = 1;
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t * s = esp_camera_sensor_get();
  //initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);//flip it back
    s->set_brightness(s, 1);//up the blightness just a bit
    s->set_saturation(s, -2);//lower the saturation
  }
  //drop down frame size for higher initial frame rate
  s->set_framesize(s, FRAMESIZE_QVGA);

#if defined(CAMERA_MODEL_M5STACK_WIDE)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

  wifiConnected = 1;
  SPIFFS.begin(true);
}

int prevState = 0;
int currentState = 0;
unsigned long previousTime = millis();
void readPin() {
  int pinValue = digitalRead(buttonPin);
  if (pinValue == LOW) {
    Serial.println("value is LOW");
    currentState = 1;
  }
  if (wifiConnected == 1 && prevState == 0 && currentState == 1) {
    prevState = 1;
    takePhotoAndSend();
  }
  if (pinValue == HIGH) {
    Serial.println("value is HIGH");
    prevState = 0;
    currentState = 0;
  }
}

void loop() {
  unsigned long currentTime = millis();
  if (currentTime - previousTime >= 500) {
    readPin();
    previousTime = currentTime;
  }
  ArduinoOTA.handle();
}
