#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>

// Wi-Fi
const char* ssid      = "NMSU_IAM3D";
const char* password  = "nmsuasme";

// AI-Thinker ESP32-CAM
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

WebServer server(80);

const framesize_t streamSizes[] = {FRAMESIZE_QQVGA,FRAMESIZE_QVGA,FRAMESIZE_VGA};

// steaming constants
const int streamSizeCount = sizeof(streamSizes) / sizeof(streamSizes[0]);
int currentStreamSize     = 1;
int maxStreamSize         = 1;
const int baseJpegQuality = 12;
const int maxJpegQuality  = 24;
const int jpegQualityStep = 3;
int currentJpegQuality    = baseJpegQuality;
uint32_t windowSendTimeMs = 0;
uint8_t windowFrameCount  = 0;

// adaptive streaming state
uint8_t slowWindows = 0;
uint8_t fastWindows = 0;

// wifi reconnect constants
unsigned long lastWifiReconnectAttempt      = 0;
const unsigned long wifiReconnectIntervalMs = 5000;

// function to update the stream resolution based on the average send time of the last 10 frames
void updateStreamResolution(uint32_t sendTimeMs) {
  windowSendTimeMs += sendTimeMs;
  windowFrameCount++;

  // Only update the stream resolution after 10 frames have been sent
  if (windowFrameCount < 10) {
    return;
  } // end if (windowFrameCount < 10)

  // Calculate the average send time for the last 10 frames and determine if the stream is too slow or too fast
  uint32_t averageSendTimeMs = windowSendTimeMs / windowFrameCount;
  windowSendTimeMs = 0;
  windowFrameCount = 0;

  if (averageSendTimeMs > 150) {
    slowWindows++;
    fastWindows = 0;
  } else if (averageSendTimeMs < 60) {
    fastWindows++;
    slowWindows = 0;
  } else {
    slowWindows = 0;
    fastWindows = 0;
  } // end if (averageSendTimeMs > 150)

  int targetSize    = currentStreamSize;
  int targetQuality = currentJpegQuality;

  // Adjust the stream resolution and JPEG quality based on the average send time
  if (slowWindows >= 2) {
    if (currentJpegQuality < maxJpegQuality) {
      targetQuality = min(currentJpegQuality + jpegQualityStep, maxJpegQuality);
    } else if (currentStreamSize > 0) {
      targetSize--;
    } // end if (currentJpegQuality < maxJpegQuality)
  } else if (fastWindows >= 4) {
    if (currentJpegQuality > baseJpegQuality) {
      targetQuality = max(currentJpegQuality - jpegQualityStep, baseJpegQuality);
    } else if (currentStreamSize < maxStreamSize) {
      targetSize++;
    } // end if (currentStreamSize < maxStreamSize)
  } // end if (slowWindows >= 2)


  // If the target size and quality are the same as the current size and quality, do nothing
  if (targetSize == currentStreamSize && targetQuality == currentJpegQuality) {
    return;
  } // end if (targetSize == currentStreamSize && targetQuality == currentJpegQuality)

  // Get the camera sensor and update the stream resolution and JPEG quality
  sensor_t *sensor = esp_camera_sensor_get();
  if (!sensor) {
    Serial.println("Failed to change stream resolution");
  } else if (targetQuality != currentJpegQuality && sensor->set_quality(sensor, targetQuality) == 0) {
    currentJpegQuality = targetQuality;
    Serial.printf("JPEG quality changed to %d (avg send %lu ms)\n",currentJpegQuality, (unsigned long)averageSendTimeMs);
  } else if (targetSize != currentStreamSize && sensor->set_framesize(sensor, streamSizes[targetSize]) == 0) {
    currentStreamSize = targetSize;
    Serial.printf("Stream resolution changed to index %d (avg send %lu ms)\n", currentStreamSize, (unsigned long)averageSendTimeMs);
  } else {
    Serial.println("Failed to update stream quality or resolution");
  }// end if (!sensor)

  slowWindows = 0;
  fastWindows = 0;
} // end void updateStreamResolution(uint32_t sendTimeMs)

