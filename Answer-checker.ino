#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Preferences.h>

const char* AP_SSID     = "QuizESP32";
const char* AP_PASSWORD = "quiz1234";
const char* STA_SSID     = "YOUR_wifi_NAME";
const char* STA_PASSWORD = "YOUR_wifi_Password";

const char* SHEETS_URL    = "YOUR_GOOGLE_SCRIPT_URL";
const char* ANTHROPIC_KEY = "YOUR_ANTHROPIC_API_KEY";

const uint8_t MAX_QUESTIONS = 60;

const uint8_t PIN_BTN_A = 12;
const uint8_t PIN_BTN_B = 13;
const uint8_t PIN_BTN_C = 14;
const uint8_t PIN_BTN_D = 26;
const uint8_t PIN_RGB_R = 32;
const uint8_t PIN_RGB_G = 33;
const uint8_t PIN_RGB_B = 25;
const uint8_t PIN_BUZZER = 4;

const uint8_t SCREEN_WIDTH  = 128;
const uint8_t SCREEN_HEIGHT = 64;
const int8_t  OLED_RESET    = -1;
const uint8_t OLED_ADDR     = 0x3C;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
WebServer server(80);
Preferences prefs;

bool    showLiveScore = true;
bool    commonCathode = true;
bool    quizActive    = false;

char    answerKey[MAX_QUESTIONS];
uint8_t totalQuestions = 0;

uint8_t currentQuestion = 0;
uint8_t correctCount    = 0;
char    userAnswers[MAX_QUESTIONS];
bool    answered[MAX_QUESTIONS];

const uint32_t DEBOUNCE_MS  = 200;
const uint32_t HOLD_MS      = 2000;

// Button A state (dual function: short=answer A, long=menu)
uint32_t btnA_pressedAt  = 0;
bool     btnA_wasDown    = false;
bool     btnA_longFired  = false;
bool     btnA_shortReady = false;
uint32_t btnA_releasedAt = 0;

// Other buttons debounce
uint32_t lastBtnB = 0;
uint32_t lastBtnC = 0;
uint32_t lastBtnD = 0;

bool    inMenu   = false;
uint8_t menuItem = 0;
const uint8_t MENU_ITEMS = 4;

bool     showFeedback    = false;
bool     lastAnswerRight = false;
uint32_t feedbackUntil   = 0;

// ─── Buzzer ───────────────────────────────────────────────────────────────────

void beep(uint16_t hz, uint16_t ms) {
  ledcWriteTone(0, hz);
  delay(ms);
  ledcWriteTone(0, 0);
}

void beepCorrect() {
  beep(1046, 80); delay(40);
  beep(1318, 80); delay(40);
  beep(1568, 150);
}

void beepWrong() {
  beep(300, 200); delay(50);
  beep(200, 300);
}

void beepMenu()  { beep(800, 60); }
void beepClick() { beep(1200, 25); }

void beepStart() {
  beep(880, 100); delay(60);
  beep(1046, 200);
}

void beepDone() {
  beep(523, 100); delay(40);
  beep(659, 100); delay(40);
  beep(784, 100); delay(40);
  beep(1046, 300);
}

// ─── RGB LED ──────────────────────────────────────────────────────────────────

void setRGB(bool r, bool g, bool b) {
  if (commonCathode) {
    digitalWrite(PIN_RGB_R, r ? HIGH : LOW);
    digitalWrite(PIN_RGB_G, g ? HIGH : LOW);
    digitalWrite(PIN_RGB_B, b ? HIGH : LOW);
  } else {
    digitalWrite(PIN_RGB_R, r ? LOW : HIGH);
    digitalWrite(PIN_RGB_G, g ? LOW : HIGH);
    digitalWrite(PIN_RGB_B, b ? LOW : HIGH);
  }
}

void rgbOff()    { setRGB(0,0,0); }
void rgbRed()    { setRGB(1,0,0); }
void rgbGreen()  { setRGB(0,1,0); }
void rgbBlue()   { setRGB(0,0,1); }

