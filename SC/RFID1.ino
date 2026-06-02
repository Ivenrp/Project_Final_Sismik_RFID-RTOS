#include <SPI.h>
#include <MFRC522.h>
#include <ESP32Servo.h>

#define SDA_PIN      5
#define SCK_PIN     18
#define MISO_PIN    19
#define MOSI_PIN    23
#define RST_PIN     27

#define SERVO_PIN   13
#define BUZZER_PIN  14

#define TRIG_PIN    32
#define ECHO_PIN    33

MFRC522 rfid(SS_PIN, RST_PIN);
Servo gateServo;

// GANTI UID CARD FRID
byte authorizedUID[4] = {0x93, 0x4A, 0x2F, 0x1C};

volatile bool carDetected = false;
volatile bool gateBusy = false;

TaskHandle_t ultrasonicTaskHandle;
TaskHandle_t rfidTaskHandle;

// ======================================================
// CEK UID KARTU
// ======================================================
bool uidMatch(MFRC522::Uid *uid)
{
    if (uid->size != 4)
        return false;

    for (int i = 0; i < 4; i++)
    {
        if (uid->uidByte[i] != authorizedUID[i])
        {
            return false;
        }
    }

    return true;
}

// ======================================================
// TASK ULTRASONIC
// ======================================================
void ultrasonicTask(void *pvParameters)
{
    while (true)
    {
        digitalWrite(TRIG_PIN, LOW);
        delayMicroseconds(2);

        digitalWrite(TRIG_PIN, HIGH);
        delayMicroseconds(10);

        digitalWrite(TRIG_PIN, LOW);

        long duration = pulseIn(ECHO_PIN, HIGH, 30000);

        int distance = duration * 0.034 / 2;

        if (distance > 0 && distance <= 18)
        {
            if (!carDetected)
            {
                Serial.println("\nADA MOBIL");
                Serial.println("SILAKAN TAP KARTU");
            }

            carDetected = true;
        }
        else
        {
            if (carDetected)
            {
                Serial.println("\nTIDAK ADA MOBIL");
            }

            carDetected = false;
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// ======================================================
// TASK RFID
// ======================================================
void rfidTask(void *pvParameters)
{
    while (true)
    {
        if (carDetected && !gateBusy)
        {
            if (rfid.PICC_IsNewCardPresent() &&
                rfid.PICC_ReadCardSerial())
            {
                Serial.print("UID : ");

                for (byte i = 0; i < rfid.uid.size; i++)
                {
                    Serial.print(rfid.uid.uidByte[i], HEX);
                    Serial.print(" ");
                }

                Serial.println();

                if (uidMatch(&rfid.uid))
                {
                    gateBusy = true;

                    Serial.println("AKSES DITERIMA");
                    Serial.println("PALANG DIBUKA");

                    ledcWrite(BUZZER_PIN, 128);

                    gateServo.write(0);

                    vTaskDelay(pdMS_TO_TICKS(1000));

                    ledcWrite(BUZZER_PIN, 0);

                    vTaskDelay(pdMS_TO_TICKS(5000));

                    gateServo.write(90);

                    Serial.println("PALANG DITUTUP");

                    gateBusy = false;
                }
                else
                {
                    Serial.println("AKSES DITOLAK");

                    ledcWrite(BUZZER_PIN, 128);
                    vTaskDelay(pdMS_TO_TICKS(500));
                    ledcWrite(BUZZER_PIN, 0);
                }

                rfid.PICC_HaltA();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ======================================================
// SETUP
// ======================================================
void setup()
{
    Serial.begin(115200);

    Serial.println("================================");
    Serial.println("      SISTEM PARKIR RTOS");
    Serial.println("================================");

    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);

    gateServo.attach(SERVO_PIN);
    gateServo.write(90);

    ledcAttach(BUZZER_PIN, 2000, 8);

    SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SDA_PIN);

    rfid.PCD_Init();

    xTaskCreatePinnedToCore(
        ultrasonicTask,
        "UltrasonicTask",
        4096,
        NULL,
        1,
        &ultrasonicTaskHandle,
        0);

    xTaskCreatePinnedToCore(
        rfidTask,
        "RFIDTask",
        4096,
        NULL,
        1,
        &rfidTaskHandle,
        1);
}

// ======================================================
// LOOP
// ======================================================
void loop()
{
    vTaskDelete(NULL);
}