// Video stream
void handleStream() {

  WiFiClient client = server.client();

  client.print(
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
    "Access-Control-Allow-Origin: *\r\n"
    "\r\n"
  );

  while (client.connected() && WiFi.status() == WL_CONNECTED) {

    camera_fb_t *fb = esp_camera_fb_get();

    if (!fb) {
      Serial.println("Camera capture failed");
      delay(100);
      continue;
    } // end if (!fb)

    unsigned long sendStarted = millis();

    client.print("--frame\r\n");
    client.print("Content-Type: image/jpeg\r\n");
    client.print("Content-Length: ");
    client.print(fb->len);
    client.print("\r\n\r\n");

    size_t frameLength = fb->len;
    size_t sent = client.write(fb->buf, frameLength);
    client.print("\r\n");

    esp_camera_fb_return(fb);

    if (sent != frameLength) {
      Serial.println("Incomplete frame write; ending stream");
      break;
    } // end if (sent != frameLength)

    updateStreamResolution(millis() - sendStarted);

    delay(30);
  } // end while (client.connected() && WiFi.status() == WL_CONNECTED)

  Serial.println("Stream client disconnected");
} // end void handleStream()

// Main webpage
void handleRoot() {

  String page =
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ESP32-CAM</title>"
    "</head>"
    "<body style='text-align:center;font-family:Arial;'>"
    "<h2>ESP32-CAM Live Video</h2>"
    "<img id='video' src='/stream' style='max-width:100%;'>"
    "<script>"
    "const video=document.getElementById('video');"
    "let retryTimer;"
    "video.onerror=()=>{clearTimeout(retryTimer);retryTimer=setTimeout(()=>{video.src='/stream?retry='+Date.now();},1000);};"
    "</script>"
    "</body>"
    "</html>";

  server.send(200, "text/html", page);
} // end void handleRoot()

// Setup
void setup() {

  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("=== ESP32-CAM STREAM ===");

  maxStreamSize = psramFound() ? streamSizeCount - 1 : 1;

  // ---------------------
  // Camera configuration
  camera_config_t config = {};

  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;

  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;

  config.pin_xclk  = XCLK_GPIO_NUM;
  config.pin_pclk  = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href  = HREF_GPIO_NUM;

  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;

  config.pin_pwdn  = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Allocate frame buffers for the largest adaptive resolution.
  config.frame_size   = streamSizes[maxStreamSize];
  config.jpeg_quality = baseJpegQuality;
  config.fb_count     = 1;

  // Start camera
  Serial.println("Starting camera...");

  esp_err_t err = esp_camera_init(&config);

  if (err != ESP_OK) {
    Serial.printf("Camera FAILED: 0x%X\n", err);
    return;
  } // end if (err != ESP_OK)

  Serial.println("Camera OK");

  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor && sensor->set_framesize(sensor, streamSizes[currentStreamSize]) == 0) {
    Serial.println("Starting stream at QVGA");
  } else {
    currentStreamSize = maxStreamSize;
    Serial.println("Could not set startup resolution; using maximum mode");
  } // end if (sensor && sensor->set_framesize(sensor, streamSizes[currentStreamSize]) == 0)

  Serial.printf("Adaptive stream range: QQVGA to %s\n",maxStreamSize == streamSizeCount - 1 ? "VGA" : "QVGA");

  // Connect Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  } // end while (WiFi.status() != WL_CONNECTED)

  Serial.println();
  Serial.println("WiFi connected");


  // Web server
  server.on("/", HTTP_GET, handleRoot);
  server.on("/stream", HTTP_GET, handleStream);

  server.begin();

  Serial.println();
  Serial.println("Web server started!");
  Serial.println();

  Serial.print("Open in browser: http://");
  Serial.println(WiFi.localIP());

  Serial.print("Direct stream:   http://");
  Serial.print(WiFi.localIP());
  Serial.println("/stream");
} // end void setup()

// Loop
void loop() {
  server.handleClient();

  if (WiFi.status() != WL_CONNECTED &&
      millis() - lastWifiReconnectAttempt >= wifiReconnectIntervalMs) {
    lastWifiReconnectAttempt = millis();
    Serial.println("WiFi disconnected; attempting reconnection");
    WiFi.reconnect();
  }

  delay(1);
} // end void loop()