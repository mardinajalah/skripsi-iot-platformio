#include <Arduino.h>
#include <WiFi.h>
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

// Definisi Pin
int led = 25;
int saklar = 26;
int relay = 27;

int statusSaklarTerakhir = HIGH;
int statusSaklarFirebaseTerakhir = -1;

int statusLampuTerakhir = -1;
int statusPerintahFirebaseTerakhir = -1;

String statusApp = "OFF";

bool lampuNyala = false;

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

  // Set kondisi awal hardware
  digitalWrite(relay, HIGH); // Active Low (HIGH = MATI)
  digitalWrite(led, LOW);
  statusSaklarTerakhir = digitalRead(saklar);

  // Inisialisasi OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  {
    Serial.println(F("OLED gagal diinisialisasi"));
    for (;;)
      ;
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

  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
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

  while (!Firebase.ready() && millis() - startTime < 30000)
  {
    Serial.print(".");
    delay(500);
  }

  Serial.println();

  if (Firebase.ready())
  {
    Serial.println("Firebase READY!");
  }
  else
  {
    Serial.println("Firebase TIDAK READY!");
  }

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("SISTEM SIAP!");
  display.display();
  delay(1500);
}

void loop()
{
  // Cek koneksi WiFi
  cekWiFi();

  // 1. baca saklar manual
  int statusSaklar = digitalRead(saklar);
  bool saklarAktif = statusSaklar == LOW;
  bool saklarBerubah = statusSaklar != statusSaklarTerakhir;

  // 2. saklar fisik menjadi prioritas utama.
  // Saat saklar ON, aplikasi dikunci dan perintah Firebase diabaikan.
  if (saklarBerubah)
  {
    lampuNyala = saklarAktif;
    statusSaklarTerakhir = statusSaklar;
    statusApp = lampuNyala ? "ON" : "OFF";

    Serial.print("Perintah saklar manual diterima: ");
    Serial.println(statusApp);

    if (Firebase.ready())
    {
      Firebase.RTDB.setString(&fbdo, "/kontrol/app", statusApp);
      Firebase.RTDB.setInt(&fbdo, "/kontrol/led_relay_status", lampuNyala ? 1 : 0);
      statusPerintahFirebaseTerakhir = lampuNyala ? 1 : 0;
    }
  }
  else if (saklarAktif)
  {
    lampuNyala = true;
    statusApp = "ON";

    if (Firebase.ready() && Firebase.RTDB.getInt(&fbdo, "/kontrol/led_relay_status"))
    {
      statusPerintahFirebaseTerakhir = fbdo.intData();
    }
  }
  else if (Firebase.ready())
  {
    // 3. saat saklar OFF, aplikasi boleh menjadi sumber perintah (cek setiap 1 detik).
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
          lampuNyala = statusFirebase == 1;
          statusApp = lampuNyala ? "ON" : "OFF";

          Serial.print("Perintah aplikasi diterima: ");
          Serial.println(statusApp);
        }
      }
    }
  }

  // relay active LOW

  if (lampuNyala)
  {
    digitalWrite(led, HIGH);
    digitalWrite(relay, LOW);
  }
  else
  {
    digitalWrite(led, LOW);

    digitalWrite(relay, HIGH);
  }

  // 4. kirim status sistem ke Firebase

  int statusLampuSekarang = lampuNyala ? 1 : 0;

  if ((statusSaklar != statusSaklarFirebaseTerakhir ||
       statusLampuSekarang != statusLampuTerakhir ||
       statusPerintahFirebaseTerakhir != statusLampuSekarang) &&
      Firebase.ready())
  {
    String statusStr = (statusSaklar == LOW) ? "ON" : "OFF";

    Firebase.RTDB.setString(&fbdo, "/kontrol/saklar", statusStr);

    Firebase.RTDB.setInt(&fbdo, "/kontrol/led_relay_status", statusLampuSekarang);
    Firebase.RTDB.setString(&fbdo, "/kontrol/app", lampuNyala ? "ON" : "OFF");

    statusSaklarFirebaseTerakhir = statusSaklar;

    statusLampuTerakhir = statusLampuSekarang;
    statusPerintahFirebaseTerakhir = statusLampuSekarang;

    Serial.println("Status Firebase diperbarui");
  }

  // 5. Kirim Heartbeat ke Firebase setiap 5 detik
  static unsigned long lastHeartbeatMillis = 0;
  if (millis() - lastHeartbeatMillis > 5000)
  {
    lastHeartbeatMillis = millis();
    if (WiFi.status() == WL_CONNECTED && Firebase.ready())
    {
      static int heartbeatCount = 0;
      heartbeatCount++;
      Firebase.RTDB.setInt(&fbdo, "/kontrol/heartbeat", heartbeatCount);
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
