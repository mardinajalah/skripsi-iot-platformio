#include <Arduino.h>
#include <WiFi.h>
#include "time.h" // Library bawaan untuk sinkronisasi waktu internet

// Definisikan kontrol flash tepat sebelum library Firebase dipanggil
#define FIREBASE_USE_PSRAM
#include <Firebase_ESP_Client.h>

// Sertakan berkas pembantu (addons)
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// Library untuk Display OLED
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Konfigurasi Layar OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Konfigurasi WiFi (mengambil nilai dari macro .env yang disuntikkan)
#ifndef SECRET_WIFI_SSID
  #define SECRET_WIFI_SSID "DefaultWiFi"
#endif
#ifndef SECRET_WIFI_PASS
  #define SECRET_WIFI_PASS "DefaultPassword"
#endif

const char* ssid = SECRET_WIFI_SSID;
const char* password = SECRET_WIFI_PASS;

// Konfigurasi Firebase (mengambil nilai dari macro .env yang disuntikkan)
#ifndef SECRET_FIREBASE_API_KEY
  #define API_KEY "DefaultAPIKey"
#else
  #define API_KEY SECRET_FIREBASE_API_KEY
#endif

#ifndef SECRET_FIREBASE_DB_URL
  #define DATABASE_URL "DefaultDbURL"
#else
  #define DATABASE_URL SECRET_FIREBASE_DB_URL
#endif

// Definisi Objek Firebase
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// Definisi Pin
int led = 25;
int saklar = 26;
int relay = 27;

int statusTerakhir = -2; 

// Fungsi untuk sinkronisasi waktu dengan server NTP Google/Indonesia
void sinkronisasiWaktu() {
  Serial.println("Sinkronisasi waktu dimulai...");

  configTime(25200, 0, "pool.ntp.org", "time.nist.gov");

  time_t now = time(nullptr);

  int retry = 0;
  while (now < 100000 && retry < 20) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
    retry++;
  }

  Serial.println();

  if (now > 100000) {
    Serial.println("Waktu berhasil disinkronkan!");
    Serial.println(ctime(&now));
  } else {
    Serial.println("Gagal sinkronisasi waktu!");
  }
}

void cekWiFi() {

  static unsigned long lastReconnectAttempt = 0;

  if (WiFi.status() != WL_CONNECTED &&
      millis() - lastReconnectAttempt > 5000) {

    lastReconnectAttempt = millis();

    Serial.println("WiFi terputus! Mencoba reconnect...");

    WiFi.disconnect();
    WiFi.begin(ssid, password);
  }

  if (WiFi.status() == WL_CONNECTED) {

    static bool sudahCetak = false;

    if (!sudahCetak) {
      Serial.println("WiFi berhasil terhubung kembali!");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());

      sudahCetak = true;
    }

  } else {
    static bool sudahCetakPutus = false;

    if (!sudahCetakPutus) {
      Serial.println("WiFi DISCONNECTED!");
      sudahCetakPutus = true;
    }
  }
}

