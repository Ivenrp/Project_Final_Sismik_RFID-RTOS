// SMART PARKING — ESP32 + FreeRTOS

#include <SPI.h>
#include <MFRC522.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// DEFINE PIN
#define RFID_SS_PIN     5
#define RFID_RST_PIN    27
#define RFID_SCK_PIN    18
#define RFID_MISO_PIN   19
#define RFID_MOSI_PIN   23
#define SERVO_PIN       13
#define SERVO_TUTUP     10
#define SERVO_BUKA      90
#define BUZZER_PIN      14
#define TRIG_PIN        32
#define ECHO_PIN        33
#define LDR_DO_PIN      4
// #define LDR_AO_PIN     34  // ADC1_CH6 — uncomment jika pakai AO

// VARIABLE
#define JARAK_DETEKSI       20
#define AMBANG_LEWAT        8
#define MIN_JARAK           2
#define MAX_JARAK           200
#define JUMLAH_SAMPEL       5
#define DEBOUNCE            3
#define INTERVAL_US         500
#define INTERVAL_LDR        500
#define INTERVAL_RFID       1000

// OBJEK
LiquidCrystal_I2C lcd(0x27, 16, 2);
MFRC522           rfid(RFID_SS_PIN, RFID_RST_PIN);
Servo             servoPalang;

// UID KARTU YANG DIIZINKAN
byte authorizedUID[4] = { 0x52, 0x89, 0x16, 0x05 };



// STATE GLOBAL
volatile bool mobilDetect = false;
volatile bool palangSibuk = false;
volatile bool rfidGranted = false;
volatile int  jarakLast   = -1;

enum GateState { CLOSED, CAR_DETECT, ACCESS, PASSING, COUNTDOWN };
GateState gateState = CLOSED;

// TASK HANDLE
TaskHandle_t hUltrasonik;
TaskHandle_t hRFID;
TaskHandle_t hLDR;

void lcdPrint(const char* line0, const char* line1)
{
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(line0);
  lcd.setCursor(0, 1); lcd.print(line1);
}

void lcdSiap()
{
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("SELAMAT DATANG");
  lcd.setCursor(0, 1); lcd.print(" ");
}

// HELPER: cocokkan UID
bool uidMatch(MFRC522::Uid *uid)
{
  if (uid->size != 4) return false;
  for (int i = 0; i < 4; i++)
    if (uid->uidByte[i] != authorizedUID[i]) return false;
  return true;
}

int bacaJarak()
{
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(4);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long durasi = pulseIn(ECHO_PIN, HIGH, 35000);
  if (durasi == 0) return -1;

  int d = (int)(durasi * 0.034f / 2.0f);
  if (d < MIN_JARAK || d > MAX_JARAK) return -1;
  return d;
}

// HELPER: median filter
int compareInt(const void *a, const void *b)
{
  return (*(int *)a - *(int *)b);
}

