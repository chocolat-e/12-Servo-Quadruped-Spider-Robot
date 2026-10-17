#include <WiFi.h>
#include <ESP32Servo.h>
#include <math.h>

// ============================================================
// SpiderBot - WiFi controller
// ESP32 creates its own WiFi access point.
// PC connects to:
//   SSID: SpiderBot
//   Password: spiderbot123
//   IP: 192.168.4.1
//
// TCP port: 5000
//
// Commands are plain text terminated by '\n'.
// Example:
//   FORWARD
//   STOP
//   ROTATE_LEFT
// ============================================================

const char* AP_SSID = "spiderspider";
const char* AP_PASSWORD = "spiderspider";
WiFiServer server(5000);
WiFiClient client;

// ==================== SERVO CONFIG ====================

const int servoPins[12] = {
  25, 33, 32,  // FL
  18, 19, 21,  // FR
  13, 27, 26,  // BL
  15,  2,  4   // BR
};

const int direction[12] = {
   1,  1,  1,
  -1, -1, -1,
   1,  1,  1,
  -1, -1, -1
};

Servo servos[12];

// ==================== LEG DIMENSIONS ====================

const float L1 = 35.0f;
const float L2 = 70.0f;
const float L3 = 100.0f;
const float RAD2DEG = 57.2957795f;

// ==================== HOME ====================

const float X_BASE[4] = {
  -75.0f, -75.0f,
   75.0f,  75.0f
};

const float Y_BASE[4] = {
   65.0f, -65.0f,
   65.0f, -65.0f
};

const float Z_STAND = -105.0f;
const float Z_CROUCH = -72.0f;

// ==================== GAIT ====================

const float STEP_LENGTH = 45.0f;
const float TURN_STEP = 38.0f;
const float STEP_HEIGHT = 55.0f;

const unsigned long GAIT_PERIOD = 1300;
const unsigned long UPDATE_INTERVAL = 20;

const float STANCE_RATIO = 0.52f;

// FL -> BR -> FR -> BL
const float PHASE_OFFSET[4] = {
  0.00f,
  0.50f,
  0.75f,
  0.25f
};

float motionForward = 0.0f;
float motionTurn = 0.0f;
bool walking = false;

unsigned long gaitStart = 0;
unsigned long lastUpdate = 0;

enum PoseMode {
  POSE_STAND,
  POSE_CROUCH
};

PoseMode poseMode = POSE_STAND;

// ==================== IK ====================

bool calculateAngles(
  int leg, float x, float y, float z,
  int &s0, int &s1, int &s2
) {
  float horiz = sqrtf(x * x + y * y) - L1;

  if (horiz <= 1.0f) return false;

  float d = sqrtf(horiz * horiz + z * z);

  if (d > L2 + L3 - 0.5f ||
      d < fabsf(L2 - L3) + 0.5f) {
    return false;
  }

  float c1 = constrain(
    (L2 * L2 + d * d - L3 * L3) /
    (2.0f * L2 * d),
    -1.0f, 1.0f
  );

  float c2 = constrain(
    (L2 * L2 + L3 * L3 - d * d) /
    (2.0f * L2 * L3),
    -1.0f, 1.0f
  );

  float a0 = 90.0f
    + (x >= 0.0f ? 1.0f : -1.0f)
    * atan2f(fabsf(x), fabsf(y)) * RAD2DEG;

  float a1 = 90.0f
    + (atan2f(z, horiz) + acosf(c1)) * RAD2DEG;

  float a2 = 180.0f - acosf(c2) * RAD2DEG;

  int b = leg * 3;

  s0 = constrain(
    (int)roundf(90.0f + direction[b] * (a0 - 90.0f)),
    0, 180
  );

  s1 = constrain(
    (int)roundf(90.0f + direction[b + 1] * (a1 - 90.0f)),
    0, 180
  );

  s2 = constrain(
    (int)roundf(90.0f + direction[b + 2] * (a2 - 90.0f)),
    0, 180
  );

  return true;
}

bool setLeg(int leg, float x, float y, float z) {
  int s0, s1, s2;

  if (!calculateAngles(leg, x, y, z, s0, s1, s2)) {
    return false;
  }

  int b = leg * 3;

  servos[b].write(s0);
  servos[b + 1].write(s1);
  servos[b + 2].write(s2);

  return true;
}

// ==================== POSTURE ====================

void setAllLegsAtZ(float z) {
  for (int leg = 0; leg < 4; leg++) {
    setLeg(leg, X_BASE[leg], Y_BASE[leg], z);
  }
}

void smoothPosture(float fromZ, float toZ, unsigned long durationMs) {
  unsigned long start = millis();

  while (millis() - start < durationMs) {
    float t = (float)(millis() - start) / (float)durationMs;
    t = constrain(t, 0.0f, 1.0f);

    float s = t * t * (3.0f - 2.0f * t);
    float z = fromZ + (toZ - fromZ) * s;

    for (int leg = 0; leg < 4; leg++) {
      setLeg(leg, X_BASE[leg], Y_BASE[leg], z);
    }

    delay(20);
  }

  setAllLegsAtZ(toZ);
}

