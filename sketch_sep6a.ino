#include <WiFi.h>
#include <FirebaseESP32.h>
#include <DHT.h>
#include <ESP32Servo.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// ================= PENGATURAN WI-FI & FIREBASE =================
const char* ssid = "Cah Ndeso";
const char* password = "putuneeyangsuro131";

// URL Firebase (TANPA https:// dan TANPA garis miring / di akhir)
#define FIREBASE_HOST "rc-telementri-default-rtdb.asia-southeast1.firebasedatabase.app"
// Kunci Rahasia / Database Secret
#define FIREBASE_AUTH "y4KdPtOZOokvMWl8qdZuSUpeOla0NAXgQsOW5a02"

// Objek Firebase
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// ================= PENGATURAN NOTIFIKASI WHATSAPP (Fonnte) =================
const char* FONNTE_TOKEN = "eLwufMjLcsdnLTP59jHa";
const char* WA_PHONE  = "6287789175820"; // nomor tujuan notifikasi

// Jeda minimum antar pengiriman WhatsApp (ms) agar tidak spam
const unsigned long WA_COOLDOWN = 60000; // 60 detik
unsigned long lastWaSent = 0;

// ================= DEKLARASI PIN & SENSOR =================
#define DHTPIN 15
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

#define TRIG_PIN 5
#define ECHO_PIN 18
#define SERVO_PIN 19
#define BUZZER_PIN 21
#define MQ2_PIN 36

// Pin Motor L298N
#define IN1 25
#define IN2 26
#define IN3 27
#define IN4 14

Servo radarServo;

// ================= VARIABEL GLOBAL =================
float humidity = -999.0;
float temperature = -999.0;
int gasValue = 0;
long distance = 0;
bool isAlarm = false;

unsigned long previousMillis = 0;
unsigned long dhtMillis = 0;
unsigned long heartbeatMillis = 0;
unsigned long buzzerMillis = 0;
bool buzzerState = LOW;
bool firstSensorRead = true;

const int GAS_THRESHOLD = 2000;
const float HUMIDITY_THRESHOLD = 80.0;
const float SUHU_THRESHOLD = 40.0;

// ================= VARIABEL SWEEP SERVO (0 - 180 derajat) =================
int servoAngle = 0;          // sudut servo saat ini
int servoDirection = 1;      // 1 = bertambah (ke kanan), -1 = berkurang (ke kiri)
unsigned long servoMillis = 0;
const int SERVO_STEP_DELAY = 20; // ms antar langkah, makin kecil makin cepat sweepnya
const int SERVO_STEP_SIZE = 1;   // derajat per langkah

// ================= PENGATURAN PENGHINDARAN RINTANGAN (MOTOR DC) =================
// Jarak (cm) dari sensor ultrasonik di atas servo biru, sebagai batas aman.
// Kalau jarak terbaca LEBIH DEKAT dari ini, robot mulai manuver menghindar.
const int OBSTACLE_THRESHOLD_CM = 15;

// Durasi tiap tahap manuver (non-blocking, pakai millis() bukan delay())
const unsigned long DURASI_MUNDUR = 500;  // ms mundur sebelum belok
const unsigned long DURASI_BELOK  = 450;  // ms durasi belok (sesuaikan agar sudut belok pas)

// State mesin untuk navigasi robot, sesuai flowchart proposal:
// MAJU -> (jika ada rintangan) -> MUNDUR -> BELOK (kiri/kanan bergantian) -> MAJU lagi
enum StatusGerak { MAJU, MUNDUR, BELOK };
StatusGerak statusGerak = MAJU;
unsigned long gerakMillis = 0;
bool belokKeKanan = true; // bergantian kiri/kanan setiap kali menghindar

// ================= FUNGSI KONTROL MOTOR DC (DRIVER L298N) =================
// Catatan: Motor Kiri = IN1/IN2, Motor Kanan = IN3/IN4.
// Kalau arah fisik robot terbalik (maju jadi mundur, dsb), tukar HIGH/LOW
// pada motor yang bersangkutan.
void motorMaju() {
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);  // kiri maju
  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);  // kanan maju
}

void motorMundur() {
  digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH);  // kiri mundur
  digitalWrite(IN3, LOW); digitalWrite(IN4, HIGH);  // kanan mundur
}

void motorBelokKanan() {
  // Skid-steer: kiri maju, kanan mundur -> robot berputar ke kanan di tempat
  digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);  digitalWrite(IN4, HIGH);
}

void motorBelokKiri() {
  // Skid-steer: kiri mundur, kanan maju -> robot berputar ke kiri di tempat
  digitalWrite(IN1, LOW);  digitalWrite(IN2, HIGH);
  digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
}

void motorBerhenti() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}

// Set TRUE untuk jalankan tes motor otomatis sekali saat ESP32 baru nyala
// (maju, mundur, belok kanan, belok kiri bergantian), TANPA bergantung pada
// sensor jarak sama sekali. Ini untuk memastikan wiring & hardware motor
// benar-benar berfungsi, terlepas dari logika penghindaran rintangan.
// Set FALSE lagi setelah motor dipastikan bergerak dengan benar.
const bool MOTOR_SELF_TEST = true;