// ─── Settings ─────────────────────────────────────────────────────────────────

void saveSettings() {
  prefs.begin("quiz", false);
  prefs.putBool("liveScore", showLiveScore);
  prefs.putBool("cathode",   commonCathode);
  prefs.end();
}

void loadSettings() {
  prefs.begin("quiz", true);
  showLiveScore = prefs.getBool("liveScore", true);
  commonCathode = prefs.getBool("cathode",   true);
  prefs.end();
}

// ─── Quiz logic ───────────────────────────────────────────────────────────────

void resetQuiz() {
  currentQuestion = 0;
  correctCount    = 0;
  quizActive      = false;
  showFeedback    = false;
  for (uint8_t i = 0; i < MAX_QUESTIONS; i++) {
    userAnswers[i] = 0;
    answered[i]    = false;
  }
  rgbOff();
}

void startQuiz() {
  if (totalQuestions == 0) return;
  resetQuiz();
  quizActive = true;
  rgbBlue();
  delay(300);
  rgbOff();
  beepStart();
}

void submitAnswer(char ans) {
  if (!quizActive) return;
  if (currentQuestion >= totalQuestions) return;
  if (showFeedback) return;

  userAnswers[currentQuestion] = ans;
  answered[currentQuestion]    = true;

  bool correct = (ans == answerKey[currentQuestion]);
  if (correct) correctCount++;

  lastAnswerRight = correct;
  showFeedback    = true;
  feedbackUntil   = millis() + 1200;

  if (correct) {
    rgbGreen();
    beepCorrect();
  } else {
    rgbRed();
    beepWrong();
  }

  currentQuestion++;

  if (currentQuestion >= totalQuestions) {
    quizActive = false;
    delay(1200);
    rgbOff();
    beepDone();
    logToSheets();
  }
}

// ─── Google Sheets ────────────────────────────────────────────────────────────