void standUp() {
  walking = false;

  if (poseMode == POSE_STAND) {
    setAllLegsAtZ(Z_STAND);
    return;
  }

  smoothPosture(Z_CROUCH, Z_STAND, 2200);
  poseMode = POSE_STAND;

  if (client && client.connected()) client.println("STAND_DONE");
}

void sitDown() {
  walking = false;
  smoothPosture(Z_STAND, Z_CROUCH, 1200);
  poseMode = POSE_CROUCH;

  if (client && client.connected()) client.println("SIT_DONE");
}

void home() {
  walking = false;

  if (poseMode == POSE_CROUCH) {
    smoothPosture(Z_CROUCH, Z_STAND, 1800);
  } else {
    smoothPosture(Z_STAND, Z_STAND, 400);
  }

  poseMode = POSE_STAND;

  if (client && client.connected()) client.println("HOME_DONE");
}

// ==================== GAIT ====================

float smoothStep(float t) {
  return t * t * (3.0f - 2.0f * t);
}

void moveLeg(int leg, float cycle) {
  float phase = cycle + PHASE_OFFSET[leg];

  if (phase >= 1.0f) {
    phase -= floorf(phase);
  }

  float xOffset = 0.0f;
  float yOffset = 0.0f;
  float zOffset = 0.0f;

  float side = (Y_BASE[leg] > 0.0f) ? 1.0f : -1.0f;
  float frontRear = (X_BASE[leg] > 0.0f) ? 1.0f : -1.0f;

  float turnX = -motionTurn * side * TURN_STEP * 0.75f;
  float turnY =  motionTurn * frontRear * TURN_STEP * 0.55f;

  float travelX = motionForward * STEP_LENGTH;

  if (phase < STANCE_RATIO) {
    float t = phase / STANCE_RATIO;
    float k = -0.5f + t;

    xOffset = k * (travelX + turnX);
    yOffset = k * turnY;
  } else {
    float t = (phase - STANCE_RATIO) / (1.0f - STANCE_RATIO);
    float s = smoothStep(t);
    float k = 0.5f - s;

    xOffset = k * (travelX + turnX);
    yOffset = k * turnY;
    zOffset = STEP_HEIGHT * sinf(PI * t);
  }

  setLeg(
    leg,
    X_BASE[leg] + xOffset,
    Y_BASE[leg] + yOffset,
    Z_STAND + zOffset
  );
}

void startWalking() {
  if (poseMode != POSE_STAND) {
    standUp();
  }

  if (!walking) {
    gaitStart = millis();
  }

  walking = true;
}

void stopWalking() {
  walking = false;
  motionForward = 0.0f;
  motionTurn = 0.0f;

  for (int leg = 0; leg < 4; leg++) {
    setLeg(leg, X_BASE[leg], Y_BASE[leg], Z_STAND);
  }

  if (client && client.connected()) client.println("STOPPED");
}

void updateGait() {
  if (!walking) return;

  unsigned long now = millis();

  if (now - lastUpdate < UPDATE_INTERVAL) return;
  lastUpdate = now;

  float cycle = fmodf(
    (float)(now - gaitStart) / (float)GAIT_PERIOD,
    1.0f
  );

  for (int leg = 0; leg < 4; leg++) {
    moveLeg(leg, cycle);
  }
}

// ==================== WAVE ====================