void jalankanTesMotor() {
  Serial.println("=== TES MOTOR DIMULAI (self-test, abaikan sensor jarak) ===");

  Serial.println("[TES] MAJU selama 1.5 detik...");
  motorMaju();
  delay(1500);

  Serial.println("[TES] BERHENTI...");
  motorBerhenti();
  delay(800);

  Serial.println("[TES] MUNDUR selama 1.5 detik...");
  motorMundur();
  delay(1500);

  Serial.println("[TES] BERHENTI...");
  motorBerhenti();
  delay(800);

  Serial.println("[TES] BELOK KANAN selama 1.5 detik...");
  motorBelokKanan();
  delay(1500);

  Serial.println("[TES] BERHENTI...");
  motorBerhenti();
  delay(800);

  Serial.println("[TES] BELOK KIRI selama 1.5 detik...");
  motorBelokKiri();
  delay(1500);

  Serial.println("[TES] BERHENTI...");
  motorBerhenti();

  Serial.println("=== TES MOTOR SELESAI ===");
  Serial.println("Kalau TIDAK ADA SAMA SEKALI gerakan selama tes di atas,");
  Serial.println("masalahnya di HARDWARE (cek jumper ENA/ENB, kabel IN1-4,");
  Serial.println("tegangan baterai motor 3S, dan GND yang harus nyambung");
  Serial.println("antara ESP32, L298N, dan baterai motor).");
}

// Dipanggil tiap loop: jalankan state machine navigasi (maju/mundur/belok)
// berdasarkan jarak rintangan terbaru dari sensor ultrasonik.
void kendalikanNavigasiRobot() {
  unsigned long currentMillis = millis();

  // distance == -999 berarti sensor tidak membaca pantulan (tidak ada
  // rintangan dalam jangkauan ~5m), jadi dianggap aman.
  bool adaRintanganDekat = (distance > 0 && distance <= OBSTACLE_THRESHOLD_CM);

  switch (statusGerak) {
    case MAJU:
      if (adaRintanganDekat) {
        // Rintangan terdeteksi -> mulai manuver: mundur dulu
        statusGerak = MUNDUR;
        gerakMillis = currentMillis;
        motorMundur();
        Serial.println("[MOTOR] Rintangan dekat -> MUNDUR");
      } else {
        motorMaju();
      }
      break;

    case MUNDUR:
      if (currentMillis - gerakMillis >= DURASI_MUNDUR) {
        // Selesai mundur -> lanjut belok (bergantian kiri/kanan)
        statusGerak = BELOK;
        gerakMillis = currentMillis;
        if (belokKeKanan) motorBelokKanan(); else motorBelokKiri();
        Serial.println(belokKeKanan ? "[MOTOR] BELOK KANAN" : "[MOTOR] BELOK KIRI");
      }
      break;

    case BELOK:
      if (currentMillis - gerakMillis >= DURASI_BELOK) {
        // Selesai belok -> kembali maju, dan tukar arah belok untuk giliran berikutnya
        belokKeKanan = !belokKeKanan;
        statusGerak = MAJU;
        Serial.println("[MOTOR] Selesai belok -> MAJU lagi");
      }
      break;
  }
}

// ================= FUNGSI KIRIM NOTIFIKASI WHATSAPP (FONNTE) =================
void kirimWhatsApp(String pesan) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi tidak terhubung, notifikasi WA dibatalkan.");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure(); // lewati verifikasi sertifikat SSL (cukup untuk kebutuhan proyek ini)

  HTTPClient http;
  http.begin(client, "https://api.fonnte.com/send");
  http.addHeader("Authorization", FONNTE_TOKEN);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String body = "target=" + String(WA_PHONE) + "&message=" + pesan;
  int httpCode = http.POST(body);

  if (httpCode > 0) {
    String response = http.getString();
    Serial.printf("Notifikasi WA terkirim, kode: %d\n", httpCode);
    Serial.println("Respon Fonnte: " + response);
  } else {
    Serial.printf("Gagal kirim WA, error: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
}

// Cek kondisi & kirim notifikasi jika perlu (dengan cooldown)
void cekDanKirimNotifikasi() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastWaSent < WA_COOLDOWN) return; // masih dalam cooldown

  bool gasBahaya  = (gasValue > GAS_THRESHOLD);
  bool suhuBahaya = (temperature > -100 && temperature > SUHU_THRESHOLD);
  bool humBahaya  = (humidity > -100 && humidity > HUMIDITY_THRESHOLD);

  if (gasBahaya || suhuBahaya || humBahaya) {
    String pesan = "PERINGATAN ROBOT!%0A";

    if (suhuBahaya) {
      pesan += "- Suhu tinggi: " + String(temperature, 1) + " C%0A";
    }
    if (gasBahaya) {
      pesan += "- Gas/asap terdeteksi: " + String(gasValue) + " ADC%0A";
    }
    if (humBahaya) {
      pesan += "- Kelembapan tinggi: " + String(humidity, 1) + " %25%0A";
    }
    pesan += "Segera periksa lokasi robot.";

    kirimWhatsApp(pesan);
    lastWaSent = currentMillis;
  }
}

