#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "time.h" // Library bawaan untuk sinkronisasi waktu internetlea

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
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Konfigurasi WiFi (mengambil nilai dari macro .env yang disuntikkan)
#ifndef SECRET_WIFI_SSID
#define SECRET_WIFI_SSID "DefaultWiFi"
#endif
#ifndef SECRET_WIFI_PASS
#define SECRET_WIFI_PASS "DefaultPassword"
#endif

const char *ssid = SECRET_WIFI_SSID;
const char *password = SECRET_WIFI_PASS;

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
Preferences preferences;

// Definisi Pin
int led = 25;
int saklar = 26;
int relay = 27;

const int SAKLAR_PRESSED_LEVEL = LOW;
const unsigned long SAKLAR_DEBOUNCE_MS = 60;
const int RELAY_ON_LEVEL = HIGH;
const int RELAY_OFF_LEVEL = LOW;

int statusSaklarTerakhir = HIGH;
int pembacaanSaklarTerakhir = HIGH;
unsigned long waktuSaklarBerubah = 0;
int statusSaklarFirebaseTerakhir = -1;

int statusLampuTerakhir = -1;
int statusPerintahFirebaseTerakhir = -1;

String statusApp = "OFF";

bool lampuNyala = false;

void setLampuHardware(bool nyala)
{
  digitalWrite(led, nyala ? HIGH : LOW);
  digitalWrite(relay, nyala ? RELAY_ON_LEVEL : RELAY_OFF_LEVEL);
}

// Fungsi untuk sinkronisasi waktu dengan server NTP Google/Indonesia
void sinkronisasiWaktu()
{
  Serial.println("Sinkronisasi waktu dimulai...");

  configTime(25200, 0, "pool.ntp.org", "time.nist.gov");

  time_t now = time(nullptr);

  int retry = 0;
  while (now < 100000 && retry < 20)
  {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
    retry++;
  }

  Serial.println();

  if (now > 100000)
  {
    Serial.println("Waktu berhasil disinkronkan!");
    Serial.println(ctime(&now));
  }
  else
  {
    Serial.println("Gagal sinkronisasi waktu!");
  }
}

void cekWiFi()
{
  static unsigned long lastReconnectAttempt = 0;
  static bool wasConnected = true;

  if (WiFi.status() == WL_CONNECTED)
  {
    if (!wasConnected)
    {
      Serial.println("WiFi berhasil terhubung kembali!");
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
      wasConnected = true;
    }
  }
  else
  {
    if (wasConnected)
    {
      Serial.println("WiFi DISCONNECTED!");
      wasConnected = false;
      lastReconnectAttempt = millis(); // Start cooldown timer
    }

    // Try to reconnect every 30 seconds
    if (millis() - lastReconnectAttempt > 30000)
    {
      lastReconnectAttempt = millis();
      Serial.println("Mencoba menghubungkan kembali ke WiFi...");
      WiFi.disconnect();
      WiFi.begin(ssid, password);
    }
  }
}

void setup()
{
  Serial.begin(115200);

  delay(2000);
  Serial.println();
  Serial.println("=================================");
  Serial.println("ESP32 STARTING...");
  Serial.println("=================================");

  pinMode(led, OUTPUT);
  pinMode(saklar, INPUT_PULLUP);
  pinMode(relay, OUTPUT);

  // 1. Baca status lampu terakhir dari memori lokal (Preferences)
  preferences.begin("smart-light", false);
  lampuNyala = preferences.getBool("lampuState", false); // Default OFF jika belum ada data
  preferences.end();

  // Set kondisi awal hardware berdasarkan data lokal secara instan!
  setLampuHardware(lampuNyala);
  statusSaklarTerakhir = digitalRead(saklar);
  pembacaanSaklarTerakhir = statusSaklarTerakhir;
  statusApp = lampuNyala ? "ON" : "OFF";
  statusLampuTerakhir = lampuNyala ? 1 : 0;
  statusPerintahFirebaseTerakhir = lampuNyala ? 1 : 0;

  // Inisialisasi OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  {
    Serial.println(F("OLED gagal diinisialisasi"));
    for (;;)
      ;
  }

  // Tampilan OLED: Sistem Siap (Offline Mode)
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("SISTEM SIAP!");
  display.println("Mode: Offline");
  display.display();

  // Memulai WiFi secara ASINKRON (Tidak Menunggu Koneksi!)
  Serial.println("Memulai koneksi WiFi secara asinkron...");
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);

  // Konfigurasi parameter Firebase
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  config.signer.test_mode = true;
  config.token_status_callback = tokenStatusCallback;
  Firebase.reconnectWiFi(true);
}

