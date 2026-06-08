#include <SPI.h>
#include <MFRC522.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ============================================================
// PIN DEFINITION
// ============================================================

// RFID MFRC522 — SPI
#define RFID_SS_PIN     5
#define RFID_RST_PIN    27
#define RFID_SCK_PIN    18
#define RFID_MISO_PIN   19
#define RFID_MOSI_PIN   23

// SERVO
#define SERVO_PIN       13
#define SERVO_TUTUP     10    // derajat posisi TUTUP
#define SERVO_BUKA      90    // derajat posisi BUKA

// BUZZER
#define BUZZER_PIN      14

// HY-SR05 ULTRASONIC
#define TRIG_PIN        32
#define ECHO_PIN        33    // ISR CHANGE

// MODUL LDR — hanya pin DO, LED-nya onboard modul
#define LDR_DO_PIN      4     // ISR CHANGE

// LCD I2C — SDA=GPIO21, SCL=GPIO22

// ============================================================
// THRESHOLD & INTERVAL PRINT
// ============================================================
#define DETECT_CM           20
#define MIN_CM              2
#define MAX_CM              200
#define SAMPLE_COUNT        5
#define DEBOUNCE_COUNT      3
#define US_PRINT_MS         500
#define LDR_PRINT_MS        500
#define RFID_PRINT_MS       1000

// ============================================================
// OBJEK
// ============================================================
LiquidCrystal_I2C lcd(0x27, 16, 2);
MFRC522           rfid(RFID_SS_PIN, RFID_RST_PIN);
Servo             gateServo;

// ============================================================
// UID KARTU YANG DIIZINKAN
// ============================================================
byte authorizedUID[4] = { 0x52, 0x89, 0x16, 0x05 };

// ============================================================
// ISR ULTRASONIC — ECHO CHANGE
// ============================================================
volatile unsigned long echoStart    = 0;
volatile unsigned long echoDuration = 0;
volatile bool          echoReady    = false;

void IRAM_ATTR echoISR()
{
  if (digitalRead(ECHO_PIN) == HIGH)
  {
    echoStart = micros();
  }
  else
  {
    if (echoStart > 0)
    {
      echoDuration = micros() - echoStart;
      echoReady    = true;
    }
  }
}

// ============================================================
// ISR LDR DO — CHANGE
// ============================================================
volatile bool ldrChanged = false;
volatile int  ldrState   = LOW;

void IRAM_ATTR ldrISR()
{
  ldrState   = digitalRead(LDR_DO_PIN);
  ldrChanged = true;
}

// ============================================================
// STATE GLOBAL ANTAR TASK
// ============================================================
volatile bool carDetected = false;
volatile bool gateBusy    = false;
volatile int  lastDist    = -1;

// ============================================================
// TASK HANDLE
// ============================================================
TaskHandle_t hUltrasonic;
TaskHandle_t hRFID;
TaskHandle_t hLDR;

// ============================================================
// HELPER: LCD standby
// ============================================================
void lcdReady()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SMART PARKING");
  lcd.setCursor(0, 1);
  lcd.print("SIAP...");
}

// ============================================================
// HELPER: cocokkan UID
// ============================================================
bool uidMatch(MFRC522::Uid *uid)
{
  if (uid->size != 4) return false;
  for (int i = 0; i < 4; i++)
    if (uid->uidByte[i] != authorizedUID[i]) return false;
  return true;
}

// ============================================================
// HELPER: baca jarak 1x via ISR (non-blocking)
// ============================================================
int readDistanceOnce()
{
  portDISABLE_INTERRUPTS();
  echoReady    = false;
  echoStart    = 0;
  echoDuration = 0;
  portENABLE_INTERRUPTS();

  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(4);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long t = millis();
  while (!echoReady && (millis() - t) < 35)
    vTaskDelay(1);

  if (!echoReady) return -1;

  int d = (int)(echoDuration * 0.034f / 2.0f);
  if (d < MIN_CM || d > MAX_CM) return -1;
  return d;
}

// ============================================================
// HELPER: median filter 5 sample
// ============================================================
int compareInt(const void *a, const void *b)
{
  return (*(int *)a - *(int *)b);
}