int bacaJarakMedian()
{
  int buf[JUMLAH_SAMPEL];
  int n = 0;
  for (int i = 0; i < JUMLAH_SAMPEL; i++)
  {
    int d = bacaJarak();
    if (d != -1) buf[n++] = d;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  if (n == 0) return -1;
  qsort(buf, n, sizeof(int), compareInt);
  return buf[n / 2];
}

// TASK: ULTRASONIC + STATE MACHINE GATE — Core 0, prioritas 2
void taskUltrasonic(void *pv)
{
  int  stableNear = 0;
  int  stableFar  = 0;
  unsigned long tPrint = 0;
  unsigned long closeAt = 0;

  while (true)
  {
    int  d     = bacaJarakMedian();
    bool dekat = (d != -1 && d <= JARAK_DETEKSI);

    jarakLast = d;

    if (dekat) { stableNear++; stableFar  = 0; }
    else        { stableFar++;  stableNear = 0; }

    // Serial print periodik
    if (millis() - tPrint >= INTERVAL_US)
    {
      tPrint = millis();
      if (d == -1)
        Serial.println("[Ultrasonik]   Jarak: --  cm | Status: TIMEOUT");
      else if (dekat)
        Serial.printf("[Ultrasonik]   Jarak: %3d cm | Status: ADA MOBIL <<<\n", d);
      else
        Serial.printf("[Ultrasonik]   Jarak: %3d cm | Status: KOSONG\n", d);
    }

    // STATE MACHINE GATE
    switch (gateState)
    {
      case CLOSED:
        if (stableNear >= DEBOUNCE)
        {
          mobilDetect = true;
          gateState = CAR_DETECT;
          lcdPrint("ADA MOBIL", "SCAN RFID");
          Serial.println("[GATE] CLOSED → CAR_DETECT");
        }
        break;

      case CAR_DETECT:
        if (rfidGranted)
        {
          rfidGranted = false;
          palangSibuk = true;
          gateState = ACCESS;
        }
        else if (stableFar >= DEBOUNCE && !palangSibuk)
        {
          mobilDetect = false;
          gateState = CLOSED;
          lcdSiap();
          Serial.println("[GATE] CAR_DETECT → CLOSED (mobil pergi)");
        }
        break;

      case ACCESS:
        Serial.println("[GATE] CAR_DETECT → ACCESS — BUKA PALANG");
        lcdPrint("AKSES DITERIMA", "PALANG BUKA");
        digitalWrite(BUZZER_PIN, HIGH);
        vTaskDelay(pdMS_TO_TICKS(300));
        digitalWrite(BUZZER_PIN, LOW);
        servoPalang.write(SERVO_BUKA);
        Serial.println("[SERVO] Posisi: BUKA 90°");
        gateState = PASSING;
        break;

      case PASSING:
        if (d != -1 && d <= AMBANG_LEWAT)
        {
          closeAt = millis() + 5000;
          gateState = COUNTDOWN;
          lcdPrint("MENUTUP PALANG", "TUTUP 5 dtk");
          Serial.println("[GATE] PASSING → COUNTDOWN (5dtk)");
        }
        break;

      case COUNTDOWN:
      {
        int sisa = (closeAt - millis()) / 1000 + 1;
        if (sisa < 0) sisa = 0;

        if (sisa == 0)
        {
          servoPalang.write(SERVO_TUTUP);
          Serial.println("[SERVO] Posisi: TUTUP 10°");
          lcdSiap();
          palangSibuk = false;
          mobilDetect = false;
          gateState = CLOSED;
          Serial.println("[GATE] COUNTDOWN → CLOSED");
        }
        else
        {
          lcd.clear();
          lcd.setCursor(0, 0); lcd.print("MENUTUP PALANG");
          lcd.setCursor(0, 1); lcd.print("TUTUP ");
          lcd.print(sisa);
          lcd.print(" dtk");
        }
        break;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// TASK: RFID — Core 1, prioritas 2
void taskRFID(void *pv)
{
  SPI.begin(RFID_SCK_PIN, RFID_MISO_PIN, RFID_MOSI_PIN, RFID_SS_PIN);
  rfid.PCD_Init();
  delay(50);

  byte ver = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  Serial.printf("[RFID] Firmware: 0x%02X %s\n", ver,
    (ver == 0x91 || ver == 0x92) ? "OK" : "!!! CEK WIRING SPI !!!");

  unsigned long tPrint = 0;

  while (true)
  {
    if (millis() - tPrint >= INTERVAL_RFID)
    {
      tPrint = millis();
      if (palangSibuk)
        Serial.println("[RFID] Status: GATE SIBUK (palang bergerak)");
      else if (!mobilDetect)
        Serial.println("[RFID] Status: TIDAK AKTIF (tunggu deteksi mobil)");
      else
        Serial.println("[RFID] Status: AKTIF — menunggu scan kartu...");
    }

    if (!mobilDetect || palangSibuk)
    {
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }

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

    Serial.print("[RFID] Kartu terdeteksi — UID: ");
    for (byte i = 0; i < rfid.uid.size; i++)
      Serial.printf("%02X ", rfid.uid.uidByte[i]);
    Serial.println();

    if (uidMatch(&rfid.uid))
    {
      rfidGranted = true;
      Serial.println("[RFID] >> AKSES DITERIMA");
    }
    else
    {
      Serial.println("[RFID] >> AKSES DITOLAK — kartu tidak dikenal");

      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("AKSES DITOLAK");
      lcd.setCursor(0, 1); lcd.print("KARTU INVALID");

      for (int i = 0; i < 3; i++)
      {
        digitalWrite(BUZZER_PIN, HIGH);
        vTaskDelay(pdMS_TO_TICKS(100));
        digitalWrite(BUZZER_PIN, LOW);
        vTaskDelay(pdMS_TO_TICKS(100));
      }

      lcdPrint("COBA LAGI", "SCAN RFID");
      Serial.println("[RFID] Menunggu scan ulang...");
    }

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// TASK: LDR — Core 0, prioritas 1
void taskLDR(void *pv)
{
  unsigned long tPrint = 0;

  while (true)
  {
    int state = digitalRead(LDR_DO_PIN);
    // int analog = analogRead(LDR_AO_PIN);  // uncomment jika pakai AO

    if (millis() - tPrint >= INTERVAL_LDR)
    {
      tPrint = millis();
      Serial.printf("[LDR]  DO: %-4s | Cahaya: %-6s\n",
        state ? "HIGH" : "LOW",
        state ? "GELAP" : "TERANG");
      // Serial.printf("[LDR]  DO: %-4s | AO: %4d\n",
      //   state ? "HIGH" : "LOW", analog);
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// SETUP
void setup()
{
  Serial.begin(115200);
  delay(200);

  Serial.println("\n============================================");
  Serial.println("       SMART PARKING — ESP32 FreeRTOS      ");
  Serial.println("============================================");

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  pinMode(LDR_DO_PIN, INPUT);
  // analogReadResolution(12);           // default 12-bit (0-4095)
  // pinMode(LDR_AO_PIN, INPUT);         // uncomment jika pakai AO

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcdSiap();

  servoPalang.attach(SERVO_PIN);
  servoPalang.write(SERVO_TUTUP);
  Serial.println("[SERVO] Init TUTUP 10°");
  delay(500);

  // TASK FreeRTOS
  xTaskCreatePinnedToCore(
    taskUltrasonic, "Ultrasonic",
    4096, NULL, 2, &hUltrasonik, 0);
  Serial.println("[RTOS] Task Ultrasonic → Core 0, prioritas 2");

  xTaskCreatePinnedToCore(
    taskRFID, "RFID",
    4096, NULL, 2, &hRFID, 1);
  Serial.println("[RTOS] Task RFID → Core 1, prioritas 2");

  xTaskCreatePinnedToCore(
    taskLDR, "LDR",
    2048, NULL, 1, &hLDR, 0);
  Serial.println("[RTOS] Task LDR → Core 0, prioritas 1");

  Serial.println("--------------------------------------------");
  Serial.println("  Sistem berjalan.");
  Serial.println("============================================\n");
}

void loop()
{
  vTaskDelay(pdMS_TO_TICKS(1000));
}
