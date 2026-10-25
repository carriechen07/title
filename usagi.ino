// ESP32-S3 4-digit 7-segment countdown timer
// Encoder sets time, start/stop buttons, melody on finish, auto-sleep when idle.

// ---------- Pins ----------
const int SEG_PINS[7]   = {15, 14, 4, 5, 6, 16, 17};  // A, B, C, D, E, F, G
const int DIGIT_PINS[4] = {13, 10, 11, 12};           // left to right
const int START_PIN  = 47;
const int STOP_PIN   = 21;
const int CLK_PIN    = 18;
const int DT_PIN     = 7;
const int BUZZER_PIN = 8;
const int FLASHBANG  = 9;
const int IR         = 48;
const int buzzer     = 1;

// ---------- Settings ----------
const unsigned long STEP     = 60;      // seconds per encoder click (1 for testing)
const unsigned long MAX_TIME = 99 * 60; // max settable time in seconds
const unsigned long SLEEP_MS = 30000;   // idle time before shutdown

// ---------- Segment patterns (bits: A B C D E F G) ----------
const byte DIGITS[10] = {
  0b1111110,  // 0
  0b0110000,  // 1
  0b1101101,  // 2
  0b1111001,  // 3
  0b0110011,  // 4
  0b1011011,  // 5
  0b1011111,  // 6
  0b1110000,  // 7
  0b1111111,  // 8
  0b1111011   // 9
};
const byte LETTER_S = 0b1011011;
const byte LETTER_E = 0b1001111;
const byte LETTER_T = 0b0001111;

// ---------- Melody (quarter note = 154 BPM) ----------
#define NOTE_C5 523
#define NOTE_D5 587
#define NOTE_E5 659
#define NOTE_F5 698
#define NOTE_G5 784
#define NOTE_A5 880
#define REST    0

const int TEMPO_BPM     = 154;
const int WHOLE_NOTE_MS = (60000 / TEMPO_BPM) * 4;

const int melody[] = {
  NOTE_G5, REST, NOTE_G5, NOTE_E5, NOTE_E5, NOTE_A5, NOTE_G5, REST,
  NOTE_F5, NOTE_F5, NOTE_F5, NOTE_F5, NOTE_E5, NOTE_D5, NOTE_C5, REST,
  NOTE_E5, NOTE_D5
};
// 2 = half, 4 = quarter, 8 = eighth
const int noteDurations[] = {
  4, 4, 4, 4, 4, 8, 8, 2,
  8, 8, 8, 4, 8, 8, 8, 4,
  4, 4
};
const int NOTE_COUNT = sizeof(melody) / sizeof(melody[0]);

// Flash 0000 (call repeatedly)
void showFlash() {
  if ((millis() / 500) % 2 == 0) {
    for (int i = 0; i < 4; i++) showDigit(i, DIGITS[0]);
  } else {
    allDigitsOff();
    delay(4);
  }
}

void playMelody() {
  for (int i = 0; i < NOTE_COUNT; i++) {
    int duration = WHOLE_NOTE_MS / noteDurations[i];
    if (melody[i] != REST) tone(BUZZER_PIN, melody[i], duration * 0.9);

    unsigned long endTime = millis() + duration;
    while (millis() < endTime) showFlash();   // keep flashing during the note

    noTone(BUZZER_PIN);
  }
}

// ---------- Display ----------
void allDigitsOff() {
  for (int i = 0; i < 4; i++) digitalWrite(DIGIT_PINS[i], HIGH);
}

// Show one pattern on one digit position (0 = leftmost) for 1 ms
void showDigit(int pos, byte pattern) {
  allDigitsOff();                                   // off first to avoid ghosting
  for (int s = 0; s < 7; s++) {
    digitalWrite(SEG_PINS[s], (pattern >> (6 - s)) & 1);
  }
  digitalWrite(DIGIT_PINS[pos], LOW);               // common-cathode: LOW enables
  delay(1);
}

void showSet() {                                    // "SEt" on digits 2-4
  showDigit(1, LETTER_S);
  showDigit(2, LETTER_E);
  showDigit(3, LETTER_T);
}