void logToSheets() {
  HTTPClient http;
  String url = String(SHEETS_URL);
  url += "?total="   + String(totalQuestions);
  url += "&correct=" + String(correctCount);
  url += "&wrong="   + String(totalQuestions - correctCount);
  url += "&score="   + String((correctCount * 100) / totalQuestions);

  String answers = "";
  for (uint8_t i = 0; i < totalQuestions; i++) {
    answers += String(userAnswers[i]);
    if (i < totalQuestions - 1) answers += ",";
  }
  url += "&answers=" + answers;

  http.begin(url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.GET();
  http.end();
  Serial.println("Logged to Sheets.");
}

// ─── OLED ─────────────────────────────────────────────────────────────────────

void drawMenu() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(30, 0);
  display.print("== MENU ==");
  display.drawFastHLine(0, 10, SCREEN_WIDTH, SSD1306_WHITE);

  const char* items[MENU_ITEMS] = {
    showLiveScore ? "Score: ON " : "Score: OFF",
    commonCathode ? "LED: Cathode" : "LED: Anode  ",
    "Reset Quiz",
    "Exit Menu"
  };

  for (uint8_t i = 0; i < MENU_ITEMS; i++) {
    if (i == menuItem) {
      display.fillRect(0, 14 + i * 12, SCREEN_WIDTH, 12, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(4, 16 + i * 12);
    display.print(items[i]);
  }

  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 56);
  display.print("B:Up C:Down D:Select");
  display.display();
}

void drawQuiz() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  if (totalQuestions == 0) {
    display.setTextSize(1);
    display.setCursor(10, 8);  display.print("No quiz loaded.");
    display.setCursor(0,  22); display.print("Open browser:");
    display.setCursor(0,  34); display.print("192.168.4.1");
    display.setCursor(0,  48); display.print("WiFi: QuizESP32");
    display.display();
    return;
  }

  if (!quizActive && currentQuestion >= totalQuestions && totalQuestions > 0) {
    display.setTextSize(1);
    display.setCursor(20, 4);
    display.print("QUIZ COMPLETE!");
    display.drawFastHLine(0, 14, SCREEN_WIDTH, SSD1306_WHITE);
    display.setTextSize(2);
    uint8_t pct = (correctCount * 100) / totalQuestions;
    char scoreBuf[8];
    snprintf(scoreBuf, sizeof(scoreBuf), "%d%%", pct);
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(scoreBuf, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((SCREEN_WIDTH - w) / 2, 20);
    display.print(scoreBuf);
    display.setTextSize(1);
    char detailBuf[22];
    snprintf(detailBuf, sizeof(detailBuf), "%d/%d correct", correctCount, totalQuestions);
    display.getTextBounds(detailBuf, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((SCREEN_WIDTH - w) / 2, 44);
    display.print(detailBuf);
    display.setCursor(10, 56);
    display.print("Saved to Sheets!");
    display.display();
    return;
  }

  if (!quizActive && totalQuestions > 0) {
    display.setTextSize(1);
    char qBuf[24];
    snprintf(qBuf, sizeof(qBuf), "%d questions loaded", totalQuestions);
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(qBuf, 0, 0, &x1, &y1, &w, &h);
    display.setCursor((SCREEN_WIDTH - w) / 2, 10);
    display.print(qBuf);
    display.setCursor(4, 30);
    display.print("Press A to start");
    display.setCursor(4, 44);
    display.print("Hold A for menu");
    display.display();
    return;
  }

  if (quizActive) {
    display.setTextSize(1);
    char qBuf[20];
    snprintf(qBuf, sizeof(qBuf), "Q %d / %d", currentQuestion + 1, totalQuestions);
    display.setCursor(0, 0);
    display.print(qBuf);

    if (showLiveScore) {
      char sBuf[12];
      snprintf(sBuf, sizeof(sBuf), "Score:%d", correctCount);
      int16_t x1, y1;
      uint16_t w, h;
      display.getTextBounds(sBuf, 0, 0, &x1, &y1, &w, &h);
      display.setCursor(SCREEN_WIDTH - w, 0);
      display.print(sBuf);
    }

    display.drawFastHLine(0, 10, SCREEN_WIDTH, SSD1306_WHITE);

    if (showFeedback && millis() < feedbackUntil) {
      display.setTextSize(2);
      int16_t x1, y1;
      uint16_t w, h;
      if (lastAnswerRight) {
        display.getTextBounds("CORRECT!", 0, 0, &x1, &y1, &w, &h);
        display.setCursor((SCREEN_WIDTH - w) / 2, 20);
        display.print("CORRECT!");
      } else {
        display.getTextBounds("WRONG!", 0, 0, &x1, &y1, &w, &h);
        display.setCursor((SCREEN_WIDTH - w) / 2, 14);
        display.print("WRONG!");
        display.setTextSize(1);
        char ansBuf[20];
        uint8_t prevQ = currentQuestion > 0 ? currentQuestion - 1 : 0;
        snprintf(ansBuf, sizeof(ansBuf), "Answer was: %c", answerKey[prevQ]);
        display.getTextBounds(ansBuf, 0, 0, &x1, &y1, &w, &h);
        display.setCursor((SCREEN_WIDTH - w) / 2, 38);
        display.print(ansBuf);
      }
    } else {
      showFeedback = false;
      rgbOff();
      display.setTextSize(1);
      display.setCursor(20, 18);
      display.print("Press your answer:");
      display.setTextSize(2);
      display.setCursor(4, 34);
      display.print("A  B  C  D");
    }

    uint8_t barW = (SCREEN_WIDTH * currentQuestion) / totalQuestions;
    display.fillRect(0, 58, barW, 6, SSD1306_WHITE);
    display.drawRect(0, 58, SCREEN_WIDTH, 6, SSD1306_WHITE);
    display.display();
  }
}

void drawDisplay() {
  if (inMenu) drawMenu();
  else        drawQuiz();
}

// ─── Menu ─────────────────────────────────────────────────────────────────────

void menuSelect() {
  switch (menuItem) {
    case 0:
      showLiveScore = !showLiveScore;
      saveSettings();
      beepClick();
      break;
    case 1:
      commonCathode = !commonCathode;
      saveSettings();
      beepClick();
      rgbGreen(); delay(300);
      rgbRed();   delay(300);
      rgbBlue();  delay(300);
      rgbOff();
      break;
    case 2:
      resetQuiz();
      totalQuestions = 0;
      inMenu = false;
      beepMenu();
      break;
    case 3:
      inMenu = false;
      beepMenu();
      break;
  }
}

// ─── Button logic ─────────────────────────────────────────────────────────────

bool btnPressed(uint8_t pin, uint32_t &last) {
  if (!digitalRead(pin) && millis() - last > DEBOUNCE_MS) {
    last = millis();
    return true;
  }
  return false;
}

void checkButtons() {
  uint32_t now = millis();

  // Button A: short press = Answer A, long press = Menu
  bool aDown = !digitalRead(PIN_BTN_A);

  if (aDown && !btnA_wasDown) {
    btnA_wasDown   = true;
    btnA_pressedAt = now;
    btnA_longFired = false;
  }

  if (aDown && btnA_wasDown && !btnA_longFired) {
    if (now - btnA_pressedAt >= HOLD_MS) {
      btnA_longFired = true;
      // Long press action
      if (inMenu) {
        inMenu = false;
      } else {
        inMenu   = true;
        menuItem = 0;
      }
      beepMenu();
    }
  }

  if (!aDown && btnA_wasDown) {
    btnA_wasDown = false;
    uint32_t held = now - btnA_pressedAt;
    if (!btnA_longFired && held > DEBOUNCE_MS && held < HOLD_MS) {
      // Short press action
      if (inMenu) {
        // In menu: A scrolls up
        menuItem = (menuItem == 0) ? MENU_ITEMS - 1 : menuItem - 1;
        beepClick();
      } else if (!quizActive && totalQuestions > 0 && currentQuestion == 0) {
        startQuiz();
      } else if (quizActive && !showFeedback) {
        submitAnswer('A');
      }
    }
    btnA_longFired = false;
  }

  // Menu mode: B=down, C=select, D=exit
  if (inMenu) {
    if (btnPressed(PIN_BTN_B, lastBtnB)) {
      menuItem = (menuItem + 1) % MENU_ITEMS;
      beepClick();
    }
    if (btnPressed(PIN_BTN_C, lastBtnC)) menuSelect();
    if (btnPressed(PIN_BTN_D, lastBtnD)) {
      inMenu = false;
      beepMenu();
    }
    return;
  }

  // Quiz mode
  if (quizActive && !showFeedback) {
    if (btnPressed(PIN_BTN_B, lastBtnB)) submitAnswer('B');
    if (btnPressed(PIN_BTN_C, lastBtnC)) submitAnswer('C');
    if (btnPressed(PIN_BTN_D, lastBtnD)) submitAnswer('D');
  }
}

// ─── Web page ─────────────────────────────────────────────────────────────────

String buildPage() {
  String p = "";
  p += "<!DOCTYPE html><html><head>";
  p += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  p += "<title>Quiz Setup</title>";
  p += "<style>";
  p += "* { box-sizing:border-box; margin:0; padding:0; }";
  p += "body { background:#0d0d0d; color:#f0e6d3; font-family:monospace;";
  p += "display:flex; flex-direction:column; align-items:center; padding:20px; }";
  p += "h1 { color:#ff6b35; letter-spacing:.3em; margin-bottom:6px; font-size:1.1rem; }";
  p += "h2 { color:#ff6b35; font-size:.85rem; letter-spacing:.2em; margin:20px 0 10px; }";
  p += ".card { background:#1a1a1a; border:1px solid #333; padding:16px;";
  p += "width:100%; max-width:500px; margin-bottom:16px; }";
  p += "label { font-size:.7rem; color:#888; letter-spacing:.15em; display:block; margin-bottom:6px; }";
  p += "textarea { background:#111; border:1px solid #444; color:#f0e6d3;";
  p += "font-family:monospace; padding:8px; width:100%; font-size:.85rem; height:120px; resize:vertical; }";
  p += "input[type=file] { background:#111; border:1px solid #444; color:#f0e6d3;";
  p += "font-family:monospace; padding:8px; width:100%; font-size:.8rem; }";
  p += ".btn { background:transparent; border:1px solid #ff6b35; color:#ff6b35;";
  p += "font-family:monospace; font-size:.8rem; padding:10px 20px;";
  p += "cursor:pointer; width:100%; margin-top:10px; }";
  p += ".btn:hover { background:#ff6b35; color:#0d0d0d; }";
  p += ".btn.green { border-color:#2ecc71; color:#2ecc71; }";
  p += ".btn.green:hover { background:#2ecc71; color:#0d0d0d; }";
  p += ".status { font-size:.7rem; color:#2ecc71; margin-top:8px; text-align:center; }";
  p += ".error  { font-size:.7rem; color:#e74c3c; margin-top:8px; text-align:center; }";
  p += ".info   { font-size:.65rem; color:#555; margin-top:6px; }";
  p += ".score-row { display:flex; gap:12px; margin-bottom:12px; }";
  p += ".score-box { flex:1; background:#1a1a1a; border:1px solid #333;";
  p += "padding:12px; text-align:center; }";
  p += ".score-box .num { font-size:2rem; font-weight:700; color:#ff6b35; }";
  p += ".score-box .lbl { font-size:.6rem; color:#666; letter-spacing:.15em; }";
  p += "hr { border:none; border-top:1px solid #222; margin:20px 0;";
  p += "width:100%; max-width:500px; }";
  p += "</style></head><body>";
  p += "<h1>QUIZ SETUP</h1>";

  if (!quizActive && totalQuestions > 0 && currentQuestion >= totalQuestions) {
    uint8_t pct = (correctCount * 100) / totalQuestions;
    p += "<div class='score-row'>";
    p += "<div class='score-box'><div class='num'>" + String(pct) + "%</div><div class='lbl'>SCORE</div></div>";
    p += "<div class='score-box'><div class='num'>" + String(correctCount) + "</div><div class='lbl'>CORRECT</div></div>";
    p += "<div class='score-box'><div class='num'>" + String(totalQuestions - correctCount) + "</div><div class='lbl'>WRONG</div></div>";
    p += "</div>";
    p += "<div class='status'>Saved to Google Sheets!</div><hr>";
  }

  p += "<div class='card'>";
  p += "<h2>OPTION 1: TYPE ANSWERS</h2>";
  p += "<label>ONE ANSWER PER LINE (A, B, C or D)</label>";
  p += "<textarea id='answers' placeholder='A\nB\nC\nD\nA\n...'></textarea>";
  p += "<p class='info'>Supports up to " + String(MAX_QUESTIONS) + " questions.</p>";
  p += "<button class='btn green' onclick='submitAnswers()'>LOAD ANSWERS</button>";
  p += "<div id='manualStatus'></div>";
  p += "</div>";

  p += "<div class='card'>";
  p += "<h2>OPTION 2: PHOTO OF ANSWER SHEET</h2>";
  p += "<label>TAKE OR UPLOAD PHOTO</label>";
  p += "<input type='file' id='photo' accept='image/*' capture='environment'>";
  p += "<p class='info'>AI will extract answers automatically from the photo.</p>";
  p += "<button class='btn' onclick='uploadPhoto()'>SCAN WITH AI</button>";
  p += "<div id='photoStatus'></div>";
  p += "</div>";

  if (totalQuestions > 0 && !quizActive) {
    p += "<div class='card'>";
    p += "<div style='text-align:center;color:#888;font-size:.7rem;margin-bottom:8px;'>";
    p += String(totalQuestions) + " QUESTIONS LOADED</div>";
    p += "<button class='btn green' onclick='startQuiz()'>START QUIZ</button>";
    p += "</div>";
  }

  p += "<script>";
  p += "function submitAnswers(){";
  p += "var raw=document.getElementById('answers').value.trim();";
  p += "if(!raw){document.getElementById('manualStatus').innerHTML=\"<div class='error'>Enter answers first.</div>\";return;}";
  p += "fetch('/setanswers',{method:'POST',headers:{'Content-Type':'text/plain'},body:raw})";
  p += ".then(r=>r.text()).then(t=>{";
  p += "document.getElementById('manualStatus').innerHTML=\"<div class='status'>\"+t+\"</div>\";";
  p += "setTimeout(()=>location.reload(),1500);";
  p += "}).catch(()=>document.getElementById('manualStatus').innerHTML=\"<div class='error'>Error.</div>\");";
  p += "}";

  p += "async function uploadPhoto(){";
  p += "var f=document.getElementById('photo').files[0];";
  p += "if(!f){document.getElementById('photoStatus').innerHTML=\"<div class='error'>Select a photo first.</div>\";return;}";
  p += "document.getElementById('photoStatus').innerHTML=\"<div class='status'>Scanning with AI...</div>\";";
  p += "var reader=new FileReader();";
  p += "reader.onload=function(e){";
  p += "var b64=e.target.result.split(',')[1];";
  p += "fetch('/scanphoto',{method:'POST',headers:{'Content-Type':'text/plain'},body:b64})";
  p += ".then(r=>r.text()).then(t=>{";
  p += "document.getElementById('photoStatus').innerHTML=\"<div class='status'>\"+t+\"</div>\";";
  p += "setTimeout(()=>location.reload(),2000);";
  p += "}).catch(()=>document.getElementById('photoStatus').innerHTML=\"<div class='error'>AI scan failed.</div>\");";
  p += "};";
  p += "reader.readAsDataURL(f);";
  p += "}";

  p += "function startQuiz(){";
  p += "fetch('/startquiz').then(()=>location.reload());";
  p += "}";
  p += "</script></body></html>";
  return p;
}

// ─── Web handlers ─────────────────────────────────────────────────────────────

void handleRoot() {
  server.send(200, "text/html", buildPage());
}

void handleSetAnswers() {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "No data");
    return;
  }
  String body = server.arg("plain");
  uint8_t count = 0;
  for (uint8_t i = 0; i < body.length() && count < MAX_QUESTIONS; i++) {
    char c = toupper(body[i]);
    if (c=='A'||c=='B'||c=='C'||c=='D') answerKey[count++] = c;
  }
  if (count == 0) {
    server.send(400, "text/plain", "No valid answers found.");
    return;
  }
  totalQuestions = count;
  resetQuiz();
  char buf[40];
  snprintf(buf, sizeof(buf), "Loaded %d answers! Press A to start.", count);
  server.send(200, "text/plain", buf);
}