int readDistanceMedian()
{
  int buf[SAMPLE_COUNT];
  int n = 0;
  for (int i = 0; i < SAMPLE_COUNT; i++)
  {
    int d = readDistanceOnce();
    if (d != -1) buf[n++] = d;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  if (n == 0) return -1;
  qsort(buf, n, sizeof(int), compareInt);
  return buf[n / 2];
}

// ============================================================
// TASK: ULTRASONIC — Core 0, prioritas 2
// ============================================================
void ultrasonicTask(void *pv)
{
  int  stableNear = 0;
  int  stableFar  = 0;
  unsigned long tPrint = 0;

  while (true)
  {
    int  d    = readDistanceMedian();
    bool dekat = (d != -1 && d <= DETECT_CM);

    lastDist = d;

    if (dekat) { stableNear++; stableFar  = 0; }
    else        { stableFar++;  stableNear = 0; }

    // Mobil terdeteksi
    if (stableNear >= DEBOUNCE_COUNT && !carDetected)
    {
      carDetected = true;
      if (!gateBusy)
      {
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print("ADA MOBIL");
        lcd.setCursor(0, 1); lcd.print("SCAN RFID");
      }
    }

    // Area kosong
    if (stableFar >= DEBOUNCE_COUNT && carDetected && !gateBusy)
    {
      carDetected = false;
      lcdReady();
    }

    // Realtime print tiap US_PRINT_MS
    if (millis() - tPrint >= US_PRINT_MS)
    {
      tPrint = millis();
      if (d == -1)
        Serial.println("[Ultrasonik]   Jarak: --  cm | Status: TIMEOUT/OUT-OF-RANGE");
      else if (carDetected)
        Serial.printf( "[Ultrasonik]   Jarak: %3d cm | Status: ADA MOBIL <<<\n", d);
      else
        Serial.printf( "[Ultrasonik]   Jarak: %3d cm | Status: KOSONG\n", d);
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ============================================================
// TASK: RFID — Core 1, prioritas 2
//   - Aktif hanya saat carDetected && !gateBusy
//   - Polling murni tanpa IRQ
//   - SPI + PCD_Init() di dalam task ini (wajib Core 1)
//   - Buzzer = satu-satunya indikator akses
// ============================================================
void rfidTask(void *pv)
{
  // Init SPI + MFRC522 di Core 1
  SPI.begin(RFID_SCK_PIN, RFID_MISO_PIN, RFID_MOSI_PIN, RFID_SS_PIN);
  rfid.PCD_Init();
  delay(50);

  byte ver = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  Serial.printf("[RFID] Firmware: 0x%02X %s\n", ver,
    (ver == 0x91 || ver == 0x92) ? "OK" : "!!! CEK WIRING SPI !!!");

  unsigned long tPrint = 0;

  while (true)
  {
    // Realtime print status RFID tiap RFID_PRINT_MS
    if (millis() - tPrint >= RFID_PRINT_MS)
    {
      tPrint = millis();
      if (gateBusy)
        Serial.println("[RFID] Status: GATE SIBUK (palang bergerak)");
      else if (!carDetected)
        Serial.println("[RFID] Status: TIDAK AKTIF (tunggu deteksi mobil)");
      else
        Serial.println("[RFID] Status: AKTIF — menunggu scan kartu...");
    }

    // Tidak aktif jika tidak ada mobil atau gate sibuk
    if (!carDetected || gateBusy)
    {
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }

    // Polling kartu
    if (!rfid.PICC_IsNewCardPresent())
    {
      vTaskDelay(pdMS_TO_TICKS(150));
      continue;
    }

    if (!rfid.PICC_ReadCardSerial())
    {
      vTaskDelay(pdMS_TO_TICKS(150));
      continue;
    }

    // Cetak UID
    Serial.print("[RFID] Kartu terdeteksi — UID: ");
    for (byte i = 0; i < rfid.uid.size; i++)
      Serial.printf("%02X ", rfid.uid.uidByte[i]);
    Serial.println();

    // ===== AKSES DITERIMA =====
    if (uidMatch(&rfid.uid))
    {
      gateBusy = true;
      Serial.println("[RFID] >> AKSES DITERIMA — palang BUKA 90°");

      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("AKSES DITERIMA");
      lcd.setCursor(0, 1); lcd.print("PALANG BUKA");

      // Buzzer 1x pendek
      digitalWrite(BUZZER_PIN, HIGH);
      vTaskDelay(pdMS_TO_TICKS(300));
      digitalWrite(BUZZER_PIN, LOW);

      // Buka palang 90°
      gateServo.write(SERVO_BUKA);
      Serial.println("[SERVO] Posisi: BUKA 90°");

      vTaskDelay(pdMS_TO_TICKS(5000));

      Serial.println("[SERVO] Menutup palang...");
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("MENUTUP PALANG");
      lcd.setCursor(0, 1); lcd.print("HARAP TUNGGU");

      // Tutup palang 10°
      gateServo.write(SERVO_TUTUP);
      Serial.println("[SERVO] Posisi: TUTUP 10°");

      vTaskDelay(pdMS_TO_TICKS(1500));

      lcdReady();
      gateBusy = false;
      Serial.println("[RFID] Siklus selesai — sistem kembali standby");
    }
    // ===== AKSES DITOLAK =====
    else
    {
      Serial.println("[RFID] >> AKSES DITOLAK — kartu tidak dikenal");

      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("AKSES DITOLAK");
      lcd.setCursor(0, 1); lcd.print("KARTU INVALID");

      // Buzzer 3x cepat
      for (int i = 0; i < 3; i++)
      {
        digitalWrite(BUZZER_PIN, HIGH);
        vTaskDelay(pdMS_TO_TICKS(100));
        digitalWrite(BUZZER_PIN, LOW);
        vTaskDelay(pdMS_TO_TICKS(100));
      }

      vTaskDelay(pdMS_TO_TICKS(2000));

      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("COBA LAGI");
      lcd.setCursor(0, 1); lcd.print("SCAN RFID");

      Serial.println("[RFID] Menunggu scan ulang...");
    }

    // Reset MFRC522
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// ============================================================
// TASK: LDR — Core 0, prioritas 1
//   - ISR ldrISR() sudah simpan state DO
//   - Task hanya apply ke Serial Monitor
//   - LED = onboard modul LDR, ikut DO secara hardware
//     (tidak perlu digitalWrite dari sini)
// ============================================================
void ldrTask(void *pv)
{
  int curState = digitalRead(LDR_DO_PIN);

  Serial.printf("[LDR]  Init — DO: %-4s | Cahaya: %-6s\n",
    curState ? "HIGH" : "LOW",
    curState ? "GELAP" : "TERANG");

  unsigned long tPrint = 0;

  while (true)
  {
    // Terapkan perubahan dari ISR
    if (ldrChanged)
    {
      ldrChanged = false;
      curState   = ldrState;

      Serial.printf("[LDR]  PERUBAHAN — DO: %-4s | Cahaya: %-6s\n",
        curState ? "HIGH" : "LOW",
        curState ? "GELAP" : "TERANG");
    }

    // Print periodik
    if (millis() - tPrint >= LDR_PRINT_MS)
    {
      tPrint = millis();
      Serial.printf("[LDR]  DO: %-4s | Cahaya: %-6s\n",
        curState ? "HIGH" : "LOW",
        curState ? "GELAP" : "TERANG");
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// ============================================================
// SETUP
// ============================================================
void setup()
{
  Serial.begin(115200);
  delay(200);

  Serial.println("\n============================================");
  Serial.println("       SMART PARKING — ESP32 FreeRTOS      ");
  Serial.println("============================================");
  Serial.println("  [Ultrasonik] = Ultrasonic HY-SR05");
  Serial.println("  [RFID] = MFRC522");
  Serial.println("  [LDR]  = Sensor Cahaya DO (LED onboard)");
  Serial.println("  [SERVO]= Palang Parkir");
  Serial.println("--------------------------------------------");

  // Pin ultrasonic
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  // Pin buzzer
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // Pin LDR DO — input saja, LED-nya onboard modul
  pinMode(LDR_DO_PIN, INPUT);

  // LCD
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcdReady();

  // Servo — posisi awal TUTUP 10°
  gateServo.attach(SERVO_PIN);
  gateServo.write(SERVO_TUTUP);
  Serial.println("[SERVO] Init TUTUP 10°");
  delay(500);

  // CATATAN: SPI + rfid.PCD_Init() dilakukan di dalam rfidTask (Core 1)

  // ISR ECHO ultrasonic
  attachInterrupt(digitalPinToInterrupt(ECHO_PIN), echoISR, CHANGE);
  Serial.println("[ISR]  ECHO terpasang (GPIO " + String(ECHO_PIN) + ")");

  // ISR LDR DO
  attachInterrupt(digitalPinToInterrupt(LDR_DO_PIN), ldrISR, CHANGE);
  Serial.println("[ISR]  LDR DO terpasang (GPIO " + String(LDR_DO_PIN) + ")");

  // ============================================================
  // BUAT TASK FreeRTOS
  // ============================================================
  xTaskCreatePinnedToCore(
    ultrasonicTask, "Ultrasonic",
    4096, NULL, 2, &hUltrasonic, 0);
  Serial.println("[RTOS] Task Ultrasonic -> Core 0, prioritas 2");

  xTaskCreatePinnedToCore(
    rfidTask, "RFID",
    4096, NULL, 2, &hRFID, 1);
  Serial.println("[RTOS] Task RFID       -> Core 1, prioritas 2");

  xTaskCreatePinnedToCore(
    ldrTask, "LDR",
    2048, NULL, 1, &hLDR, 0);
  Serial.println("[RTOS] Task LDR        -> Core 0, prioritas 1");

  Serial.println("--------------------------------------------");
  Serial.println("  Sistem berjalan. Monitor di bawah ini:");
  Serial.println("============================================\n");
}

void loop()
{
  vTaskDelay(pdMS_TO_TICKS(1000));
}
