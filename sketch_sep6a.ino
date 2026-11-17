#include <WiFi.h>
#include <FirebaseESP32.h>
#include <DHT.h>
#include <ESP32Servo.h>

// ================= PENGATURAN WI-FI & FIREBASE =================
const char* ssid = "Kjiwonn";         // Ganti dengan WiFi Anda
const char* password = "geewonii28"; // Ganti dengan Password WiFi Anda

// Ganti dengan URL Firebase Anda (TANPA https:// dan TANPA garis miring / di akhir)
#define FIREBASE_HOST "rc-telementri-default-rtdb.asia-southeast1.firebasedatabase.app" 
// Ganti dengan Kunci Rahasia / Database Secret Anda
#define FIREBASE_AUTH "y4KdPtOZOokvMWl8qdZuSUpeOla0NAXgQsOW5a02"

// Objek Firebase
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// ================= DEKLARASI PIN & SENSOR =================
#define DHTPIN 15
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

#define TRIG_PIN 5
#define ECHO_PIN 18
#define SERVO_PIN 19
#define BUZZER_PIN 21
#define MQ2_PIN 36 // Sesuai dengan pin MQ-2 Anda

// Pin Motor L298N
#define IN1 25
#define IN2 26
#define IN3 27
#define IN4 14

Servo radarServo;

// ================= VARIABEL GLOBAL =================
float humidity = 0.0;
float temperature = 0.0;
int gasValue = 0;
long distance = 0;
bool isAlarm = false;

unsigned long previousMillis = 0;
unsigned long buzzerMillis = 0;
bool buzzerState = LOW;

const int GAS_THRESHOLD = 2000;
const float HUMIDITY_THRESHOLD = 80.0;

// ================= FUNGSI BACA ULTRASONIK =================
long readUltrasonic() {
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH);
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

  // Koneksi Wi-Fi
  Serial.print("Menghubungkan ke Wi-Fi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { 
    delay(500); 
    Serial.print("."); 
  }
  Serial.println("\nWi-Fi Terhubung!");

  // Koneksi Firebase (Standar Library Terbaru)
  config.database_url = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

// ================= LOOP UTAMA =================
void loop() {
  unsigned long currentMillis = millis();

  // 1. Timer Khusus DHT22 (Setiap 2 Detik)
  static unsigned long dhtMillis = 0;
  if (currentMillis - dhtMillis >= 2000) {
    dhtMillis = currentMillis;
    
    humidity = dht.readHumidity();
    temperature = dht.readTemperature();
    
    // Filter Error
    if (isnan(humidity)) humidity = 0.0;
    if (isnan(temperature)) temperature = 0.0;
    
    // Kirim Data DHT ke Firebase
    Firebase.setFloat(fbdo, "/Sensor/Kelembapan", humidity);
    Firebase.setFloat(fbdo, "/Sensor/Suhu", temperature);
  }

  // 2. Timer Utama untuk Real-Time MQ-2 & Ultrasonik (Setiap 500ms)
  if (currentMillis - previousMillis >= 500) {
    previousMillis = currentMillis;
    
    gasValue = analogRead(MQ2_PIN);
    distance = readUltrasonic();

    // Kirim Data Gas & Jarak ke Firebase
    Firebase.setInt(fbdo, "/Sensor/Gas", gasValue);
    Firebase.setFloat(fbdo, "/Sensor/Jarak", distance);

    // Logika Alarm
    if (gasValue > GAS_THRESHOLD || humidity > HUMIDITY_THRESHOLD) {
      isAlarm = true;
    } else {
      isAlarm = false;
    }
  }

  // 3. Logika Buzzer Berkedip
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

  // Servo berputar terus menerus
  radarServo.write(100); 
}