void handleScanPhoto() {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "No image");
    return;
  }
  String b64 = server.arg("plain");

  HTTPClient http;
  http.begin("https://api.anthropic.com/v1/messages");
  http.addHeader("Content-Type",      "application/json");
  http.addHeader("x-api-key",         ANTHROPIC_KEY);
  http.addHeader("anthropic-version", "2023-06-01");

  String body = "{";
  body += "\"model\":\"claude-sonnet-4-20250514\",";
  body += "\"max_tokens\":1024,";
  body += "\"messages\":[{\"role\":\"user\",\"content\":[";
  body += "{\"type\":\"image\",\"source\":{\"type\":\"base64\",\"media_type\":\"image/jpeg\",\"data\":\"" + b64 + "\"}},";
  body += "{\"type\":\"text\",\"text\":\"This is an MCQ answer key. Extract ONLY the answers in order. Reply with ONLY the letters one per line, each being A B C or D. Nothing else.\"}";
  body += "]}]}";

  int code = http.POST(body);
  if (code != 200) {
    http.end();
    server.send(500, "text/plain", "AI error: " + String(code));
    return;
  }

  String resp = http.getString();
  http.end();

  int textStart = resp.indexOf("\"text\":\"");
  if (textStart < 0) {
    server.send(500, "text/plain", "Could not parse AI response");
    return;
  }
  textStart += 8;
  int textEnd = resp.indexOf("\"", textStart);
  String extracted = resp.substring(textStart, textEnd);
  extracted.replace("\\n", "\n");

  uint8_t count = 0;
  for (uint8_t i = 0; i < extracted.length() && count < MAX_QUESTIONS; i++) {
    char c = toupper(extracted[i]);
    if (c=='A'||c=='B'||c=='C'||c=='D') answerKey[count++] = c;
  }

  if (count == 0) {
    server.send(400, "text/plain", "AI could not find answers. Try a clearer photo.");
    return;
  }

  totalQuestions = count;
  resetQuiz();
  char buf[50];
  snprintf(buf, sizeof(buf), "AI found %d answers! Press A to start.", count);
  server.send(200, "text/plain", buf);
}

