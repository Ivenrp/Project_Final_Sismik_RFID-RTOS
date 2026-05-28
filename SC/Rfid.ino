// ============================================================
// SISTEM PARKIR OTOMATIS - ESP32 + FreeRTOS
// Mata Kuliah  : Sistem Mikrokontroler (TK244004)
// Universitas  : Universitas Jenderal Soedirman
// ── UPGRADE: FreeRTOS Multitasking + OLED SSD1306 ──────────
// ============================================================

#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

// ── PIN ESP32 ─────────────────────────────────────
#define PIN_RFID_SS    5
#define PIN_RFID_RST   27
#define PIN_SERVO      13
#define PIN_TRIG       26
#define PIN_ECHO       25
#define PIN_BUZZER     12
#define PIN_LED_HIJAU  14
#define PIN_LED_MERAH  32
#define PIN_BUTTON     4
#define PIN_LED_KUNING 33
#define PIN_LDR        34
#define PIN_LED_LAMPU  15

// ── SERVO via LEDC (tanpa library Servo) ─────────
#define SERVO_LEDC_FREQ 50
#define SERVO_LEDC_BIT  16

// ── SERVO via LEDC (Core v3 API) ─────────────────
void servoAttach() {
  ledcAttach(PIN_SERVO, SERVO_LEDC_FREQ, SERVO_LEDC_BIT);
}

void servoWrite(int angle) {
  uint32_t duty = map(angle, 0, 180, 1638, 8192);
  ledcWrite(PIN_SERVO, duty);
}
// ── OLED SSD1306 128x64 I2C ───────────────────────
#define OLED_WIDTH  128
#define OLED_HEIGHT 64
#define OLED_ADDR   0x3C
#define PIN_SDA     21
#define PIN_SCL     22

// ── KONFIGURASI ──────────────────────────────────
#define KAPASITAS_SLOT  3
#define JARAK_BATAS     120
#define JARAK_DEKAT     3
#define JEDA_TUTUP      5000
#define AMBANG_GELAP    1800
#define SERVO_BUKA      90
#define SERVO_TUTUP     0

// ── UID KARTU RFID TERDAFTAR ─────────────────────
const byte UID_TERDAFTAR[][4] = {
  {0x01, 0x02, 0x03, 0x04},
  {0x11, 0x22, 0x33, 0x44},
  {0x55, 0x66, 0x77, 0x88}
};
const int JUMLAH_KARTU = 3;

// ── TIPE EVENT UNTUK QUEUE ───────────────────────
typedef enum {
  EVT_KARTU_VALID,
  EVT_KARTU_INVALID,
  EVT_SLOT_PENUH,
  EVT_PALANG_TUTUP,
  EVT_RESET_DARURAT
} EventType;

// ── OBJEK ────────────────────────────────────────
MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);
Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);

// ── SHARED DATA ───────────────────────────────────
int           slotTerisi          = 0;
bool          palangTerbuka       = false;
bool          kendaraanTerdeteksi = false;
bool          lampuMenyala        = false;
float         jarakTerakhir       = 999.0;
String        pesanOLED1          = "Sistem Parkir";
String        pesanOLED2          = "Siap Beroperasi";
String        pesanOLED3          = "";
bool          modeScan            = false;

// ── RTOS HANDLES ─────────────────────────────────
SemaphoreHandle_t xMutex_SharedData;
SemaphoreHandle_t xSemaphore_RFID;
QueueHandle_t     xQueue_Event;

// ── ISR BUTTON ───────────────────────────────────
volatile bool resetDarurat = false;
void IRAM_ATTR ISR_ResetDarurat() {
  resetDarurat = true;
}

// ══════════════════════════════════════════════════
// HELPER
// ══════════════════════════════════════════════════
float ukurJarak() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  long durasi = pulseIn(PIN_ECHO, HIGH, 30000);
  return (durasi * 0.0343) / 2.0;
}

bool cekKartuTerdaftar(byte* uid, byte ukuranUID) {
  if (ukuranUID != 4) return false;
  for (int i = 0; i < JUMLAH_KARTU; i++) {
    if (memcmp(uid, UID_TERDAFTAR[i], 4) == 0) return true;
  }
  return false;
}