void setup() {
  Serial.begin(115200);

  delay(2000);
  Serial.println();
  Serial.println("=================================");
  Serial.println("ESP32 STARTING...");
  Serial.println("=================================");

  pinMode(led, OUTPUT);
  pinMode(saklar, INPUT_PULLUP); 
  pinMode(relay, OUTPUT);

  // Set kondisi awal hardware
  digitalWrite(relay, HIGH); // Active Low (HIGH = MATI)
  digitalWrite(led, LOW);

  // Inisialisasi OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("OLED gagal diinisialisasi"));
    for(;;); 
  }

  // Tampilan OLED: Connecting WiFi
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Connecting WiFi...");
  display.display();

  // Memulai WiFi
  Serial.println("Menghubungkan WiFi...");

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi Terhubung!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Tampilan OLED: Sinkronisasi Waktu Server
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Syncing Time Server...");
  display.display();
  
  // Panggil fungsi sinkronisasi waktu sebelum inisialisasi Firebase
  sinkronisasiWaktu();

  // Tampilan OLED: Menghubungkan Firebase
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Connecting Firebase...");
  display.display();

  // Konfigurasi parameter Firebase
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  // TEST MODE
  config.signer.test_mode = true;

  // Menetapkan fungsi callback untuk memantau status token di Serial Monitor
  config.token_status_callback = tokenStatusCallback;

  // Mengizinkan penanganan pemutusan koneksi WiFi secara otomatis oleh Firebase
  Firebase.reconnectWiFi(true);

  // Menginisialisasi koneksi ke server Firebase

  Serial.print("Firebase Client Version: ");
  Serial.println(FIREBASE_CLIENT_VERSION);

  Serial.println("Firebase begin...");
  Firebase.begin(&config, &auth);


  unsigned long startTime = millis();

  while (!Firebase.ready() && millis() - startTime < 30000) {
    Serial.print(".");
    delay(500);
  }

  Serial.println();

  if (Firebase.ready()) {
    Serial.println("Firebase READY!");
  } else {
    Serial.println("Firebase TIDAK READY!");
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("SISTEM SIAP!");
  display.display();
  delay(1500);
}

void loop() {
   // Cek dan reconnect WiFi jika terputus
  cekWiFi();

  // 1. Baca kondisi dari saklar fisik
  int statusSaklar = digitalRead(saklar);

  // 2. Logika kendali LED & Relay
  if (statusSaklar == LOW) {
    digitalWrite(led, HIGH);   
    digitalWrite(relay, LOW);  
  } else {
    digitalWrite(led, LOW);    
    digitalWrite(relay, HIGH); 
  }

  // 3. KIRIM DATA KE FIREBASE 
  if (statusSaklar != statusTerakhir) {
    
    if (Firebase.ready()) {
      String statusStr = (statusSaklar == LOW) ? "ON" : "OFF";
      
      Serial.println("Mencoba mengirim data ke Firebase...");
      
      // Kirim Status String ke path /kontrol/saklar
      if (Firebase.RTDB.setString(&fbdo, "/kontrol/saklar", statusStr)) {
        Serial.println("=> SUKSES: Data string terupdate di Firebase!");
        
        // Kirim Status Integer (1 atau 0) ke path /kontrol/led_relay_status
        Firebase.RTDB.setInt(&fbdo, "/kontrol/led_relay_status", (statusSaklar == LOW) ? 1 : 0);
        
        statusTerakhir = statusSaklar;
      } else {
        Serial.print("=> GAGAL kirim data. Alasan: ");
        Serial.println(fbdo.errorReason());
      }
      
    } else {
      Serial.println("Firebase belum ready!");
      Serial.print("Error Firebase: ");
      Serial.println(fbdo.errorReason());
    }
  }

  // 4. Menampilkan Status Realtime di OLED
  display.clearDisplay();
  display.setCursor(0, 0);

  // Status WiFi
  display.print("WiFi : ");

  if (WiFi.status() == WL_CONNECTED) {
    display.println("Connected");
  } else {
    display.println("Disconnected");
  }

  // Status Firebase
  display.print("FB   : ");

  if (WiFi.status() == WL_CONNECTED && Firebase.ready()) {
    display.println("Connected");
  } else {
    display.println("Disconnected");
  }

  display.println("----------------");

  // RSSI WiFi
  if (WiFi.status() == WL_CONNECTED) {
    display.print("RSSI : ");
    display.println(WiFi.RSSI());
  }

  display.print("SW   : ");
  display.println(statusSaklar == LOW ? "ON" : "OFF");

  display.print("LED  : ");
  display.println(statusSaklar == LOW ? "ON" : "OFF");

  display.print("RLY  : ");
  display.println(statusSaklar == LOW ? "ON" : "OFF");

  display.display();

  delay(200); 
}