void handleStartQuiz() {
  startQuiz();
  server.send(200, "text/plain", "Quiz started!");
}

// ─── Setup ───────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  loadSettings();

  pinMode(PIN_BTN_A, INPUT_PULLUP);
  pinMode(PIN_BTN_B, INPUT_PULLUP);
  pinMode(PIN_BTN_C, INPUT_PULLUP);
  pinMode(PIN_BTN_D, INPUT_PULLUP);
  pinMode(PIN_RGB_R, OUTPUT);
  pinMode(PIN_RGB_G, OUTPUT);
  pinMode(PIN_RGB_B, OUTPUT);
  rgbOff();

  ledcSetup(0, 2000, 8);
  ledcAttachPin(PIN_BUZZER, 0);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED failed!");
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(22, 8);  display.print("MCQ QUIZ");
  display.setCursor(18, 22); display.print("ESP32 System");
  display.setCursor(4,  38); display.print("Starting hotspot...");
  display.display();

  rgbBlue();
  delay(1000);
  rgbOff();
  
WiFi.mode(WIFI_AP_STA);
WiFi.softAP(AP_SSID, AP_PASSWORD);
WiFi.begin(STA_SSID, STA_PASSWORD);
Serial.print("Connecting to hotspot");
uint8_t t = 0;
while (WiFi.status() != WL_CONNECTED && t++ < 20) {
  delay(500);
  Serial.print(".");
}
if (WiFi.status() == WL_CONNECTED) {
  Serial.println("\nInternet connected!");
} else {
  Serial.println("\nNo internet, Sheets wont work.");
}

  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);  display.print("WiFi: QuizESP32");
  display.setCursor(0, 12); display.print("Pass: quiz1234");
  display.setCursor(0, 26); display.print("Open browser:");
  display.setCursor(0, 38); display.print("192.168.4.1");
  display.setCursor(0, 52); display.print("Hold A = Menu");
  display.display();
  delay(3000);

  server.on("/",           HTTP_GET,  handleRoot);
  server.on("/setanswers", HTTP_POST, handleSetAnswers);
  server.on("/scanphoto",  HTTP_POST, handleScanPhoto);
  server.on("/startquiz",  HTTP_GET,  handleStartQuiz);
  server.begin();

  beepStart();
}

// ─── Loop ────────────────────────────────────────────────────────────────────

void loop() {
  server.handleClient();
  checkButtons();

  if (showFeedback && millis() > feedbackUntil) {
    showFeedback = false;
    rgbOff();
  }

  drawDisplay();
  delay(50);
}