void setStatusOLED(String b1, String b2, String b3 = "") {
  if (xSemaphoreTake(xMutex_SharedData, pdMS_TO_TICKS(50)) == pdTRUE) {
    pesanOLED1 = b1;
    pesanOLED2 = b2;
    pesanOLED3 = b3;
    xSemaphoreGive(xMutex_SharedData);
  }
}

// ══════════════════════════════════════════════════
// TASK 1: ULTRASONIC
// ══════════════════════════════════════════════════
void TaskUltrasonic(void* pvParameters) {
  for (;;) {
    float jarak = ukurJarak();

    if (xSemaphoreTake(xMutex_SharedData, pdMS_TO_TICKS(10)) == pdTRUE) {
      jarakTerakhir = jarak;

      if (!palangTerbuka) {
        bool terdeteksi = (jarak < JARAK_BATAS && jarak > 0);
        if (terdeteksi && !kendaraanTerdeteksi) {
          kendaraanTerdeteksi = true;
          modeScan = true;
          digitalWrite(PIN_LED_KUNING, HIGH);
          digitalWrite(PIN_LED_HIJAU, LOW);
          digitalWrite(PIN_LED_MERAH, LOW);
          xSemaphoreGive(xMutex_SharedData);
          setStatusOLED("  Selamat", "  Datang!", "Scan kartu RFID");
          xSemaphoreGive(xSemaphore_RFID);
          continue;
        } else if (!terdeteksi && kendaraanTerdeteksi) {
          kendaraanTerdeteksi = false;
          modeScan = false;
          digitalWrite(PIN_LED_KUNING, LOW);
        }
      }
      xSemaphoreGive(xMutex_SharedData);
    }
    vTaskDelay(pdMS_TO_TICKS(150));
  }
}

// ══════════════════════════════════════════════════
// TASK 2: RFID
// ══════════════════════════════════════════════════
void TaskRFID(void* pvParameters) {
  for (;;) {
    xSemaphoreTake(xSemaphore_RFID, portMAX_DELAY);
    Serial.println("[RFID] Mode aktif, menunggu kartu...");

    bool masihAda = true;
    while (masihAda) {
      if (xSemaphoreTake(xMutex_SharedData, pdMS_TO_TICKS(10)) == pdTRUE) {
        masihAda = kendaraanTerdeteksi && !palangTerbuka;
        xSemaphoreGive(xMutex_SharedData);
      }
      if (!masihAda) break;

      if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
        vTaskDelay(pdMS_TO_TICKS(50));
        continue;
      }

      Serial.print("[RFID] UID: ");
      for (byte i = 0; i < rfid.uid.size; i++) Serial.printf("%02X ", rfid.uid.uidByte[i]);
      Serial.println();

      EventType evt;
      bool slotPenuh = false, valid = false;

      if (xSemaphoreTake(xMutex_SharedData, pdMS_TO_TICKS(50)) == pdTRUE) {
        slotPenuh = (slotTerisi >= KAPASITAS_SLOT);
        valid = cekKartuTerdaftar(rfid.uid.uidByte, rfid.uid.size);
        xSemaphoreGive(xMutex_SharedData);
      }

      if (slotPenuh) {
        evt = EVT_SLOT_PENUH;
        setStatusOLED("  SLOT PENUH!", " Maaf, coba", "   lagi nanti");
      } else if (valid) {
        evt = EVT_KARTU_VALID;
        setStatusOLED("Akses Diterima", "Palang Terbuka", "Selamat masuk!");
        if (xSemaphoreTake(xMutex_SharedData, pdMS_TO_TICKS(100)) == pdTRUE) {
          servoWrite(SERVO_BUKA);
          palangTerbuka = true;
          kendaraanTerdeteksi = false;
          modeScan = false;
          digitalWrite(PIN_LED_KUNING, LOW);
          digitalWrite(PIN_LED_HIJAU, HIGH);
          xSemaphoreGive(xMutex_SharedData);
        }
      } else {
        evt = EVT_KARTU_INVALID;
        setStatusOLED("Kartu Invalid!", "Akses Ditolak", "Coba lagi...");
      }

      xQueueSend(xQueue_Event, &evt, pdMS_TO_TICKS(50));
      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();

      if (evt == EVT_KARTU_VALID) break;
      vTaskDelay(pdMS_TO_TICKS(2000));
    }
  }
}