// ================= FUNGSI BACA ULTRASONIK =================
long readUltrasonic() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  // Timeout 30ms (~5 meter) agar loop tidak macet jika pantulan hilang
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0) return -999; // Sensor tidak terbaca / di luar jangkauan
  return duration * 0.034 / 2;
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  // Inisialisasi Pin
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);

  dht.begin();
  radarServo.setPeriodHertz(50);
  radarServo.attach(SERVO_PIN, 500, 2400);

  // Pastikan motor dalam keadaan berhenti saat baru menyala
  motorBerhenti();

  // Jalankan tes motor otomatis (lihat MOTOR_SELF_TEST di atas)
  if (MOTOR_SELF_TEST) {
    jalankanTesMotor();
  }

  // Koneksi Wi-Fi
  Serial.print("Menghubungkan ke Wi-Fi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi Terhubung!");

  // Koneksi Firebase
  config.database_url = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Kirim sinyal langsung saat ESP32 selesai booting & terhubung
  Firebase.setInt(fbdo, "/ESP32_Status/heartbeat", millis());
  Serial.println("Sinyal ESP32 Terhubung terkirim ke Firebase!");

  // Notifikasi opsional saat robot online (boleh dihapus jika tidak perlu)
  kirimWhatsApp("Robot Penghindar Rintangan aktif dan terhubung ke WiFi & Firebase.");
  lastWaSent = millis();
}

// ================= LOOP UTAMA =================
void loop() {
  unsigned long currentMillis = millis();

  // 1. Timer Khusus Heartbeat Koneksi ESP32 (Setiap 1.5 Detik)
  if (currentMillis - heartbeatMillis >= 1500) {
    heartbeatMillis = currentMillis;
    Firebase.setInt(fbdo, "/ESP32_Status/heartbeat", currentMillis);
  }

  // 2. Timer Khusus DHT22 (Langsung saat start, lalu setiap 2 Detik)
  if (firstSensorRead || (currentMillis - dhtMillis >= 2000)) {
    dhtMillis = currentMillis;
    firstSensorRead = false;

    float h = dht.readHumidity();
    float t = dht.readTemperature();

    // Kirim -999 jika sensor DHT22 gagal terbaca agar website mendeteksi error sensor
    humidity = isnan(h) ? -999.0 : h;
    temperature = isnan(t) ? -999.0 : t;

    Firebase.setFloat(fbdo, "/Sensor/Kelembapan", humidity);
    Firebase.setFloat(fbdo, "/Sensor/Suhu", temperature);
  }

  // 3. Timer Real-Time MQ-2 & Ultrasonik (Setiap 800ms)
  if (currentMillis - previousMillis >= 800) {
    previousMillis = currentMillis;

    gasValue = analogRead(MQ2_PIN);
    distance = readUltrasonic();

    Firebase.setInt(fbdo, "/Sensor/Gas", gasValue);
    Firebase.setFloat(fbdo, "/Sensor/Jarak", distance);

    // Logika Alarm Buzzer Lokal di Mobil RC
    bool gasAlert = (gasValue > GAS_THRESHOLD);
    bool humAlert = (humidity > -100 && humidity > HUMIDITY_THRESHOLD);
    bool suhuAlert = (temperature > -100 && temperature > SUHU_THRESHOLD);
    isAlarm = (gasAlert || humAlert || suhuAlert);

    // Cek kondisi bahaya & kirim notifikasi WhatsApp jika perlu (dengan cooldown)
    cekDanKirimNotifikasi();

  }

  // Jalankan state machine navigasi (maju / mundur / belok) setiap loop,
  // supaya transisi antar tahap manuver tetap presisi waktunya.
  kendalikanNavigasiRobot();

  // 4. Logika Buzzer Berkedip
  if (isAlarm) {
    if (currentMillis - buzzerMillis >= 300) {
      buzzerMillis = currentMillis;
      buzzerState = !buzzerState;
      digitalWrite(BUZZER_PIN, buzzerState);
    }
  } else {
    digitalWrite(BUZZER_PIN, LOW);
    buzzerState = LOW;
  }

  // Servo sweep (radar) 0 - 180 derajat, bolak-balik, TIDAK berputar terus 360 derajat
  if (currentMillis - servoMillis >= SERVO_STEP_DELAY) {
    servoMillis = currentMillis;

    servoAngle += SERVO_STEP_SIZE * servoDirection;

    if (servoAngle >= 180) {
      servoAngle = 180;
      servoDirection = -1; // sampai di kanan, balik arah ke kiri
    } else if (servoAngle <= 0) {
      servoAngle = 0;
      servoDirection = 1;  // sampai di kiri, balik arah ke kanan
    }

    radarServo.write(servoAngle);
  }
}