void showTime(unsigned long totalSeconds) {         // MM:SS
  int minutes = totalSeconds / 60;
  int seconds = totalSeconds % 60;
  showDigit(0, DIGITS[minutes / 10]);
  showDigit(1, DIGITS[minutes % 10]);
  showDigit(2, DIGITS[seconds / 10]);
  showDigit(3, DIGITS[seconds % 10]);
}

// ---------- State ----------
int startState = HIGH;
int irread = HIGH;
int stopState = HIGH;
int buttonPrev = HIGH;       // HIGH = stopped/paused, LOW = running
int clkState;
int lastClkState;

unsigned long userTime = 0;       // goal time (s)
unsigned long timeClick = 0;      // when start was pressed (s)
unsigned long timeRunning = 0;    // elapsed since start (s)
unsigned long lastActivity = 0;
bool running = false;
bool completed = false;
bool asleep = false;

void resetTimer() {
  userTime = 0;
  timeRunning = 0;
  running = false;
  completed = false;
  buttonPrev = HIGH;
}

void setup() {
  Serial.begin(9600);
  for (int i = 0; i < 7; i++) pinMode(SEG_PINS[i], OUTPUT);
  for (int i = 0; i < 4; i++) pinMode(DIGIT_PINS[i], OUTPUT);
  pinMode(CLK_PIN, INPUT);
  pinMode(DT_PIN, INPUT);
  pinMode(START_PIN, INPUT_PULLUP);
  pinMode(STOP_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(FLASHBANG, OUTPUT);
  digitalWrite(FLASHBANG, HIGH);
  lastClkState = digitalRead(CLK_PIN);
  pinMode(IR, INPUT);
  pinMode(buzzer, OUTPUT);
  
  digitalWrite(buzzer, LOW);
}


void loop() {
  clkState   = digitalRead(CLK_PIN);
  startState = digitalRead(START_PIN);
  stopState  = digitalRead(STOP_PIN);
  irread     = digitalRead(IR);

  bool bothPressed = (startState == LOW && stopState == LOW);

  // Any input counts as activity and wakes everything up
  if (clkState != lastClkState || startState == LOW || stopState == LOW) {
    lastActivity = millis();
    if (asleep) {
      asleep = false;
      digitalWrite(FLASHBANG, HIGH);
    }
  }

  // Idle while stopped (not running, not finished) -> shut down
  if (!asleep && !running && !completed && millis() - lastActivity > SLEEP_MS) {
    asleep = true;
    digitalWrite(FLASHBANG, LOW);
  }

  if (asleep) {
    allDigitsOff();
    lastClkState = clkState;
    return;
  }

  // Both buttons after finishing: reset and play melody
  if (completed && bothPressed) {
    resetTimer();
  }

  // Encoder adjusts time, only while stopped
  if (clkState != lastClkState && clkState == HIGH && !running) {
    if (digitalRead(DT_PIN) != clkState) {          // clockwise: add
      if (userTime < MAX_TIME) userTime += STEP;
    } else {                                        // counter-clockwise: subtract
      if (userTime >= STEP) userTime -= STEP;
    }
  }
  lastClkState = clkState;

  // Start
  if (startState == LOW && stopState == HIGH && buttonPrev == HIGH && userTime > timeRunning) {
    buttonPrev = LOW;
    running = true;
    timeClick = millis() / 1000;
  }

  // Pause
  if (stopState == LOW && startState == HIGH && buttonPrev == LOW && userTime > timeRunning) {
    buttonPrev = HIGH;
    running = false;
    userTime -= timeRunning;
    timeRunning = 0;
  }

  // Both pressed: reset
  if (bothPressed) resetTimer();

  if (running) {timeRunning = millis() / 1000 - timeClick;
    if (irread == HIGH && !completed){
      digitalWrite(buzzer, HIGH);
    } else {
      digitalWrite(buzzer, LOW);
    }
  }

  if (!running){
    digitalWrite(buzzer, LOW);
  }

  // Draw the right thing
  if (!running && userTime == 0) {
    showSet();
  } else if (!running) {
    showTime(userTime);                             // paused
  } else if (userTime > timeRunning) {
    showTime(userTime - timeRunning);               // counting down
  }   else {        
    digitalWrite(buzzer, LOW);                                  // time's up
    if (!completed) {
      completed = true;
      playMelody();
    }
    showFlash();
  }
}