void loop()
{
  // Cek koneksi WiFi
  cekWiFi();

  // 1. Baca saklar manual dengan debounce.
  int pembacaanSaklar = digitalRead(saklar);
  bool posisiSaklarBerubah = false;

  if (pembacaanSaklar != pembacaanSaklarTerakhir)
  {
    waktuSaklarBerubah = millis();
    pembacaanSaklarTerakhir = pembacaanSaklar;
  }

  if ((millis() - waktuSaklarBerubah) > SAKLAR_DEBOUNCE_MS &&
      pembacaanSaklar != statusSaklarTerakhir)
  {
    statusSaklarTerakhir = pembacaanSaklar;
    posisiSaklarBerubah = true;
  }

  int statusSaklar = statusSaklarTerakhir;

  // 2. Saklar fisik sebagai pemicu toggle fleksibel (Lokal + Simpan Memory)
  if (posisiSaklarBerubah)
  {
    lampuNyala = !lampuNyala;
    statusApp = lampuNyala ? "ON" : "OFF";

    Serial.print("Saklar manual memicu toggle fleksibel: ");
    Serial.println(statusApp);

    // Simpan ke memori lokal secara instan
    preferences.begin("smart-light", false);
    preferences.putBool("lampuState", lampuNyala);
    preferences.end();

    if (Firebase.ready())
    {
      Firebase.RTDB.setString(&fbdo, "/kontrol/app", statusApp);
      Firebase.RTDB.setInt(&fbdo, "/kontrol/led_relay_status", lampuNyala ? 1 : 0);
      statusPerintahFirebaseTerakhir = lampuNyala ? 1 : 0;
      statusLampuTerakhir = lampuNyala ? 1 : 0;
    }
  }

  // Cek Koneksi WiFi & Inisialisasi Firebase secara Asinkron (Background)
  static bool firebaseInitialized = false;
  static bool timeSynced = false;
  static bool firstSyncDone = false;

  if (WiFi.status() == WL_CONNECTED)
  {
    if (!firebaseInitialized)
    {
      if (!timeSynced)
      {
        Serial.println("WiFi terhubung. Sinkronisasi waktu...");
        configTime(25200, 0, "pool.ntp.org", "time.nist.gov");
        timeSynced = true;
      }

      Serial.println("Memulai Firebase Client...");
      Firebase.begin(&config, &auth);
      firebaseInitialized = true;
    }

    // Sinkronisasi data awal satu arah (Firebase ke Lokal) ketika Firebase pertama kali READY
    if (Firebase.ready() && !firstSyncDone)
    {
      Serial.println("Menyelaraskan data awal dengan Firebase...");
      if (Firebase.RTDB.getInt(&fbdo, "/kontrol/led_relay_status"))
      {
        int statusFirebase = fbdo.intData();
        if (statusFirebase == 0 || statusFirebase == 1)
        {
          lampuNyala = (statusFirebase == 1);
          statusPerintahFirebaseTerakhir = statusFirebase;
          statusLampuTerakhir = statusFirebase;
          statusApp = lampuNyala ? "ON" : "OFF";

          // Simpan ke memori lokal
          preferences.begin("smart-light", false);
          preferences.putBool("lampuState", lampuNyala);
          preferences.end();

          firstSyncDone = true;
          Serial.print("Sinkronisasi Firebase selesai! Status lampu: ");
          Serial.println(statusApp);
        }
      }
    }
  }
  else
  {
    // Reset flags ketika koneksi putus
    firebaseInitialized = false;
    timeSynced = false;
    firstSyncDone = false;
  }

  // Pengecekan perintah aplikasi (Firebase) setiap 1 detik ketika saklar diam dan sudah sinkronisasi
  if (Firebase.ready() && firstSyncDone)
  {
    static unsigned long lastQueryMillis = 0;
    if (millis() - lastQueryMillis > 1000)
    {
      lastQueryMillis = millis();
      if (Firebase.RTDB.getInt(&fbdo, "/kontrol/led_relay_status"))
      {
        int statusFirebase = fbdo.intData();

        if ((statusFirebase == 0 || statusFirebase == 1) &&
            statusFirebase != statusPerintahFirebaseTerakhir)
        {
          statusPerintahFirebaseTerakhir = statusFirebase;
          statusLampuTerakhir = statusFirebase;
          lampuNyala = statusFirebase == 1;
          statusApp = lampuNyala ? "ON" : "OFF";

          // Simpan ke memori lokal
          preferences.begin("smart-light", false);
          preferences.putBool("lampuState", lampuNyala);
          preferences.end();

          Serial.print("Perintah aplikasi diterima: ");
          Serial.println(statusApp);
        }
      }
    }
  }

  // Set hardware state
  setLampuHardware(lampuNyala);

  int statusLampuSekarang = lampuNyala ? 1 : 0;

  // 4. Kirim status saklar logis ke Firebase. Status ini mengikuti lampu,
  // bukan posisi fisik, agar aplikasi dan saklar memakai satu keadaan yang sama.
  if (statusLampuSekarang != statusSaklarFirebaseTerakhir && Firebase.ready())
  {
    String statusStr = lampuNyala ? "ON" : "OFF";
    Firebase.RTDB.setString(&fbdo, "/kontrol/saklar", statusStr);
    statusSaklarFirebaseTerakhir = statusLampuSekarang;
    Serial.println("Status saklar logis diperbarui di Firebase");
  }

  // Kirim status lampu ke Firebase jika tidak cocok (sinkronisasi darurat)
  if (statusLampuSekarang != statusLampuTerakhir && Firebase.ready() && firstSyncDone)
  {
    Firebase.RTDB.setInt(&fbdo, "/kontrol/led_relay_status", statusLampuSekarang);
    Firebase.RTDB.setString(&fbdo, "/kontrol/app", lampuNyala ? "ON" : "OFF");
    statusLampuTerakhir = statusLampuSekarang;
    statusPerintahFirebaseTerakhir = statusLampuSekarang;
    Serial.println("Status lampu disinkronkan ke Firebase");
  }

  // 5. Kirim Heartbeat ke Firebase setiap 5 detik (Alternating Boolean Toggle)
  static unsigned long lastHeartbeatMillis = 0;
  if (millis() - lastHeartbeatMillis > 5000)
  {
    lastHeartbeatMillis = millis();
    if (WiFi.status() == WL_CONNECTED && Firebase.ready())
    {
      static bool heartbeatState = false;
      heartbeatState = !heartbeatState;
      Firebase.RTDB.setBool(&fbdo, "/kontrol/heartbeat", heartbeatState);
    }
  }

  // OLED
  display.clearDisplay();
  display.setCursor(0, 0);

  display.print("WiFi : ");
  display.println(WiFi.status() == WL_CONNECTED ? "ON" : "OFF");

  display.print("FB   : ");
  display.println(Firebase.ready() ? "ON" : "OFF");

  display.println("----------------");

  display.print("SW : ");
  display.println(statusSaklar == LOW ? "ON" : "OFF");

  display.print("APP: ");
  display.println(statusApp);

  display.print("LAMP:");
  display.println(lampuNyala ? "ON" : "OFF");

  display.display();

  delay(200);
}