// ══════════════════════════════════════════════════
// TASK 3: PALANG
// ══════════════════════════════════════════════════
void TaskPalang(void* pvParameters) {
  for (;;) {
    bool buka = false;
    if (xSemaphoreTake(xMutex_SharedData, pdMS_TO_TICKS(10)) == pdTRUE) {
      buka = palangTerbuka;
      xSemaphoreGive(xMutex_SharedData);
    }

    if (buka) {
      float jarak = 0;
      if (xSemaphoreTake(xMutex_SharedData, pdMS_TO_TICKS(10)) == pdTRUE) {
        jarak = jarakTerakhir;
        xSemaphoreGive(xMutex_SharedData);
      }

      if (jarak <= JARAK_DEKAT && jarak > 0) {
        Serial.println("[PALANG] Hitung mundur 5 detik...");
        for (int i = 5; i >= 0; i--) {
          setStatusOLED("Kendaraan Lewat", "Menutup dalam:", String(i) + " detik...");
          vTaskDelay(pdMS_TO_TICKS(1000));
        }
        if (xSemaphoreTake(xMutex_SharedData, pdMS_TO_TICKS(100)) == pdTRUE) {
          servoWrite(SERVO_TUTUP);
          palangTerbuka = false;
          slotTerisi++;
          int kosong = KAPASITAS_SLOT - slotTerisi;
          digitalWrite(PIN_LED_HIJAU, kosong > 0 ? HIGH : LOW);
          digitalWrite(PIN_LED_MERAH, kosong <= 0 ? HIGH : LOW);
          String b2 = (kosong > 0) ? "Kosong: " + String(kosong) : "  PARKIR PENUH!";
          xSemaphoreGive(xMutex_SharedData);
          setStatusOLED("Slot: " + String(slotTerisi) + "/" + String(KAPASITAS_SLOT), b2, "");
        }
        EventType evt = EVT_PALANG_TUTUP;
        xQueueSend(xQueue_Event, &evt, pdMS_TO_TICKS(50));
      }
    }

    // Cek reset darurat
    if (resetDarurat) {
      resetDarurat = false;
      EventType evt = EVT_RESET_DARURAT;
      xQueueSend(xQueue_Event, &evt, pdMS_TO_TICKS(50));
      if (xSemaphoreTake(xMutex_SharedData, pdMS_TO_TICKS(100)) == pdTRUE) {
        servoWrite(SERVO_TUTUP);
        palangTerbuka = false;
        kendaraanTerdeteksi = false;
        slotTerisi = 0;
        modeScan = false;
        lampuMenyala = false;
        digitalWrite(PIN_LED_KUNING, LOW);
        digitalWrite(PIN_LED_HIJAU, HIGH);
        digitalWrite(PIN_LED_MERAH, LOW);
        digitalWrite(PIN_LED_LAMPU, LOW);
        xSemaphoreGive(xMutex_SharedData);
      }
      setStatusOLED("!! RESET !!", "Sistem direset", "Slot: 0/" + String(KAPASITAS_SLOT));
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ══════════════════════════════════════════════════
// TASK 4: BUZZER + LED
// ══════════════════════════════════════════════════
void TaskBuzzerLED(void* pvParameters) {
  EventType evt;
  for (;;) {
    if (xQueueReceive(xQueue_Event, &evt, pdMS_TO_TICKS(100)) == pdTRUE) {
      switch (evt) {
        case EVT_KARTU_VALID:
          for (int i = 0; i < 3; i++) {
            digitalWrite(PIN_LED_HIJAU, HIGH); digitalWrite(PIN_BUZZER, HIGH);
            vTaskDelay(pdMS_TO_TICKS(200));
            digitalWrite(PIN_LED_HIJAU, LOW);  digitalWrite(PIN_BUZZER, LOW);
            vTaskDelay(pdMS_TO_TICKS(100));
          }
          digitalWrite(PIN_LED_HIJAU, HIGH);
          break;
        case EVT_KARTU_INVALID:
          for (int i = 0; i < 5; i++) {
            digitalWrite(PIN_LED_MERAH, HIGH); digitalWrite(PIN_BUZZER, HIGH);
            vTaskDelay(pdMS_TO_TICKS(80));
            digitalWrite(PIN_LED_MERAH, LOW);  digitalWrite(PIN_BUZZER, LOW);
            vTaskDelay(pdMS_TO_TICKS(80));
          }
          vTaskDelay(pdMS_TO_TICKS(1500));
          if (xSemaphoreTake(xMutex_SharedData, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (kendaraanTerdeteksi) digitalWrite(PIN_LED_KUNING, HIGH);
            xSemaphoreGive(xMutex_SharedData);
          }
          break;
        case EVT_SLOT_PENUH:
          for (int i = 0; i < 3; i++) {
            digitalWrite(PIN_LED_MERAH, HIGH); digitalWrite(PIN_BUZZER, HIGH);
            vTaskDelay(pdMS_TO_TICKS(150));
            digitalWrite(PIN_LED_MERAH, LOW);  digitalWrite(PIN_BUZZER, LOW);
            vTaskDelay(pdMS_TO_TICKS(150));
          }
          break;
        case EVT_PALANG_TUTUP:
          for (int i = 0; i < 2; i++) {
            digitalWrite(PIN_BUZZER, HIGH); vTaskDelay(pdMS_TO_TICKS(150));
            digitalWrite(PIN_BUZZER, LOW);  vTaskDelay(pdMS_TO_TICKS(150));
          }
          break;
        case EVT_RESET_DARURAT:
          for (int i = 0; i < 5; i++) {
            digitalWrite(PIN_BUZZER, HIGH); digitalWrite(PIN_LED_MERAH, HIGH);
            vTaskDelay(pdMS_TO_TICKS(100));
            digitalWrite(PIN_BUZZER, LOW);  digitalWrite(PIN_LED_MERAH, LOW);
            vTaskDelay(pdMS_TO_TICKS(100));
          }
          break;
      }
    }
  }
}

// ══════════════════════════════════════════════════
// TASK 5: DISPLAY OLED
// ══════════════════════════════════════════════════
void TaskDisplay(void* pvParameters) {
  for (;;) {
    String b1, b2, b3;
    int slot;
    float jarak;
    bool lampu;

    if (xSemaphoreTake(xMutex_SharedData, pdMS_TO_TICKS(30)) == pdTRUE) {
      b1    = pesanOLED1; b2 = pesanOLED2; b3 = pesanOLED3;
      slot  = slotTerisi;
      jarak = jarakTerakhir;
      lampu = lampuMenyala;
      xSemaphoreGive(xMutex_SharedData);
    }

    oled.clearDisplay();
    oled.fillRect(0, 0, 128, 12, WHITE);
    oled.setTextColor(BLACK); oled.setTextSize(1);
    oled.setCursor(2, 2);   oled.print("PARKIR UNSOED");
    oled.setCursor(90, 2);  oled.print(String(slot) + "/" + String(KAPASITAS_SLOT));

    oled.setTextColor(WHITE);
    oled.setCursor(0, 16); oled.print(b1);
    oled.setCursor(0, 28); oled.print(b2);
    oled.setCursor(0, 40); oled.print(b3);

    oled.drawLine(0, 52, 128, 52, WHITE);
    oled.setCursor(0, 55); oled.print("Jrk:");
    if (jarak <= 0 || jarak > 400) oled.print("--");
    else oled.print(String((int)jarak));
    oled.print("cm ");
    oled.print(lampu ? "LAMP:ON" : "LAMP:OFF");

    oled.display();
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

// ══════════════════════════════════════════════════
// TASK 6: LDR
// ══════════════════════════════════════════════════
void TaskLDR(void* pvParameters) {
  for (;;) {
    int cahaya = analogRead(PIN_LDR);
    if (xSemaphoreTake(xMutex_SharedData, pdMS_TO_TICKS(20)) == pdTRUE) {
      if (cahaya < AMBANG_GELAP && !lampuMenyala) {
        lampuMenyala = true;
        digitalWrite(PIN_LED_LAMPU, HIGH);
      } else if (cahaya >= AMBANG_GELAP && lampuMenyala) {
        lampuMenyala = false;
        digitalWrite(PIN_LED_LAMPU, LOW);
      }
      xSemaphoreGive(xMutex_SharedData);
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

// ══════════════════════════════════════════════════
// TASK 7: INDIKATOR SLOT (LED HIJAU/MERAH)
// ══════════════════════════════════════════════════
void TaskSlotIndicator(void* pvParameters) {
  for (;;) {
    int slot;
    if (xSemaphoreTake(xMutex_SharedData, pdMS_TO_TICKS(20)) == pdTRUE) {
      slot = slotTerisi;
      if (slot < KAPASITAS_SLOT) {
        digitalWrite(PIN_LED_HIJAU, HIGH);
        digitalWrite(PIN_LED_MERAH, LOW);
      } else {
        digitalWrite(PIN_LED_HIJAU, LOW);
        digitalWrite(PIN_LED_MERAH, HIGH);
      }
      xSemaphoreGive(xMutex_SharedData);
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

// ══════════════════════════════════════════════════
// TASK 8: SERIAL MONITOR INTERAKTIF
// ══════════════════════════════════════════════════
void TaskSerial(void* pvParameters) {
  vTaskDelay(pdMS_TO_TICKS(2000)); // tunggu sistem ready

  Serial.println("╔══════════════════════════════════════╗");
  Serial.println("║   SISTEM PARKIR UNSOED - FreeRTOS    ║");
  Serial.println("╠══════════════════════════════════════╣");
  Serial.println("║ Perintah:                            ║");
  Serial.println("║  'status' → lihat status parkir      ║");
  Serial.println("║  'uid'    → cara daftarin kartu      ║");
  Serial.println("║  'reset'  → reset sistem             ║");
  Serial.println("║  'help'   → tampilkan menu ini       ║");
  Serial.println("╚══════════════════════════════════════╝");

  String inputBuffer = "";

  for (;;) {
    // ── Print status otomatis tiap 5 detik ──
    static unsigned long lastPrint = 0;
    if (millis() - lastPrint >= 5000) {
      lastPrint = millis();

      int slot, kapasitas = KAPASITAS_SLOT;
      bool palang, lampu;
      float jarak;

      if (xSemaphoreTake(xMutex_SharedData, pdMS_TO_TICKS(20)) == pdTRUE) {
        slot   = slotTerisi;
        palang = palangTerbuka;
        lampu  = lampuMenyala;
        jarak  = jarakTerakhir;
        xSemaphoreGive(xMutex_SharedData);
      }

      Serial.println("──────────────────────────────────────");
      Serial.print  ("  Slot terisi : "); Serial.print(slot); Serial.print("/"); Serial.println(kapasitas);
      Serial.print  ("  Slot kosong : "); Serial.println(kapasitas - slot);
      Serial.print  ("  Palang      : "); Serial.println(palang ? "TERBUKA" : "TERTUTUP");
      Serial.print  ("  Jarak       : "); Serial.print((int)jarak); Serial.println(" cm");
      Serial.print  ("  Lampu       : "); Serial.println(lampu ? "MENYALA" : "MATI");
      Serial.println("──────────────────────────────────────");
    }

    // ── Baca input dari Serial ──
    while (Serial.available()) {
      char c = (char)Serial.read();
      if (c == '\n' || c == '\r') {
        inputBuffer.trim();
        if (inputBuffer.length() > 0) {
          Serial.print("> "); Serial.println(inputBuffer);

          if (inputBuffer == "status") {
            int slot;
            bool palang, lampu;
            float jarak;
            if (xSemaphoreTake(xMutex_SharedData, pdMS_TO_TICKS(20)) == pdTRUE) {
              slot   = slotTerisi;
              palang = palangTerbuka;
              lampu  = lampuMenyala;
              jarak  = jarakTerakhir;
              xSemaphoreGive(xMutex_SharedData);
            }
            Serial.println("═══════ STATUS PARKIR ═══════");
            Serial.print("  Slot   : "); Serial.print(slot); Serial.print("/"); Serial.println(KAPASITAS_SLOT);
            Serial.print("  Kosong : "); Serial.println(KAPASITAS_SLOT - slot);
            Serial.print("  Palang : "); Serial.println(palang ? "TERBUKA ✓" : "TERTUTUP");
            Serial.print("  Jarak  : "); Serial.print((int)jarak); Serial.println(" cm");
            Serial.print("  Lampu  : "); Serial.println(lampu ? "ON 💡" : "OFF");
            Serial.println("═════════════════════════════");

          } else if (inputBuffer == "uid") {
            Serial.println("═══════ CARA DAFTARIN KARTU ═══════");
            Serial.println("  1. Klik komponen RFID di Wokwi");
            Serial.println("  2. Di panel kanan isi UID (contoh: DE AD BE EF)");
            Serial.println("  3. Klik 'Scan Card'");
            Serial.println("  4. Lihat UID yang muncul di sini:");
            Serial.println("     [RFID] UID: XX XX XX XX");
            Serial.println("  5. Salin ke kode:");
            Serial.println("     const byte UID_TERDAFTAR[][4] = {");
            Serial.println("       {0xXX, 0xXX, 0xXX, 0xXX},");
            Serial.println("     };");
            Serial.println("════════════════════════════════════");

          } else if (inputBuffer == "reset") {
            Serial.println("[SERIAL] Reset manual via Serial Monitor...");
            resetDarurat = true;

          } else if (inputBuffer == "help") {
            Serial.println("╔══════════════════════════════════════╗");
            Serial.println("║ Perintah:                            ║");
            Serial.println("║  'status' → lihat status parkir      ║");
            Serial.println("║  'uid'    → cara daftarin kartu      ║");
            Serial.println("║  'reset'  → reset sistem             ║");
            Serial.println("║  'help'   → tampilkan menu ini       ║");
            Serial.println("╚══════════════════════════════════════╝");

          } else {
            Serial.print("[!] Perintah tidak dikenal: ");
            Serial.println(inputBuffer);
            Serial.println("    Ketik 'help' untuk daftar perintah.");
          }
        }
        inputBuffer = "";
      } else {
        inputBuffer += c;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ══════════════════════════════════════════════════
// SETUP
// ══════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);

  pinMode(PIN_TRIG,       OUTPUT);
  pinMode(PIN_ECHO,       INPUT);
  pinMode(PIN_BUZZER,     OUTPUT);
  pinMode(PIN_LED_HIJAU,  OUTPUT);
  pinMode(PIN_LED_MERAH,  OUTPUT);
  pinMode(PIN_LED_KUNING, OUTPUT);
  pinMode(PIN_LED_LAMPU,  OUTPUT);
  pinMode(PIN_BUTTON,     INPUT_PULLUP);

  digitalWrite(PIN_LED_HIJAU,  LOW);
  digitalWrite(PIN_LED_MERAH,  LOW);
  digitalWrite(PIN_LED_KUNING, LOW);
  digitalWrite(PIN_LED_LAMPU,  LOW);

  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), ISR_ResetDarurat, FALLING);

  SPI.begin(18, 19, 23, PIN_RFID_SS);
  rfid.PCD_Init();

  servoAttach();
  servoWrite(SERVO_TUTUP);

  Wire.begin(PIN_SDA, PIN_SCL);
  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED gagal!");
  } else {
    oled.clearDisplay();
    oled.setTextColor(WHITE);
    oled.setTextSize(1);
    oled.setCursor(10, 20); oled.print("PARKIR UNSOED");
    oled.setCursor(15, 35); oled.print("FreeRTOS Ready");
    oled.display();
  }

  xMutex_SharedData = xSemaphoreCreateMutex();
  xSemaphore_RFID   = xSemaphoreCreateBinary();
  xQueue_Event      = xQueueCreate(10, sizeof(EventType));

  xTaskCreatePinnedToCore(TaskRFID,       "RFID",       4096, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(TaskUltrasonic, "Ultrasonic", 2048, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(TaskPalang,     "Palang",     2048, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(TaskBuzzerLED,  "BuzzerLED",  2048, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(TaskDisplay,    "Display",    4096, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(TaskLDR,        "LDR",        1024, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(TaskSlotIndicator, "SlotInd", 1024, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(TaskSerial, "Serial", 3072, NULL, 1, NULL, 0);

  Serial.println("=== SISTEM PARKIR ESP32 + FreeRTOS AKTIF ===");
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}