void wave(bool leftSide) {
  walking = false;

  float x0[4], y0[4], z0[4];
  float x1[4], y1[4], z1[4];

  for (int leg = 0; leg < 4; leg++) {
    x0[leg] = X_BASE[leg];
    y0[leg] = Y_BASE[leg];
    z0[leg] = Z_STAND;

    x1[leg] = X_BASE[leg];
    y1[leg] = Y_BASE[leg];
    z1[leg] = Z_STAND;
  }

  // waveLeg: Chân trước để vẫy (0: FL, 1: FR)
  int waveLeg = leftSide ? 0 : 1;
  // supportLeg: Chân sau chéo góc để chống (3: BR, 2: BL)
  int supportLeg = leftSide ? 3 : 2; 

  float lean = leftSide ? -15.0f : 15.0f;

  // Nghiêng trọng tâm
  for (int leg = 0; leg < 4; leg++) {
    y1[leg] += lean;
  }

  // 1. Nhấc chân vẫy: cao hơn và vươn ra xa hơn
  z1[waveLeg] = -40.0f; 
  x1[waveLeg] -= 25.0f; // Vươn ra trước nhiều hơn
  y1[waveLeg] += leftSide ? 15.0f : -15.0f; // Dang ra ngoài

  // 2. Chân trụ phía sau chéo góc: lùi về sau để giữ thăng bằng
  x1[supportLeg] += 20.0f; 

  unsigned long start = millis();

  // Chuyển từ tư thế đứng sang tư thế chuẩn bị vẫy
  while (millis() - start < 800) {
    float t = (float)(millis() - start) / 800.0f;
    float s = t * t * (3.0f - 2.0f * t);

    for (int leg = 0; leg < 4; leg++) {
      setLeg(
        leg,
        x0[leg] + (x1[leg] - x0[leg]) * s,
        y0[leg] + (y1[leg] - y0[leg]) * s,
        z0[leg] + (z1[leg] - z0[leg]) * s
      );
    }
    delay(20);
  }

  int b = waveLeg * 3;

  // Thực hiện vẫy (Xoay servo Coxa của chân vẫy)
  for (int n = 0; n < 5; n++) {
    servos[b].write(
      constrain(90 + (n % 2 == 0 ? 25 : -10), 20, 160)
    );
    delay(200); // Tăng delay một chút cho tự nhiên
  }

  start = millis();

  // Trở về tư thế đứng ban đầu
  while (millis() - start < 800) {
    float t = (float)(millis() - start) / 800.0f;
    float s = t * t * (3.0f - 2.0f * t);

    for (int leg = 0; leg < 4; leg++) {
      setLeg(
        leg,
        x1[leg] + (x0[leg] - x1[leg]) * s,
        y1[leg] + (y0[leg] - y1[leg]) * s,
        z1[leg] + (z0[leg] - z1[leg]) * s
      );
    }
    delay(20);
  }

  poseMode = POSE_STAND;

  if (client && client.connected()) client.println("WAVE_DONE");
}

// ==================== COMMANDS ====================

void command(String cmd) {
  cmd.trim();
  cmd.toUpperCase();

  if (cmd == "PING") {
    if (client && client.connected()) client.println("PONG");
    return;
  }

  if (cmd == "FORWARD") {
    motionForward = 1.0f;
    motionTurn = 0.0f;
    startWalking();
    client.println("OK FORWARD");
    return;
  }

  if (cmd == "BACK") {
    motionForward = -1.0f;
    motionTurn = 0.0f;
    startWalking();
    client.println("OK BACK");
    return;
  }

  if (cmd == "LEFT") {
    motionForward = 0.75f;
    motionTurn = 1.0f;
    startWalking();
    client.println("OK LEFT");
    return;
  }

  if (cmd == "RIGHT") {
    motionForward = 0.75f;
    motionTurn = -1.0f;
    startWalking();
    client.println("OK RIGHT");
    return;
  }

  if (cmd == "ROTATE_LEFT") {
    motionForward = 0.0f;
    motionTurn = 1.0f;
    startWalking();
    client.println("OK ROTATE_LEFT");
    return;
  }

  if (cmd == "ROTATE_RIGHT") {
    motionForward = 0.0f;
    motionTurn = -1.0f;
    startWalking();
    client.println("OK ROTATE_RIGHT");
    return;
  }

  if (cmd == "STOP") {
    stopWalking();
    return;
  }

  if (cmd == "HOME") {
    home();
    return;
  }

  if (cmd == "STAND") {
    standUp();
    return;
  }

  if (cmd == "SIT") {
    sitDown();
    return;
  }

  if (cmd == "WAVE_LEFT") {
    wave(true);
    return;
  }

  if (cmd == "WAVE_RIGHT") {
    wave(false);
    return;
  }

  client.println("ERR UNKNOWN_COMMAND");
}

// ==================== STARTUP ====================

void startupSequence() {
  // Start low and distribute load across all four legs.
  // Do NOT raise one servo/leg at a time.

  setAllLegsAtZ(Z_CROUCH);
  delay(700);

  smoothPosture(Z_CROUCH, Z_STAND, 2500);
  poseMode = POSE_STAND;
}

// ==================== SETUP ====================

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 4; i++) {
    ESP32PWM::allocateTimer(i);
  }

  for (int i = 0; i < 12; i++) {
    servos[i].setPeriodHertz(50);
    servos[i].attach(servoPins[i], 500, 2400);
  }

  delay(500);

  // Create private WiFi network.
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  Serial.println();
  Serial.println("================================");
  Serial.println("SpiderBot WiFi");
  Serial.print("SSID: ");
  Serial.println(AP_SSID);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.println("TCP: 5000");
  Serial.println("================================");

  server.begin();

  startupSequence();

  Serial.println("READY");
}

// ==================== LOOP ====================

void loop() {
  // Accept/reaccept PC connection.
  if (!client || !client.connected()) {
    WiFiClient newClient = server.available();

    if (newClient) {
      if (client) client.stop();

      client = newClient;
      client.setTimeout(20);

      client.println("SPIDERBOT_READY");
      client.println("PONG");
    }
  }

  if (client && client.connected() && client.available()) {
    String cmd = client.readStringUntil('\n');
    command(cmd);
  }

  updateGait();
}
