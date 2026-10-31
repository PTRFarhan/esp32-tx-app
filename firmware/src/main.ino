#include <SPI.h>
#include <Wire.h>
#include <RF24.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>
#include <WiFi.h>
#include "esp_bt.h"
#include <WebServer.h>
#include <Update.h>
#include <DNSServer.h>
#include "esp_wifi.h"
#include <NimBLEDevice.h>      // NimBLE: tumpukan Bluetooth jauh lebih kecil dari Bluedroid, supaya firmware muat di dua slot OTA

// ================= EEPROM =================
#define EEPROM_SIZE        800  // kalibrasi stick, konektivitas, suara, model, rate, endpoint, nama model, dst (kelipatan 50 untuk pemulihan lewat Bluetooth)

#define EEPROM_ADDR_BRIGHT   0
#define EEPROM_ADDR_REVERSE  1
#define EEPROM_ADDR_TRIM     7
#define EEPROM_ADDR_OUTPUT   13   // dipakai untuk simpan THEME (0=LIGHT, 1=DARK)
#define EEPROM_ADDR_CAL      17   // 4 stick x 3 nilai x 2 byte = 24 byte (alamat 17 sampai 40)
#define EEPROM_ADDR_CALMAGIC 41   // penanda data kalibrasi valid (0xCA)
#define EEPROM_ADDR_WIFI     42   // 1 = WiFi ON
#define EEPROM_ADDR_BT       43   // 1 = Bluetooth ON
#define EEPROM_ADDR_VOL      44   // volume suara 1..10
#define EEPROM_ADDR_SNDMASK  45   // bit 0..7 = suara aktif (lihat SND_*)
#define EEPROM_ADDR_SNDMASK2 46   // bit 8..9 = suara aktif, ditambah penanda 0x80 (format baru)
#define EEPROM_ADDR_MAGIC    47   // 0xA7 = data model sudah dibuat
#define EEPROM_ADDR_MODEL    48   // model aktif (indeks)
#define EEPROM_ADDR_MCOUNT   49   // jumlah model 1..MAX_MODELS
#define EEPROM_ADDR_NRFPWR   89   // daya pancar NRF 0..3 (MIN, LOW, HIGH, MAX), 0xFF = bawaan MAX
#define EEPROM_ADDR_BUZTYPE  90   // jenis buzzer 0 = aktif, 1 = pasif (0xFF = bawaan pasif)
#define EEPROM_ADDR_DASH     92   // gaya dashboard 0..4 (0xFF = 0)
#define EEPROM_ADDR_TRTRIG   93   // channel pemicu RESET timer: 0 = manual, 1..8 = CH1..CH8 (0xFF = belum diatur)
#define EEPROM_ADDR_TRDIR    94   // 0 = reset saat di atas batas (BATAS yang sama dengan pemicu jalan), 1 = di bawah
#define EEPROM_ADDR_BUZTONE  91   // nada buzzer pasif, satuan 100 Hz (10..50), 0xFF = bawaan 27
#define EEPROM_ADDR_MNMAGIC  88   // 0xB5 = jumlah dan nama model sudah dibuat
#define EEPROM_ADDR_DR       50   // dual rate CH1..CH4 (4 byte)
#define EEPROM_ADDR_EXPO     54   // expo CH1..CH4 (4 byte)
#define EEPROM_ADDR_EPLO     58   // endpoint bawah CH1..CH6 (6 byte)
#define EEPROM_ADDR_EPHI     64   // endpoint atas CH1..CH6 (6 byte)
#define EEPROM_ADDR_FS       70   // failsafe HP CH1..CH8 (8 byte)
#define EEPROM_ADDR_MIXT     78   // tipe mixing
#define EEPROM_ADDR_MIXW     79   // bobot mixing
#define EEPROM_ADDR_TMIN     80   // timer hitung mundur (menit, 0 = mati)
#define EEPROM_ADDR_TAUTO    81   // lama (versi sebelumnya): 1 = timer otomatis dengan gas CH3. Sekarang dibaca hanya untuk migrasi
#define EEPROM_ADDR_MIXA     83   // channel mixing pertama 1..6
#define EEPROM_ADDR_MIXB     84   // channel mixing kedua 1..6
#define EEPROM_ADDR_TTRIG    85   // channel pemicu timer: 0 = manual, 1..8 = CH1..CH8 (0xFF = belum diatur)
#define EEPROM_ADDR_TTHR     86   // batas pemicu timer, persen
#define EEPROM_ADDR_TDIR     87   // 0 = jalan saat di atas batas, 1 = saat di bawah batas
#define EEPROM_ADDR_BLOW     82   // batas alarm baterai (per 0.1 V)
#define EEPROM_ADDR_SLOTS    100  // 10 slot model x 40 byte (alamat 100 sampai 499)
#define EEPROM_ADDR_FMIXS    620  // 10 slot x 12 byte mix bebas (alamat 620 sampai 739)
#define EEPROM_ADDR_FMIXW    740  // salinan kerja mix bebas model aktif, 12 byte (740 sampai 751)
#define EEPROM_ADDR_MNAMES   500  // 10 nama model x 12 byte (alamat 500 sampai 619)
#define EEPROM_MAGIC         0xA7

// ================= OLED =================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
// Clock I2C 400 kHz. Pada 1 MHz data ke OLED sering korup (layar glitch).
// Kalau layar sudah bersih dan mau lebih cepat, coba 600000UL atau 800000UL.
#define OLED_I2C_CLOCK 800000UL   // 800 kHz: layar tergambar dua kali lebih cepat. Kalau layar bergaris atau kosong, kembalikan ke 400000UL
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1, OLED_I2C_CLOCK, OLED_I2C_CLOCK);

// ================= NRF24 =================
RF24 radio(4, 5);     // CE = 4, CSN = 5
const byte address[6] = "RADIO";

// ================= PIN =================
#define BUZZER 2

#define J1X 34
#define J1Y 35
#define J2X 32
#define J2Y 33

#define SW1 12
#define SW2 13
#define SW3 15   // <<< PIN UNTUK SENSOR TEGANGAN
// true kalau pin baterai termasuk ADC2 (tidak bisa dibaca saat WiFi aktif)
#define BATT_ON_ADC2 (SW3 == 0 || SW3 == 2 || SW3 == 4 || (SW3 >= 12 && SW3 <= 15) || (SW3 >= 25 && SW3 <= 27))

#define BTN 14

#define ENC_A 25
#define ENC_B 26
#define ENC_SW 27

// ================= PENGATURAN ANALOG =================
// Filter adaptif: saat stick diam noise ditahan kuat (output beku),
// saat stick bergerak filter otomatis dilepas sehingga respon tetap instan.
#define ANALOG_SAMPLES    16     // sampel per pembacaan (nilai tertinggi dan terendah dibuang)
#define ANALOG_DEADBAND   8      // output baru hanya berubah kalau selisih melewati nilai ini (skala 0..4095)
#define ANALOG_SNAP       48     // selisih sebesar ini dianggap gerakan nyata, filter dilepas penuh
#define ANALOG_ALPHA_MIN  0.06f  // kekuatan filter saat stick benar-benar diam
#define BATT_SAMPLES      16     // sampel rata-rata untuk baterai (tanpa filter lambat)

// ================= PENGATURAN ENCODER =================
// Jumlah transisi sinyal per 1 klik (detent). Encoder EC11 umumnya 4.
// Kalau 1 klik malah tidak bergerak, ganti jadi 2.
#define ENC_STEPS_PER_DETENT 4

// ================= PENGATURAN TOMBOL (BARU) =================

// Log tombol ke Serial Monitor (115200) untuk menelusuri tekanan ganda. Ubah ke 0 kalau tidak perlu.
#define DEBUG_INPUT 1
#if DEBUG_INPUT
  #define DBG(...) Serial.printf(__VA_ARGS__)
#else
  #define DBG(...)
#endif


// ================= STRUCT =================
struct TXPacket
{
    uint16_t ch[6];

    uint8_t switches;

    uint8_t mode;

    uint8_t outputMode;  // berisi status darkMode (dikirim, walau belum dipakai RX)

    // [BARU] CH7 dan CH8 ditaruh di BELAKANG, jadi RX lama tetap membaca 6 channel seperti biasa.
    // Posisi di paket: byte 16..17 = CH7, byte 18..19 = CH8 (uint16 little endian, 0..4095)
    uint16_t ch7;
    uint16_t ch8;
};
struct TelemetryPacket {
  float altitude;
  float voltage;
  float pitch;
  float roll;
};

// ================= LINK MONITOR =================
bool linkLost = false;           // status koneksi RX
unsigned long lastLinkOK = 0;    // waktu terakhir RX terhubung
unsigned long lastBeepTime = 0;  // untuk interval buzzer

#define LINK_TIMEOUT_MS 1000     // 1 detik tanpa RX → LOST
#define BEEP_INTERVAL   2000     // bunyi tiap 2 detik
#define BEEP_DURATION   120      // lama bunyi (ms)


TXPacket pkt;
TelemetryPacket telemetry;
int linkBars = 5;

// ================= MODE =================
#define MODE_DASHBOARD 0
#define MODE_BRIGHTNESS 1
#define MODE_CHANNEL 2
#define MODE_REVERSE 3
#define MODE_TRIM 4
// MODE_TELEMETRY 5 dihapus
#define MODE_THEME 6
#define MODE_CALIB 7    // kalibrasi stick
#define MODE_LIST 8     // [BARU] daftar menu
#define MODE_CONN 9     // [BARU] konektivitas
#define MODE_SOUND 10   // [BARU] suara
#define MODE_PARAM 11   // [BARU] menu parameter umum (model, rate, endpoint, dst), halaman di paramPage


uint8_t currentMode = MODE_DASHBOARD;

// ================= DAFTAR MENU (BARU) =================
// Halaman menu parameter. Di LIST_MODES ditulis 100 + nomor halaman.
enum { PG_MODEL = 0, PG_RATE, PG_LIMIT, PG_FAIL, PG_MIX, PG_FMIX, PG_NRF, PG_BUZ, PG_DASH, PG_ANA, PG_TIMER, PG_BATT, PG_OTA, PG_COUNT };
#define LIST_COUNT 21
static const uint8_t LIST_MODES[LIST_COUNT] = {
  MODE_BRIGHTNESS, MODE_CHANNEL, MODE_REVERSE, MODE_TRIM, MODE_THEME, MODE_CALIB, MODE_CONN, MODE_SOUND,
  100 + PG_MODEL, 100 + PG_RATE, 100 + PG_LIMIT, 100 + PG_FAIL, 100 + PG_MIX, 100 + PG_FMIX, 100 + PG_NRF, 100 + PG_BUZ, 100 + PG_DASH, 100 + PG_ANA, 100 + PG_TIMER, 100 + PG_BATT, 100 + PG_OTA
};
static const char* const LIST_NAMES[LIST_COUNT] = {
  "BRIGHTNESS", "CHANNEL READOUT", "CHANNEL REVERSE", "CHANNEL TRIM", "THEME", "KALIBRASI JOYSTICK", "KONEKTIVITAS", "SUARA",
  "MODEL", "RATE DAN EXPO", "ENDPOINT", "FAILSAFE HP", "MIXING", "MIX BEBAS", "DAYA NRF", "NADA BUZZER", "TAMPILAN DASHBOARD", "ANALISIS SINYAL", "TIMER", "ALARM BATERAI", "UPDATE FIRMWARE"
};
uint8_t paramPage = 0;   // halaman menu parameter yang sedang dibuka
int paramIndex = 0;      // kursor di halaman
int paramEdit = 0;       // nilai yang sedang diedit
bool timerVisible();     // didefinisikan di bagian model dan timer
int timerShownSec();
int listIndex = 0;   // posisi kursor di daftar menu (diingat saat kembali dari submenu)

// ================= KONEKTIVITAS (WiFi, Bluetooth, sumber input) =================
#define WIFI_AP_SSID   "ESP32 TX"
#define WIFI_AP_PASS   "Farhan2005"   // minimal 8 karakter
#define WIFI_AP_CH     11            // kanal 11 (2.462 GHz): tidak menimpa kanal iklan Bluetooth (2.402, 2.426, 2.480 GHz) dan jauh dari NRF24 kanal 110 (2.510 GHz)
#define BLE_NAME       "ESP32 TX"
#define FW_VERSION     "4.4"
#define BT_TIMEOUT_MS  500           // mode HP: kalau 500 ms tidak ada data dari HP, pakai FAILSAFE

#define INPUT_REMOT 0
#define INPUT_HP    1

// Nilai FAILSAFE saat mode HP tapi data HP putus (skala 0..4095, sebelum reverse dan trim).
// CH3 = 0 dianggap throttle minimum. Kalau throttle kamu di channel lain, ubah di sini.
static const uint16_t BT_FAILSAFE[8] = {2048, 2048, 0, 2048, 0, 0, 2048, 2048};

// Pilihan pengguna (disimpan di EEPROM kecuali sumber input)
bool wifiOn = false;
bool btOn = false;
// WiFi dan Bluetooth saling mengunci: salah satu ON, yang lain ditolak.
// Mode update firmware (otaMode) memakai WiFi sementara tanpa mengubah pilihan tersimpan.
volatile bool otaMode = false;        // layar dan WiFi khusus update firmware
volatile bool otaRequest = false;     // permintaan masuk mode update (dari menu atau aplikasi)
volatile uint8_t otaState = 0;        // 0 menunggu, 1 menerima, 2 selesai, 3 gagal
volatile uint32_t otaBytes = 0;
volatile uint32_t otaTotal = 0;       // 0 kalau ukuran tidak diketahui
char otaMsg[40] = "";
// Update firmware lewat Bluetooth: data biner masuk ke ring buffer, ditulis ke flash oleh TaskConn
#define OTA_RING 16384
volatile bool otaBle = false;          // update lewat Bluetooth (tanpa WiFi)
volatile bool bleRaw = false;          // true: semua tulisan dari HP adalah data firmware, bukan perintah
static uint8_t otaRing[OTA_RING];
volatile uint32_t otaHead = 0, otaTail = 0;    // penunjuk naik terus, indeks = nilai % OTA_RING
volatile uint32_t otaLastRx = 0;       // millis() data terakhir
volatile uint32_t otaFailAt = 0;
static uint32_t otaLastAck = 0;        // byte terakhir yang sudah dikabarkan ke aplikasi
volatile uint32_t otaRestartAt = 0;   // millis() saat remot harus restart (0 = tidak)
uint8_t inputSrc = INPUT_REMOT;      // selalu REMOT saat dinyalakan, demi keamanan

// Status nyata, diatur TaskConn
volatile bool wifiActive = false;
volatile bool btActive = false;
volatile bool btConnected = false;
volatile bool hpDataOk = false;      // mode HP dan data HP masih segar
volatile bool battFrozen = false;    // baterai tidak bisa dibaca (pin ADC2 saat WiFi aktif)

// Data channel dari HP (0..4095), diisi callback Bluetooth
volatile uint16_t btCh[8] = {2048, 2048, 0, 2048, 0, 0, 2048, 2048};
volatile uint32_t btLastRxMs = 0;
volatile uint32_t btRxCount = 0;
portMUX_TYPE btMux = portMUX_INITIALIZER_UNLOCKED;

// Kursor dan nilai edit di menu Konektivitas
int connIndex = 0;     // 0 = WiFi, 1 = Bluetooth, 2 = Input
int connEdit = 0;

// ================= SUARA =================
// Suara dimainkan task sendiri lewat antrean, jadi layar dan tombol tidak pernah tertahan.
enum { SND_DISCONNECT = 0, SND_BOOT, SND_MENU, SND_SCROLL, SND_CLICK, SND_BT_CONN, SND_WIFI_CONN, SND_RXLOST, SND_LOWBATT, SND_TIMER, SND_COUNT };
#define SND_ALL_MASK 0x3FF
#define SND_RAW      200        // beep polos (alarm), lamanya diberikan pemanggil
#define VOL_MAX      10
#define SOUND_ITEMS  11         // item menu Suara: 0 = volume, 1..10 = suara (urut sesuai SND_*)
uint8_t soundVol = VOL_MAX;     // 1..10
uint8_t buzPassive = 1;         // 1 = buzzer pasif (bernada), 0 = buzzer aktif (ON/OFF)
uint8_t buzTone = 27;           // nada buzzer pasif, satuan 100 Hz (10..50). 2.7 kHz = resonansi umum
uint8_t dashStyle = 0;          // gaya dashboard 0..4
uint8_t nrfPower = 3;           // daya pancar NRF: 0 = MIN (-18 dBm), 1 = LOW (-12), 2 = HIGH (-6), 3 = MAX (0), 4 = OFF (radio dimatikan). Semua tersimpan, termasuk OFF
uint16_t soundMask = SND_ALL_MASK;   // bit i = suara i aktif
void soundMaskSave() {
  EEPROM.write(EEPROM_ADDR_SNDMASK, (uint8_t)(soundMask & 0xFF));
  EEPROM.write(EEPROM_ADDR_SNDMASK2, (uint8_t)(0x80 | ((soundMask >> 8) & 3)));
}
int soundIndex = 0;             // kursor menu Suara
int soundEdit = 0;              // nilai yang sedang diedit

// Statistik untuk halaman web
volatile uint16_t stickCal[4] = {2048, 2048, 2048, 2048};
volatile uint16_t txRate = 0;        // paket NRF per detik
volatile uint8_t  ackRate = 0;       // persen paket yang di-ACK RX

// ================= OLED BRIGHTNESS =================
#define BRIGHTNESS_MAX 10
int oledBrightness = 128;
// Level brightness 0..10 disimpan sendiri sebagai sumber nilai asli.
int brightnessLevel = 5;
int lastBrightnessSaved = -1;   // level terakhir yang sudah ditulis ke EEPROM

// ================= KALIBRASI STICK =================
// Urutan indeks: 0 = J1X, 1 = J1Y, 2 = J2X, 3 = J2Y
// Nilai bawaan (0, 2048, 4095) berarti tanpa koreksi sampai kalibrasi dilakukan.
#define CAL_END_MARGIN 25   // ujung dianggap penuh sedikit sebelum batas fisik agar pasti mentok
volatile uint16_t rawStick[4] = {2048, 2048, 2048, 2048};   // nilai ADC bersih sebelum kalibrasi
uint16_t calMin[4]    = {0, 0, 0, 0};
uint16_t calCenter[4] = {2048, 2048, 2048, 2048};
uint16_t calMax[4]    = {4095, 4095, 4095, 4095};

// ================= ENCODER =================
volatile int encoderPos = 5; // default brightness 0..10

int menuEncoder = 0;   // encoder khusus menu reverse

// state decoder encoder
volatile int8_t  encSub = 0;          // akumulator transisi menuju 1 klik
volatile uint8_t encLast = 0;         // state A/B terakhir
volatile uint32_t encLastMicros = 0;  // waktu transisi terakhir
static const int8_t ENC_TABLE[16] = {
   0, +1, -1,  0,
  -1,  0,  0, +1,
  +1,  0,  0, -1,
   0, -1, +1,  0
};

// ===== CHANNEL REVERSE =====
bool channelReverse[6] = {false, false, false, false, false, false};
int reverseIndex = 0;

int8_t channelTrim[6] = {0,0,0,0,0,0};
int trimIndex = 0;

// ===== MODEL, RATE, EXPO, ENDPOINT, MIXING, FAILSAFE (semua ikut model aktif) =====
#define MAX_MODELS 10          // jumlah model maksimum (nama dari aplikasi)
#define MODEL_BLOB 40
#define MODEL_NAME_MAX 9       // panjang nama, muat di layar OLED
uint8_t modelCount = 5;                            // jumlah model yang ada (bertambah lewat aplikasi)
char modelName[MAX_MODELS][12] = {"PLANE", "BOAT", "ROVER", "UNIVERSAL", "MODEL 5"};
uint8_t dualRate[4]   = {100, 100, 100, 100};      // 30..100 %
uint8_t expoAmt[4]    = {0, 0, 0, 0};              // 0..100 %
uint8_t epLo[6]       = {100, 100, 100, 100, 100, 100};   // 20..120 %
uint8_t epHi[6]       = {100, 100, 100, 100, 100, 100};
uint8_t failsafePct[8] = {50, 50, 0, 50, 0, 0, 50, 50};   // nilai saat data HP putus, 0..100 % (CH3 = 0 = throttle minimum)
uint8_t mixType = 0;                               // 0 = OFF, 1 = ELEVON (CH1,CH2), 2 = V-TAIL (CH2,CH4)
uint8_t mixWeight = 50;                            // 10..100 %
uint8_t fmSrc[4] = {0, 0, 0, 0};                 // mix bebas: sumber 0 = mati, 1..8 = CH1..CH8
uint8_t fmDst[4] = {1, 1, 1, 1};                 // mix bebas: tujuan 1..8
int8_t  fmW[4]   = {50, 50, 50, 50};             // mix bebas: bobot -100..+100 persen
// Analisis sinyal 2,4 GHz: NRF24 mendengarkan 126 kanal (2400 sampai 2525 MHz) dan mencatat energi yang terdengar
uint8_t anaView = 0;                              // 0 = spektrum, 1 = kepadatan kanal WiFi
volatile uint32_t anaTouch = 0;                   // terakhir halaman analisis atau aplikasi meminta data
uint8_t anaLevel[126];                            // aktivitas tiap kanal, 0..100
uint8_t anaPeak[126];                             // puncak yang turun pelan
uint16_t anaAcc[126];                             // aktivitas halus, 0..1000 (anaLevel = anaAcc / 10)
uint8_t anaHold[126];                             // tahan puncak sebelum turun
#define ANA_WF_ROWS 22
uint8_t anaWf[ANA_WF_ROWS][126];                  // riwayat (waterfall): tiap baris cuplikan aktivitas 0..3, baris 0 = paling baru
uint32_t anaWfLast = 0;                           // kapan baris terakhir ditambahkan
static bool anaActive() { return anaTouch != 0 && (uint32_t)(millis() - anaTouch) < 2000; }
static void anaStart() {
  if (!anaActive()) { memset(anaLevel, 0, sizeof(anaLevel)); memset(anaPeak, 0, sizeof(anaPeak)); memset(anaAcc, 0, sizeof(anaAcc)); memset(anaHold, 0, sizeof(anaHold)); memset(anaWf, 0, sizeof(anaWf)); anaWfLast = 0; }
  anaTouch = millis() ? millis() : 1;   // hindari 0 (dianggap "belum pernah disentuh")
}
uint8_t mixA = 1, mixB = 2;                        // channel yang dicampur (1..6). ELEVON: A = aileron, B = elevator. V-TAIL: A = elevator, B = rudder
uint8_t activeModel = 0;                           // 0..modelCount-1
// Pengaturan umum (tidak ikut model)
uint8_t timerCountdownMin = 0;                     // 0 = hanya stopwatch
uint8_t timerTrig = 0;                             // 0 = manual, 1..8 = jalan otomatis menurut nilai CH1..CH8
uint8_t timerRstTrig = 0;                          // 0 = manual (tombol RESET saja), 1..8 = reset otomatis menurut nilai CH1..CH8, dengan BATAS yang sama
uint8_t timerRstDir = 0;                           // 0 = reset saat channel di atas BATAS, 1 = di bawah
uint8_t timerThr = 10;                             // batas pemicu (persen dari rentang 0..4095)
uint8_t timerDir = 0;                              // 0 = jalan saat nilai di atas batas, 1 = saat di bawah batas
uint8_t battLowTenths = 64;                        // batas alarm baterai, per 0.1 V
// Status timer
volatile bool timerRun = false;
volatile uint32_t timerElapsedMs = 0;
volatile bool timerBeeped = false;
// Telemetri dari RX (tegangan baterai RX), diisi TaskInputs
volatile uint32_t telemMs = 0;

//================ THEME MODE (LIGHT / DARK) ================
bool darkMode = true;   // true = DARK (bg hitam, instrumen putih), false = LIGHT (bg putih, instrumen hitam)
int themeIndex = 1;     // 0 = LIGHT, 1 = DARK (posisi kursor menu)

bool reverseEditMode = false;
bool reverseBlinkState = true;
unsigned long lastBlink = 0;


// Decoder encoder memakai tabel transisi dan dipicu oleh kedua pin (A dan B).
// Transisi yang tidak valid (pantulan kontak) otomatis diabaikan, dan
// posisi baru berubah 1 setiap 1 klik penuh.
// ===== Tipe data tombol (harus di atas semua fungsi karena prototipe otomatis Arduino) =====
enum { KEY_NONE = 0, KEY_CLICK = 1, KEY_HOLD = 2 };

// Status tombol navigasi (variabel biasa, tanpa struct, supaya aman dari penyusun prototipe Arduino)
volatile bool navKey_raw = false;         // pembacaan terakhir (true = ditekan)
volatile bool navKey_stable = false;      // status setelah debounce
uint32_t navKey_rawChange = 0;            // waktu pembacaan mentah terakhir berubah
uint32_t navKey_pressStart = 0;           // awal tekanan (setelah pantulan)
volatile uint32_t navKey_pressEdge = 0;   // sentuhan pertama tekanan ini
bool navKey_tracking = false;             // tekanan ini sedang dihitung (KLIK atau HOLD)
bool navKey_waitRelease = false;          // HOLD sudah dijalankan, tunggu tombol dilepas


volatile uint32_t encStepMs = 0;   // waktu langkah encoder terakhir
// Riwayat 8 langkah encoder terakhir (waktu dan arah), dipakai untuk membatalkan goyangan knob saat ditekan
volatile uint32_t encHistT[8];
volatile int8_t   encHistD[8];
volatile uint8_t  encHistIdx = 0;
void IRAM_ATTR handleEncoder() {
  uint32_t nowUs = micros();

  // Kalau encoder lama diam, buang sisa transisi yang menggantung
  if (nowUs - encLastMicros > 100000UL) {
    encSub = 0;
  }
  encLastMicros = nowUs;

  uint8_t state = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
  int8_t d = ENC_TABLE[(encLast << 2) | state];
  encLast = state;

  if (d == 0) return;

  encSub += d;

  if (encSub >= ENC_STEPS_PER_DETENT) {
    encSub = 0;
    encoderPos++;
    encStepMs = millis();
    encHistT[encHistIdx & 7] = encStepMs; encHistD[encHistIdx & 7] = 1; encHistIdx++;
  } else if (encSub <= -ENC_STEPS_PER_DETENT) {
    encSub = 0;
    encoderPos--;
    encStepMs = millis();
    encHistT[encHistIdx & 7] = encStepMs; encHistD[encHistIdx & 7] = -1; encHistIdx++;
  }
}

// ================= ANALOG HELPER =================
// Rata-rata beberapa sampel
uint16_t readAnalogAvg(uint8_t pin, uint8_t samples) {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < samples; i++) {
    sum += analogRead(pin);
  }
  return (uint16_t)(sum / samples);
}

// Rata-rata dengan membuang sampel tertinggi dan terendah (menahan lonjakan acak ADC ESP32)
uint16_t readAnalogTrimmed(uint8_t pin, uint8_t samples) {
  uint32_t sum = 0;
  uint16_t mn = 4095, mx = 0;
  for (uint8_t i = 0; i < samples; i++) {
    uint16_t v = analogRead(pin);
    sum += v;
    if (v < mn) mn = v;
    if (v > mx) mx = v;
  }
  sum -= mn;
  sum -= mx;
  return (uint16_t)(sum / (samples - 2));
}

// Pembacaan stick adaptif:
//  1. Rata-rata 16 sampel tanpa nilai ekstrem.
//  2. Filter yang kekuatannya mengikuti kecepatan gerak. Selisih kecil (noise) disaring kuat,
//     selisih besar (stick digerakkan) langsung diikuti tanpa delay.
//  3. Output memakai histeresis, jadi saat stick diam nilainya tidak bergeser sama sekali.
uint16_t stableStickRead(uint8_t idx, uint8_t pin) {
  static float    filt[4];
  static uint16_t outv[4];
  static bool     ready[4] = {false, false, false, false};

  float r = (float)readAnalogTrimmed(pin, ANALOG_SAMPLES);
  rawStick[idx] = (uint16_t)r;   // dipakai menu kalibrasi

  if (!ready[idx]) {
    filt[idx] = r;
    outv[idx] = (uint16_t)r;
    ready[idx] = true;
    return outv[idx];
  }

  float d = r - filt[idx];
  float ad = (d < 0) ? -d : d;

  float alpha;
  if (ad >= ANALOG_SNAP) {
    alpha = 1.0f;                       // gerakan nyata: langsung ikut
  } else {
    float k = ad / (float)ANALOG_SNAP;  // 0..1
    alpha = ANALOG_ALPHA_MIN + (1.0f - ANALOG_ALPHA_MIN) * k * k;
  }
  filt[idx] += alpha * d;

  float od = filt[idx] - (float)outv[idx];
  if (od < 0) od = -od;

  // Ujung jangkauan (0 dan 4095) selalu langsung diikuti agar travel stick penuh
  if (od >= ANALOG_DEADBAND || filt[idx] <= 2.0f || filt[idx] >= 4093.0f) {
    outv[idx] = (uint16_t)(filt[idx] + 0.5f);
  }

  return outv[idx];
}

// ================= KALIBRASI: PEMETAAN & EEPROM =================
// Memetakan nilai ADC asli ke 0..4095 penuh, dengan titik tengah tepat di 2048.
// Sisi bawah: min..tengah menjadi 0..2048. Sisi atas: tengah..max menjadi 2048..4095.
uint16_t applyCal(uint8_t idx, uint16_t v) {
  int mn = (int)calMin[idx] + CAL_END_MARGIN;
  int c  = (int)calCenter[idx];
  int mx = (int)calMax[idx] - CAL_END_MARGIN;

  // Data tidak masuk akal, pakai nilai asli
  if (mn >= c - 50 || mx <= c + 50) return v;

  long out;
  if (v <= c) {
    out = (long)((int)v - mn) * 2048L / (c - mn);
  } else {
    out = 2048L + (long)((int)v - c) * 2047L / (mx - c);
  }

  if (out < 0) out = 0;
  if (out > 4095) out = 4095;
  return (uint16_t)out;
}

void saveCalibration() {
  for (int i = 0; i < 4; i++) {
    int base = EEPROM_ADDR_CAL + i * 6;
    EEPROM.writeUShort(base,     calMin[i]);
    EEPROM.writeUShort(base + 2, calCenter[i]);
    EEPROM.writeUShort(base + 4, calMax[i]);
  }
  EEPROM.write(EEPROM_ADDR_CALMAGIC, 0xCA);
  EEPROM.commit();
}

void loadCalibration() {
  if (EEPROM.read(EEPROM_ADDR_CALMAGIC) != 0xCA) return;   // belum pernah kalibrasi, pakai bawaan

  for (int i = 0; i < 4; i++) {
    int base = EEPROM_ADDR_CAL + i * 6;
    uint16_t mn = EEPROM.readUShort(base);
    uint16_t c  = EEPROM.readUShort(base + 2);
    uint16_t mx = EEPROM.readUShort(base + 4);

    if (mn < c && c < mx && mx <= 4095) {
      calMin[i] = mn;
      calCenter[i] = c;
      calMax[i] = mx;
    }
  }
}

// ================= BUZZER DRIVER (ACTIVE BUZZER) + MESIN SUARA =================
#ifndef ESP_ARDUINO_VERSION_MAJOR
#define ESP_ARDUINO_VERSION_MAJOR 2
#endif

// Volume pada active buzzer dibuat dengan PWM cepat (20 kHz, di atas pendengaran).
// Duty kecil = tegangan rata-rata kecil = lebih pelan. Seberapa terasa tergantung buzzer yang dipakai,
// ubah angka di tabel ini kalau selisih antar level kurang terasa. Level 10 = ON penuh seperti biasa.
// Active buzzer punya ambang tegangan: dengan PWM 20 kHz (diratakan) duty kecil membuatnya diam total.
// Karena itu PWM dibuat lambat (VOL_PWM_HZ) supaya osilator buzzer menyala PENUH selama bagian ON,
// dan pelan-kerasnya ditentukan oleh lamanya ON. Suara jadi agak "kasar" di volume kecil, itu wajar.
// Kalau volume 1 masih diam, naikkan angka kecil di tabel. Kalau terlalu keras, turunkan.
#define VOL_PWM_HZ 1000
static const uint8_t VOL_DUTY[VOL_MAX + 1] = {0, 30, 42, 56, 74, 96, 124, 156, 194, 232, 255};

// BUZZER PASIF: nada dibuat PWM pada frekuensi buzTone (kotak). Keras pelannya ditentukan lebar pulsa:
// duty 50% (128 dari 256) = paling keras, makin sempit makin pelan. Tabel dibuat mengikuti telinga
// (amplitudo dasar = sin(pi x duty)), jadi tiap level terasa naik rata dari sangat pelan (1) sampai keras penuh (10).
static const uint8_t VOL_DUTY_PASSIVE[VOL_MAX + 1] = {0, 3, 6, 10, 16, 25, 38, 56, 80, 108, 128};
static bool buzzPwm = false;
static uint16_t buzzPwmHz = 0;

void buzzPwmRelease() {
  if (!buzzPwm) return;
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(BUZZER, 0);
  ledcDetach(BUZZER);
#else
  ledcWrite(1, 0);
  ledcDetachPin(BUZZER);
#endif
  buzzPwm = false;
  buzzPwmHz = 0;
  pinMode(BUZZER, OUTPUT);
}

// Nyalakan PWM pada frekuensi hz dengan duty 8 bit. Frekuensi boleh berganti saat sudah menyala.
static void buzzToneAt(uint16_t hz, uint8_t duty) {
  if (!buzzPwm) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(BUZZER, hz, 8);
#else
    ledcSetup(1, hz, 8);
    ledcAttachPin(BUZZER, 1);
#endif
    buzzPwm = true;
    buzzPwmHz = hz;
  } else if (buzzPwmHz != hz) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcChangeFrequency(BUZZER, hz, 8);
#else
    ledcSetup(1, hz, 8);
#endif
    buzzPwmHz = hz;
  }
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(BUZZER, duty);
#else
  ledcWrite(1, duty);
#endif
}

void buzzOn(uint8_t level) {
  if (level < 1) level = 1;
  if (level > VOL_MAX) level = VOL_MAX;
  if (buzPassive) {                  // pasif: nada pada buzTone, volume lewat lebar pulsa
    buzzToneAt((uint16_t)buzTone * 100, VOL_DUTY_PASSIVE[level]);
    return;
  }
  if (level >= VOL_MAX) {            // aktif, volume penuh: ON/OFF digital biasa
    buzzPwmRelease();
    digitalWrite(BUZZER, HIGH);
    return;
  }
  buzzToneAt(VOL_PWM_HZ, VOL_DUTY[level]);   // aktif: PWM lambat supaya osilator buzzer menyala penuh
}

void buzzOff() {
  buzzPwmRelease();
  digitalWrite(BUZZER, LOW);
}

// Pola ritme untuk active buzzer. Format: {lama ON ms, lama OFF ms}
struct BeepStep { uint16_t onMs; uint16_t offMs; };

static const BeepStep P_DISCONNECT[] = {{200, 70}, {90, 0}};                    // panjang lalu pendek (turun)
static const BeepStep P_BOOT[]       = {{70, 90}, {70, 70}, {70, 50}, {200, 0}}; // contoh singkat untuk menu (booting asli lebih panjang)
static const BeepStep P_MENU[]       = {{40, 0}};
static const BeepStep P_SCROLL[]     = {{6, 0}};
static const BeepStep P_CLICK[]      = {{16, 0}};
static const BeepStep P_BT[]         = {{70, 60}, {70, 60}, {160, 0}};            // dua pendek, satu panjang
static const BeepStep P_WIFI[]       = {{50, 45}, {50, 45}, {50, 45}, {130, 0}};  // tiga cepat, satu panjang
static const BeepStep P_RXLOST[]     = {{BEEP_DURATION, 0}};                       // alarm sinyal RX hilang (contoh)
static const BeepStep P_LOWBATT[]    = {{120, 0}};                                 // alarm baterai habis (contoh)
static const BeepStep P_TIMER[]      = {{150, 100}, {150, 100}, {300, 0}};        // timer selesai

struct SndPattern { const BeepStep* s; uint8_t n; };
static const SndPattern SND_PATTERNS[SND_COUNT] = {
  {P_DISCONNECT, 2}, {P_BOOT, 4}, {P_MENU, 1}, {P_SCROLL, 1}, {P_CLICK, 1}, {P_BT, 3}, {P_WIFI, 4}, {P_RXLOST, 1}, {P_LOWBATT, 1}, {P_TIMER, 3}
};

QueueHandle_t sndQ = NULL;
static uint32_t sndLastScroll = 0;

// Isi antrean: bit 24..31 = jenis suara (atau SND_RAW), bit 16..23 = volume (0 = pakai volume sekarang), bit 0..15 = lama (SND_RAW)
void sndEnqueue(uint8_t id, uint8_t vol, uint16_t arg) {
  if (sndQ == NULL) return;
  uint32_t ev = ((uint32_t)id << 24) | ((uint32_t)vol << 16) | arg;
  xQueueSend(sndQ, &ev, 0);
}

// Mainkan suara (tidak menunggu). Diabaikan kalau suara itu dimatikan di menu Suara.
void sndPlay(uint8_t id) {
  if (id >= SND_COUNT || !(soundMask & (1 << id))) return;
  if (id == SND_SCROLL) {            // putaran cepat tidak boleh menumpuk antrean
    uint32_t n = millis();
    if (n - sndLastScroll < 30 || (sndQ != NULL && uxQueueMessagesWaiting(sndQ) > 0)) return;
    sndLastScroll = n;
  }
  sndEnqueue(id, 0, 0);
}

// Contoh suara di menu: tetap berbunyi walau suara itu dimatikan, volume bisa ditentukan (0 = volume sekarang)
void sndPreview(uint8_t id, uint8_t vol) {
  if (id >= SND_COUNT) return;
  if (sndQ != NULL && uxQueueMessagesWaiting(sndQ) > 1) return;
  sndEnqueue(id, vol, 0);
}

void TaskSound(void *pvParameters) {
  (void) pvParameters;
  uint32_t ev;
  for (;;) {
    if (xQueueReceive(sndQ, &ev, portMAX_DELAY) != pdTRUE) continue;
    uint8_t id = (uint8_t)(ev >> 24);
    uint8_t vol = (uint8_t)((ev >> 16) & 0xFF);
    if (vol < 1 || vol > VOL_MAX) vol = soundVol;
    if (id == SND_RAW) {
      buzzOn(vol);
      vTaskDelay(pdMS_TO_TICKS(ev & 0xFFFF));
      buzzOff();
    } else if (id < SND_COUNT) {
      const SndPattern& pt = SND_PATTERNS[id];
      for (uint8_t i = 0; i < pt.n; i++) {
        buzzOn(vol);
        vTaskDelay(pdMS_TO_TICKS(pt.s[i].onMs));
        buzzOff();
        if (pt.s[i].offMs) vTaskDelay(pdMS_TO_TICKS(pt.s[i].offMs));
      }
    }
  }
}

// Beep polos (alarm sinyal hilang, baterai lemah, kalibrasi). Mengikuti volume, tidak ikut dimatikan per suara.
void buzzerBeep(uint16_t durationMs) {
  if (sndQ != NULL) { sndEnqueue(SND_RAW, 0, durationMs); return; }
  buzzOn(soundVol);
  vTaskDelay(pdMS_TO_TICKS(durationMs));
  buzzOff();
}

// ================= BOOT SOUND =================
// Jenis buzzer diatur dari menu NADA BUZZER (buzPassive): 0 = aktif (ON/OFF, jingle dari ritme),
// 1 = pasif (bernada, jingle melodi). Semua suara lain (beep menu, alarm) ikut jenis yang dipilih.

// Pola booting untuk active buzzer. Total 2000 ms. Format: {lama ON ms, lama OFF ms}
static const BeepStep BOOT_BEEPS[] = {
  {70,90}, {70,70}, {70,50},      // tiga ketukan makin rapat
  {0,80},
  {120,60}, {120,60},             // dua hentakan
  {420,80},                       // nada panjang
  {70,50}, {70,50}, {70,50},      // gema tiga ketukan
  {200,80}                        // penutup
};
#define BOOT_BEEPS_STEPS (sizeof(BOOT_BEEPS) / sizeof(BOOT_BEEPS[0]))

// Jingle bernada untuk buzzer pasif. Format: {frekuensi Hz (0 = diam), durasi ms}
#ifndef ESP_ARDUINO_VERSION_MAJOR
#define ESP_ARDUINO_VERSION_MAJOR 2
#endif

struct ToneStep { uint16_t freq; uint16_t ms; };

static const ToneStep BOOT_MELODY[] = {
  {1047,120}, {1319,120}, {1568,120}, {2093,220},   // arpeggio naik C E G C
  {0,70},
  {1568,90},  {2093,90},  {2637,300},               // G C E
  {0,80},
  {2093,100}, {2637,100}, {3136,590}                // C E G panjang (penutup)
};
#define BOOT_MELODY_STEPS (sizeof(BOOT_MELODY) / sizeof(BOOT_MELODY[0]))

volatile bool bootSoundDone = false;

// Dijalankan di task sendiri agar timing suara tidak terganggu lama gambar OLED
void BootSoundTask(void *pvParameters) {
  (void) pvParameters;

  bool bootOn = (soundMask & (1 << SND_BOOT)) != 0;
  if (buzPassive) {
    // Buzzer pasif: jingle bernada. Seluruh nada ikut bergeser mengikuti pengaturan NADA BUZZER, volume lewat lebar pulsa.
    for (size_t i = 0; i < BOOT_MELODY_STEPS; i++) {
      uint16_t f  = BOOT_MELODY[i].freq;
      uint16_t ms = BOOT_MELODY[i].ms;
      if (f > 0 && bootOn) {
        uint32_t hz = (uint32_t)f * buzTone * 100 / 2600;
        if (hz < 300) hz = 300;
        if (hz > 8000) hz = 8000;
        buzzToneAt((uint16_t)hz, VOL_DUTY_PASSIVE[soundVol]);
        vTaskDelay(pdMS_TO_TICKS(ms > 14 ? ms - 10 : ms));
        buzzOff();
        if (ms > 14) vTaskDelay(pdMS_TO_TICKS(10));
      } else {
        vTaskDelay(pdMS_TO_TICKS(ms));
      }
    }
    buzzOff();
  } else {
    // Buzzer aktif: ritme ON/OFF. Kalau suara dimatikan, animasi tetap berjalan 2 detik tanpa bunyi.
    for (size_t i = 0; i < BOOT_BEEPS_STEPS; i++) {
      if (BOOT_BEEPS[i].onMs > 0) {
        if (bootOn) buzzOn(soundVol);
        vTaskDelay(pdMS_TO_TICKS(BOOT_BEEPS[i].onMs));
        buzzOff();
      }
      if (BOOT_BEEPS[i].offMs > 0) vTaskDelay(pdMS_TO_TICKS(BOOT_BEEPS[i].offMs));
    }
    buzzOff();
  }

  bootSoundDone = true;
  vTaskDelete(NULL);
}

// ================= TAMPILAN (UI KIT) =================
// Potongan kecil yang dipakai semua layar supaya gayanya seragam:
// header bar, baris menu bersorot, penggulung, tombol geser (pill), meter, dan chip.
#define UI_ROW_Y0 13      // baris pertama daftar
#define UI_ROW_H  10      // tinggi tiap baris
#define UI_ROWS   5       // jumlah baris terlihat

static int uiTextW(const char* s) { return (int)strlen(s) * 6; }

static void uiPrintR(int xr, int y, const char* s) {      // rata kanan, xr = tepi kanan
  display.setCursor(xr - uiTextW(s), y);
  display.print(s);
}

static void uiPrintC(int xc, int y, const char* s) {      // rata tengah
  display.setCursor(xc - uiTextW(s) / 2, y);
  display.print(s);
}

// Header: bar putih dengan judul hitam, teks kanan opsional (nomor item, dsb)
static void uiHeader(const char* title, const char* right) {
  display.fillRect(0, 0, 128, 11, SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(4, 2);
  display.print(title);
  if (right && right[0]) uiPrintR(124, 2, right);
  display.setTextColor(SSD1306_WHITE);
}

// Nomor "3/15" untuk kanan header
static void uiCounter(char* out, int idx, int total) { sprintf(out, "%d/%d", idx + 1, total); }

// Baris pertama yang terlihat supaya kursor di tengah daftar (kecuali dekat ujung)
static int uiTop(int index, int total) {
  int top = index - UI_ROWS / 2;
  if (top > total - UI_ROWS) top = total - UI_ROWS;
  if (top < 0) top = 0;
  return top;
}

// Penggulung vertikal di tepi kanan, hanya muncul kalau daftar lebih panjang dari layar
static void uiScrollbar(int top, int total) {
  if (total <= UI_ROWS) return;
  const int y0 = UI_ROW_Y0, h = UI_ROWS * UI_ROW_H - 1;
  int th = h * UI_ROWS / total;
  if (th < 6) th = 6;
  int ty = y0 + (h - th) * top / (total - UI_ROWS);
  display.drawFastVLine(126, y0, h, SSD1306_WHITE);
  display.fillRect(125, ty, 3, th, SSD1306_WHITE);
}

// Latar satu baris. sel = terpilih (blok putih), edit = sedang diedit (hanya bingkai, nilainya diberi blok).
// Mengembalikan warna teks yang harus dipakai.
static uint16_t uiRow(int y, bool sel, bool edit, bool scroll) {
  int w = scroll ? 123 : 126;
  if (sel && !edit) { display.fillRoundRect(1, y, w, UI_ROW_H, 2, SSD1306_WHITE); return SSD1306_BLACK; }
  if (sel && edit)  display.drawRoundRect(1, y, w, UI_ROW_H, 2, SSD1306_WHITE);
  return SSD1306_WHITE;
}

// Tombol geser ON/OFF (17 x 8). xr = tepi kanan, y = atas baris
static void uiPill(int xr, int y, bool on, uint16_t fg) {
  uint16_t bg = (fg == SSD1306_WHITE) ? SSD1306_BLACK : SSD1306_WHITE;
  int x = xr - 17, py = y + 1;
  if (on) {
    display.fillRoundRect(x, py, 17, 8, 4, fg);
    display.fillCircle(x + 12, py + 4, 2, bg);
  } else {
    display.drawRoundRect(x, py, 17, 8, 4, fg);
    display.fillCircle(x + 5, py + 4, 2, fg);
  }
}

// Nilai teks rata kanan. Saat diedit nilainya diberi blok putih berpanah supaya jelas sedang aktif.
static void uiValue(int xr, int y, const char* tx, bool edit, uint16_t fg) {
  if (edit) {
    int w = uiTextW(tx) + 12;
    display.fillRoundRect(xr - w, y + 1, w + 2, UI_ROW_H - 2, 2, SSD1306_WHITE);
    display.fillTriangle(xr - w + 3, y + 5, xr - w + 6, y + 3, xr - w + 6, y + 7, SSD1306_BLACK);
    display.fillTriangle(xr - 1, y + 5, xr - 4, y + 3, xr - 4, y + 7, SSD1306_BLACK);
    display.setTextColor(SSD1306_BLACK);
    display.setCursor(xr - w + 8, y + 1);
    display.print(tx);
    display.setTextColor(SSD1306_WHITE);
  } else {
    display.setTextColor(fg);
    uiPrintR(xr, y + 1, tx);
    display.setTextColor(SSD1306_WHITE);
  }
}

// Meter horizontal berbingkai. frac 0..1000. center = nol di tengah (trim, channel)
static void uiMeter(int x, int y, int w, int h, int frac, bool center, uint16_t fg) {
  display.drawRect(x, y, w, h, fg);
  int iw = w - 2;
  if (frac < 0) frac = 0;
  if (frac > 1000) frac = 1000;
  if (center) {
    int mid = x + w / 2;
    int px = x + 1 + iw * frac / 1000;
    if (px >= mid) display.fillRect(mid, y + 1, px - mid + 1, h - 2, fg);
    else           display.fillRect(px, y + 1, mid - px, h - 2, fg);
    display.drawFastVLine(mid, y, h, fg);
  } else {
    display.fillRect(x + 1, y + 1, iw * frac / 1000, h - 2, fg);
  }
}

// Chip kecil berhuruf. filled = blok putih berhuruf hitam, selain itu bingkai
static void uiChip(int x, int y, int w, const char* tx, bool filled) {
  if (filled) {
    display.fillRoundRect(x, y, w, 10, 2, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
  } else {
    display.drawRoundRect(x, y, w, 10, 2, SSD1306_WHITE);
    display.setTextColor(SSD1306_WHITE);
  }
  display.setCursor(x + (w - uiTextW(tx)) / 2 + 1, y + 1);
  display.print(tx);
  display.setTextColor(SSD1306_WHITE);
}

// Tujuh titik kecil sebagai penanda status (hidup = terisi)
static void uiDot(int x, int y, bool on) {
  if (on) display.fillCircle(x, y, 2, SSD1306_WHITE);
  else    display.drawCircle(x, y, 2, SSD1306_WHITE);
}

// Animasi booting: pemancar RC naik dari bawah, antena memanjang, stik bergerak, gelombang radio memancar.
// Berjalan bersamaan dengan suara dan selesai saat suara selesai (2 detik).
static void bootDrawRadio(int dy, int ant, uint32_t t, int p) {
  const uint16_t W = SSD1306_WHITE, K = SSD1306_BLACK;
  // badan pemancar
  display.fillRoundRect(14, 22 + dy, 100, 36, 10, W);
  display.fillRoundRect(16, 24 + dy, 96, 32, 8, K);
  // pegangan kiri dan kanan
  display.fillRoundRect(8, 36 + dy, 14, 22, 6, W);
  display.fillRoundRect(106, 36 + dy, 14, 22, 6, W);
  // antena (panjangnya ant 0..16)
  if (ant > 0) {
    display.drawLine(96, 22 + dy, 100, 22 + dy - ant, W);
    display.drawLine(97, 22 + dy, 101, 22 + dy - ant, W);
    display.fillCircle(100, 22 + dy - ant, 2, W);
  }
  // layar kecil di tengah dengan batang sinyal berdenyut
  display.drawRoundRect(48, 28 + dy, 32, 16, 3, W);
  for (int i = 0; i < 5; i++) {
    int h = 2 + ((t / 90 + i * 3) % 6) * 2 / 1;
    if (h > 11) h = 11;
    display.fillRect(52 + i * 5, 41 + dy - h, 3, h, W);
  }
  // dua gimbal: sumur bulat, stik bergerak melingkar
  const int gx[2] = {34, 94};
  for (int g = 0; g < 2; g++) {
    display.drawCircle(gx[g], 42 + dy, 10, W);
    float a = (float)t / 260.0f + g * 2.1f;
    int sx = gx[g] + (int)(5.0f * cosf(a));
    int sy = 42 + dy + (int)(5.0f * sinf(a * (g ? 1.0f : 1.3f)));
    display.drawLine(gx[g], 42 + dy, sx, sy, W);
    display.fillCircle(sx, sy, 3, W);
  }
  // saklar kecil
  for (int i = 0; i < 3; i++) {
    int x = 50 + i * 10;
    bool up = ((t / 300 + i) & 1);
    display.drawFastVLine(x, 48 + dy + (up ? 0 : 2), 4, W);
    display.fillCircle(x, 48 + dy + (up ? 0 : 2), 1, W);
  }
  (void) p;
}

// Gelombang radio dari ujung antena: busur-busur makin besar
static void bootDrawWaves(uint32_t t, int tipY) {
  for (int k = 0; k < 3; k++) {
    int ph = (int)((t / 5 + k * 130) % 400);       // 0..399
    int r = 4 + ph / 22;                           // 4..22
    if (ph > 360) continue;
    for (int a = -75; a <= 15; a += 6) {
      float rad = (float)a * 0.0174533f;
      int x = 100 + (int)((float)r * cosf(rad));
      int y = tipY - (int)((float)r * sinf(-rad));
      if (x >= 0 && x < 128 && y >= 0 && y < 64) display.drawPixel(x, y, SSD1306_WHITE);
    }
  }
}

// Satu gambar animasi pada waktu t (ms sejak mulai, 0..2000)
void bootDrawFrame(uint32_t t) {
  int p = (int)(t * 100 / 2000);
  if (p > 100) p = 100;
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  // 0..25%: badan naik dari bawah. Lalu antena memanjang sampai 45%.
  int dy = 0;
  if (p < 25) { int q = 25 - p; dy = q * q / 8; }
  int ant = p < 25 ? 0 : (p >= 45 ? 14 : (p - 25) * 14 / 20);
  bootDrawRadio(dy, ant, t, p);
  if (p >= 45) bootDrawWaves(t, 22 + dy - ant);

  // Judul muncul di kiri atas: ESP32 huruf demi huruf, lalu TX dalam kotak putih
  static const char TTL[] = "ESP32";
  int nch = (p - 15) * 8 / 40;
  if (nch < 0) nch = 0;
  display.setTextSize(2);
  for (int i = 0; i < nch && i < 5; i++) { display.setCursor(2 + i * 12, 1); display.write(TTL[i]); }
  if (nch >= 6) {
    display.fillRoundRect(66, 0, 28, 16, 3, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setCursor(70, 1);
    display.print("TX");
    display.setTextColor(SSD1306_WHITE);
  }
  display.setTextSize(1);

  // Bar kemajuan tipis di dasar layar
  display.drawRect(14, 60, 100, 4, SSD1306_WHITE);
  display.fillRect(14, 60, p, 4, SSD1306_WHITE);
  display.display();
}

void bootAnimation() {
  bootSoundDone = false;
  xTaskCreatePinnedToCore(BootSoundTask, "BootSound", 2048, NULL, 1, NULL, 0);
  uint32_t t0 = millis();
  while (!bootSoundDone) {
    bootDrawFrame(millis() - t0);
    delay(20);
  }
}

// ================= BATTERY ICON (2S Li-ion) =================
// Ikon baterai 4 segmen. Range 2S Li-ion: 6.0V (kosong) sampai 8.4V (penuh). Mengembalikan lebar yang terpakai.
void drawBatteryIcon(int x, int y, float voltage) {
  float vc = voltage;
  if (vc < 6.0f) vc = 6.0f;
  if (vc > 8.4f) vc = 8.4f;
  int pct = (int)(((vc - 6.0f) / 2.4f) * 100.0f);
  int segs = (pct + 12) / 25;          // 0..4
  if (segs > 4) segs = 4;

  display.drawRoundRect(x, y, 17, 9, 2, SSD1306_WHITE);
  display.fillRect(x + 17, y + 3, 2, 3, SSD1306_WHITE);
  for (int i = 0; i < segs; i++) display.fillRect(x + 2 + i * 4, y + 2, 3, 5, SSD1306_WHITE);

  char b[8];
  snprintf(b, sizeof(b), "%.1fV", voltage);
  display.setTextSize(1);
  display.setCursor(x + 22, y + 1);
  display.print(b);
}

// ================= DASHBOARD =================
float batteryVoltage = 0;
bool lowBattAlarm = false;

static void dashStyle0() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  bool blinkOn = (millis() / 400) % 2;

  // ===== BAR STATUS (y 0..10) =====
  {
    char mb[4];
    sprintf(mb, "M%d", activeModel + 1);
    display.fillRoundRect(0, 0, 15, 10, 2, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setCursor(2, 1);
    display.print(mb);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(20, 1);
    display.print("FARHAN");

    // Penanda koneksi: W = WiFi, B = Bluetooth, HP = input dari HP
    int x = 60;
    if (wifiActive) { uiChip(x, 0, 9, "W", true); x += 11; }
    if (btActive)   { uiChip(x, 0, 9, "B", btConnected || blinkOn); x += 11; }
    if (inputSrc == INPUT_HP) { uiChip(x, 0, 15, "HP", hpDataOk || blinkOn); }

    // Kekuatan link NRF24
    for (int i = 0; i < 5; i++) {
      int h = 2 + i * 2;
      int bx = 106 + i * 4;
      if (i < linkBars) display.fillRect(bx, 10 - h, 3, h, SSD1306_WHITE);
      else              display.drawFastHLine(bx, 9, 3, SSD1306_WHITE);
    }
    display.drawFastHLine(0, 11, 128, SSD1306_WHITE);
  }

  // ===== KOLOM TENGAH =====
  // Baris 1: baterai remot
  if (!lowBattAlarm || blinkOn) drawBatteryIcon(41, 15, batteryVoltage);

  // Baris 2: timer (selalu tampil). Kalau timer belum aktif tampil --:--
  {
    char tb[10];
    bool end = false;
    if (timerVisible()) {
      int ts = timerShownSec();
      snprintf(tb, sizeof(tb), "%02d:%02d", (ts / 60) % 100, ts % 60);
      end = (timerCountdownMin > 0 && ts == 0);
    } else {
      snprintf(tb, sizeof(tb), "--:--");
    }
    if (end && blinkOn) {
      display.fillRoundRect(41, 25, 47, 11, 2, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.drawRoundRect(41, 25, 47, 11, 2, SSD1306_WHITE);
    }
    char tt[14];
    snprintf(tt, sizeof(tt), "T %s", tb);
    uiPrintC(64, 27, tt);               // tepat di tengah kotak
    display.setTextColor(SSD1306_WHITE);
  }

  // Baris 3: saklar
  uiChip(41, 38, 22, "S1", (pkt.switches & 1) != 0);
  uiChip(66, 38, 22, "S2", (pkt.switches & 2) != 0);

  // Baris 4: nama model. Kalau RX tidak membalas, diganti NO LINK yang berkedip
  if (nrfPower >= 4) {
    display.fillRoundRect(41, 51, 47, 11, 2, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setCursor(45, 53);
    display.print("NRF OFF");
    display.setTextColor(SSD1306_WHITE);
  } else if (linkLost) {
    if (blinkOn) {
      display.fillRoundRect(41, 51, 47, 11, 2, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
      display.setCursor(45, 53);
      display.print("NO LINK");
      display.setTextColor(SSD1306_WHITE);
    } else {
      display.drawRoundRect(41, 51, 47, 11, 2, SSD1306_WHITE);
    }
  } else {
    uiPrintC(64, 53, modelName[activeModel < modelCount ? activeModel : 0]);
  }

  // ===== KOTAK STIK =====
  // Kiri: X = CH4, Y = CH3. Kanan: X = CH1, Y = CH2
  for (int s = 0; s < 2; s++) {
    int bx = s == 0 ? 0 : 94;
    int by = 27;
    uint16_t cx = s == 0 ? pkt.ch[3] : pkt.ch[0];
    uint16_t cy = s == 0 ? pkt.ch[2] : pkt.ch[1];
    display.drawRoundRect(bx, by, 34, 36, 3, SSD1306_WHITE);
    // garis bantu tengah (titik-titik)
    for (int k = 4; k < 32; k += 4) {
      if (k < 30) display.drawPixel(bx + k, by + 18, SSD1306_WHITE);
      display.drawPixel(bx + 17, by + k, SSD1306_WHITE);
    }
    int px = bx + 4 + (int)((uint32_t)cx * 25 / 4095);
    int py = by + 4 + (int)((uint32_t)(4095 - cy) * 27 / 4095);
    display.fillCircle(px, py, 3, SSD1306_WHITE);
  }
}

// ===== Gaya dashboard 1 sampai 4 =====
static int dashCh(int i) { return i < 6 ? (int)pkt.ch[i] : (i == 6 ? (int)pkt.ch7 : (int)pkt.ch8); }
static int dashPct(int v) { return (int)(((int32_t)v - 2048) * 100 / 2048); }

// Teks timer "mm:ss" atau "--:--". end = hitung mundur habis.
static void dashTimerStr(char* out, bool* end) {
  *end = false;
  if (timerVisible()) {
    int ts = timerShownSec();
    snprintf(out, 8, "%02d:%02d", (ts / 60) % 100, ts % 60);
    *end = (timerCountdownMin > 0 && ts == 0);
  } else {
    strcpy(out, "--:--");
  }
}

// Bar atas dipakai gaya 1 dan 3: model, timer, tegangan baterai, kekuatan link (atau OFF / NO)
static void dashTopBar(bool blinkOn) {
  char t[12];
  sprintf(t, "M%d", activeModel + 1);
  display.fillRoundRect(0, 0, 15, 10, 2, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 1);
  display.print(t);
  display.setTextColor(SSD1306_WHITE);
  char nm[8];
  strncpy(nm, modelName[activeModel < modelCount ? activeModel : 0], 5);
  nm[5] = 0;
  display.setCursor(18, 1);
  display.print(nm);
  bool end;
  char tb[8];
  dashTimerStr(tb, &end);
  if (end && blinkOn) {
    display.fillRoundRect(48, 0, 33, 10, 2, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
  }
  display.setCursor(51, 1);
  display.print(tb);
  display.setTextColor(SSD1306_WHITE);
  sprintf(t, "%.1fV", batteryVoltage);
  if (!lowBattAlarm || blinkOn) { display.setCursor(84, 1); display.print(t); }
  if (nrfPower >= 4) uiChip(107, 0, 21, "OFF", true);
  else if (linkLost) uiChip(107, 0, 21, "NO", blinkOn);
  else {
    for (int i = 0; i < 5; i++) {
      int h = 2 + i * 2;
      int bx = 108 + i * 4;
      if (i < linkBars) display.fillRect(bx, 10 - h, 3, h, SSD1306_WHITE);
      else              display.drawFastHLine(bx, 9, 3, SSD1306_WHITE);
    }
  }
  display.drawFastHLine(0, 11, 128, SSD1306_WHITE);
}

// Gaya 1: delapan bar channel bertanda tengah dengan angka persen, dua kolom
static void dashStyle1() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  bool blinkOn = (millis() / 400) % 2;
  dashTopBar(blinkOn);
  for (int i = 0; i < 8; i++) {
    int x0 = (i / 4) * 64, y = 14 + (i % 4) * 12;
    char t[8];
    sprintf(t, "%d", i + 1);
    display.setCursor(x0 + 1, y + 1);
    display.print(t);
    int bx = x0 + 9, bw = 26;
    display.drawRect(bx, y, bw, 10, SSD1306_WHITE);
    int v = dashCh(i);
    int mid = bx + bw / 2;
    int px = bx + 1 + (int)((int32_t)v * (bw - 3) / 4095);
    if (px >= mid) display.fillRect(mid, y + 2, px - mid + 1, 6, SSD1306_WHITE);
    else           display.fillRect(px, y + 2, mid - px, 6, SSD1306_WHITE);
    display.drawFastVLine(mid, y, 10, SSD1306_WHITE);
    sprintf(t, "%d", dashPct(v));
    uiPrintR(x0 + 61, y + 2, t);
  }
}

// Gaya 2: timer besar di tengah, baterai, saklar, RX, empat bar stik di dasar
static void dashStyle2() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  bool blinkOn = (millis() / 400) % 2;
  char t[16];
  sprintf(t, "M%d", activeModel + 1);
  display.fillRoundRect(0, 0, 15, 10, 2, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 1);
  display.print(t);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(19, 1);
  display.print(modelName[activeModel < modelCount ? activeModel : 0]);
  int x = 84;
  if (wifiActive) { uiChip(x, 0, 9, "W", true); x += 11; }
  if (btActive)   { uiChip(x, 0, 9, "B", btConnected || blinkOn); x += 11; }
  if (inputSrc == INPUT_HP) uiChip(x, 0, 15, "HP", hpDataOk || blinkOn);
  if (nrfPower >= 4) uiChip(107, 0, 21, "OFF", true);
  else if (linkLost) uiChip(107, 0, 21, "NO", blinkOn);
  else {
    for (int i = 0; i < 5; i++) {
      int h = 2 + i * 2;
      int bx = 108 + i * 4;
      if (i < linkBars) display.fillRect(bx, 10 - h, 3, h, SSD1306_WHITE);
      else              display.drawFastHLine(bx, 9, 3, SSD1306_WHITE);
    }
  }
  display.drawFastHLine(0, 11, 128, SSD1306_WHITE);

  bool end;
  char tb[8];
  dashTimerStr(tb, &end);
  display.setTextSize(3);
  int tw = (int)strlen(tb) * 18;
  int tx = (128 - tw) / 2;
  if (end && blinkOn) {
    display.fillRoundRect(tx - 4, 14, tw + 6, 26, 3, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
  }
  display.setCursor(tx, 16);
  display.print(tb);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  if (!lowBattAlarm || blinkOn) drawBatteryIcon(0, 42, batteryVoltage);
  uiChip(50, 41, 20, "S1", (pkt.switches & 1) != 0);
  uiChip(72, 41, 20, "S2", (pkt.switches & 2) != 0);
  uint32_t tm = telemMs;
  if (tm != 0 && (uint32_t)(millis() - tm) < 2000 && telemetry.voltage > 1.0f && telemetry.voltage < 30.0f) {
    sprintf(t, "RX%.1fV", telemetry.voltage);
    uiPrintR(127, 42, t);
  } else {
    uiPrintR(127, 42, "RX --");
  }
  display.drawFastHLine(0, 53, 128, SSD1306_WHITE);
  for (int i = 0; i < 4; i++) {
    int bx = 2 + i * 32, bw = 28;
    display.drawRect(bx, 56, bw, 7, SSD1306_WHITE);
    int px = bx + 1 + (int)((int32_t)pkt.ch[i] * (bw - 3) / 4095);
    int mid = bx + bw / 2;
    if (px >= mid) display.fillRect(mid, 58, px - mid + 1, 3, SSD1306_WHITE);
    else           display.fillRect(px, 58, mid - px, 3, SSD1306_WHITE);
    display.drawFastVLine(mid, 56, 7, SSD1306_WHITE);
  }
  sprintf(t, "%d", 1);
  display.setCursor(0, 54);
}

// Gaya 3: delapan bar vertikal bertanda tengah, nomor channel dan status saklar di bawahnya
static void dashStyle3() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  bool blinkOn = (millis() / 400) % 2;
  dashTopBar(blinkOn);
  for (int i = 0; i < 8; i++) {
    int x = 6 + i * 15;
    display.drawRect(x, 14, 10, 38, SSD1306_WHITE);
    int h = (int)(((int32_t)dashCh(i) - 2048) * 17 / 2048);
    if (h > 0) display.fillRect(x + 1, 33 - h, 8, h + 1, SSD1306_WHITE);
    else       display.fillRect(x + 1, 33, 8, -h + 1, SSD1306_WHITE);
    display.drawFastHLine(x - 2, 33, 14, SSD1306_WHITE);
    char t[4];
    sprintf(t, "%d", i + 1);
    bool on = (i == 4 && (pkt.switches & 1)) || (i == 5 && (pkt.switches & 2));
    if (on) {
      display.fillRoundRect(x, 54, 10, 10, 2, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
      display.setCursor(x + 2, 55);
      display.print(t);
      display.setTextColor(SSD1306_WHITE);
    } else {
      display.setCursor(x + 2, 55);
      display.print(t);
    }
  }
}

// Gimbal bundar: stik digambar sebagai titik di dalam lingkaran dengan garis silang putus-putus
static void dashGimbal(int cx, int cy, int r, int vx, int vy) {
  display.drawCircle(cx, cy, r, SSD1306_WHITE);
  display.drawCircle(cx, cy, r / 2, SSD1306_WHITE);
  for (int k = -r + 3; k <= r - 3; k += 4) {
    display.drawPixel(cx + k, cy, SSD1306_WHITE);
    display.drawPixel(cx, cy + k, SSD1306_WHITE);
  }
  int dx = (int)(((int32_t)vx - 2048) * (r - 5) / 2048);
  int dy = (int)(((int32_t)2048 - vy) * (r - 5) / 2048);
  int d2 = dx * dx + dy * dy, lim = (r - 5) * (r - 5);
  if (d2 > lim && d2 > 0) {                    // kunci di dalam lingkaran
    float f = sqrtf((float)lim / (float)d2);
    dx = (int)(dx * f); dy = (int)(dy * f);
  }
  display.drawLine(cx, cy, cx + dx, cy + dy, SSD1306_WHITE);
  display.fillCircle(cx + dx, cy + dy, 3, SSD1306_WHITE);
}

// Gaya 4: dua gimbal bundar di sisi, timer besar di atas, model dan baterai di tengah
static void dashStyle4() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  bool blinkOn = (millis() / 400) % 2;
  char t[16];
  sprintf(t, "M%d", activeModel + 1);
  display.fillRoundRect(0, 0, 15, 10, 2, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 1);
  display.print(t);
  display.setTextColor(SSD1306_WHITE);
  if (nrfPower >= 4) uiChip(107, 0, 21, "OFF", true);
  else if (linkLost) uiChip(107, 0, 21, "NO", blinkOn);
  else {
    for (int i = 0; i < 5; i++) {
      int h = 2 + i * 2;
      int bx = 108 + i * 4;
      if (i < linkBars) display.fillRect(bx, 10 - h, 3, h, SSD1306_WHITE);
      else              display.drawFastHLine(bx, 9, 3, SSD1306_WHITE);
    }
  }
  bool end;
  char tb[8];
  dashTimerStr(tb, &end);
  display.setTextSize(2);
  int tx = (128 - (int)strlen(tb) * 12) / 2;
  if (end && blinkOn) {
    display.fillRoundRect(tx - 3, 0, (int)strlen(tb) * 12 + 5, 17, 3, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
  }
  display.setCursor(tx, 1);
  display.print(tb);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  dashGimbal(19, 43, 18, pkt.ch[3], pkt.ch[2]);
  dashGimbal(109, 43, 18, pkt.ch[0], pkt.ch[1]);

  uiPrintC(64, 20, modelName[activeModel < modelCount ? activeModel : 0]);
  if (!lowBattAlarm || blinkOn) drawBatteryIcon(41, 31, batteryVoltage);
  uiChip(46, 44, 18, "S1", (pkt.switches & 1) != 0);
  uiChip(66, 44, 18, "S2", (pkt.switches & 2) != 0);
  int x = 49;
  if (wifiActive) { uiChip(x, 54, 9, "W", true); x += 11; }
  if (btActive)   { uiChip(x, 54, 9, "B", btConnected || blinkOn); x += 11; }
  if (inputSrc == INPUT_HP) uiChip(x, 54, 15, "HP", hpDataOk || blinkOn);
}

static const char* const DASH_NAMES[5] = {"KLASIK", "BAR 8CH", "TIMER", "VERTIKAL", "GIMBAL"};

// Gambar satu gaya tanpa mengirim ke layar
static void dashDraw(int st) {
  switch (st) {
    case 1: dashStyle1(); break;
    case 2: dashStyle2(); break;
    case 3: dashStyle3(); break;
    case 4: dashStyle4(); break;
    default: dashStyle0(); break;
  }
}

void drawDashboard() {
  dashDraw(dashStyle);
  display.display();
}

// ================= DAFTAR MENU =================

void drawListMenu() {
  display.clearDisplay();
  char cn[10];
  uiCounter(cn, listIndex, LIST_COUNT);
  uiHeader("MENU", cn);

  int top = uiTop(listIndex, LIST_COUNT);
  for (int r = 0; r < UI_ROWS && top + r < LIST_COUNT; r++) {
    int i = top + r;
    int y = UI_ROW_Y0 + r * UI_ROW_H;
    bool sel = (i == listIndex);
    uint16_t fg = uiRow(y, sel, false, true);
    display.setTextColor(fg);
    display.setCursor(6, y + 1);
    display.print(LIST_NAMES[i]);
    if (sel) display.fillTriangle(116, y + 2, 116, y + 8, 120, y + 5, SSD1306_BLACK);
  }
  display.setTextColor(SSD1306_WHITE);
  uiScrollbar(top, LIST_COUNT);
  display.display();
}

// ================= BRIGHTNESS =================
void drawBrightnessMenu() {
  display.clearDisplay();
  char cn[10];
  sprintf(cn, "%d/%d", encoderPos, BRIGHTNESS_MAX);
  uiHeader("BRIGHTNESS", cn);

  // Ikon matahari
  int cx = 18, cy = 29;
  display.fillCircle(cx, cy, 4, SSD1306_WHITE);
  static const int8_t RAY[8][4] = {{0,-7,0,-9},{0,7,0,9},{-7,0,-9,0},{7,0,9,0},{5,-5,6,-6},{-5,-5,-6,-6},{5,5,6,6},{-5,5,-6,6}};
  for (int i = 0; i < 8; i++) display.drawLine(cx + RAY[i][0], cy + RAY[i][1], cx + RAY[i][2], cy + RAY[i][3], SSD1306_WHITE);

  // Angka besar
  display.setTextSize(3);
  int nw = (encoderPos >= 10) ? 36 : 18;
  display.setCursor(48, 18);
  display.print(encoderPos);
  display.setTextSize(1);
  display.setCursor(48 + nw + 3, 31);
  display.print("/10");

  // Tangga level
  for (int i = 0; i < BRIGHTNESS_MAX; i++) {
    int h = 3 + i;
    int x = 9 + i * 11;
    int y = 61 - h + 1;
    if (i < encoderPos) display.fillRect(x, y, 9, h, SSD1306_WHITE);
    else                display.drawRect(x, y, 9, h, SSD1306_WHITE);
  }
  display.display();
}

// ================= CHANNEL READOUT =================
void drawChannelMenu() {
  display.clearDisplay();
  uiHeader("CH READOUT", "");
  if (inputSrc == INPUT_HP) uiChip(104, 0, 20, "HP", true);

  uint16_t ch[8];
  for (int i = 0; i < 6; i++) ch[i] = pkt.ch[i];
  ch[6] = pkt.ch7;
  ch[7] = pkt.ch8;

  // 2 kolom x 4 baris. Tiap sel: nama dan nilai, di bawahnya bar dari tengah.
  for (int i = 0; i < 8; i++) {
    int col = i / 4, row = i % 4;
    int x0 = col * 65 + 1;
    int y = 14 + row * 12;
    char nm[6], vl[8];
    sprintf(nm, "CH%d", i + 1);
    sprintf(vl, "%d", ch[i]);
    display.setCursor(x0, y);
    display.print(nm);
    uiPrintR(x0 + 61, y, vl);
    uiMeter(x0, y + 8, 62, 4, (int)((uint32_t)ch[i] * 1000 / 4095), true, SSD1306_WHITE);
  }
  display.display();
}

// ================= CHANNEL REVERSE =================
void drawReverseMenu() {
  display.clearDisplay();
  char cn[10];
  uiCounter(cn, reverseIndex, 6);
  uiHeader("REVERSE", cn);

  int top = uiTop(reverseIndex, 6);
  for (int r = 0; r < UI_ROWS && top + r < 6; r++) {
    int i = top + r;
    int y = UI_ROW_Y0 + r * UI_ROW_H;
    bool sel = (i == reverseIndex);
    bool ed = sel && reverseEditMode;
    uint16_t fg = uiRow(y, sel, ed, true);
    char nm[6];
    sprintf(nm, "CH%d", i + 1);
    display.setTextColor(fg);
    display.setCursor(6, y + 1);
    display.print(nm);
    display.setCursor(40, y + 1);
    display.print(channelReverse[i] ? "REVERSE" : "NORMAL");
    uiPill(120, y, channelReverse[i], fg);
    if (ed) {   // tanda sedang edit: panah di kiri pill
      display.fillTriangle(98, y + 5, 101, y + 3, 101, y + 7, SSD1306_WHITE);
    }
  }
  display.setTextColor(SSD1306_WHITE);
  uiScrollbar(top, 6);
  display.display();
}

// ================= CHANNEL TRIM =================
void drawTrimMenu() {
  display.clearDisplay();
  char cn[10];
  uiCounter(cn, trimIndex, 6);
  uiHeader("TRIM", cn);

  int top = uiTop(trimIndex, 6);
  for (int r = 0; r < UI_ROWS && top + r < 6; r++) {
    int i = top + r;
    int y = UI_ROW_Y0 + r * UI_ROW_H;
    bool sel = (i == trimIndex);
    bool ed = sel && reverseEditMode;
    uint16_t fg = uiRow(y, sel, ed, true);
    char nm[6], vl[8];
    sprintf(nm, "CH%d", i + 1);
    int t = channelTrim[i];
    if (t > 0) sprintf(vl, "+%d", t); else sprintf(vl, "%d", t);
    display.setTextColor(fg);
    display.setCursor(6, y + 1);
    display.print(nm);
    uiMeter(30, y + 2, 50, 6, (t + 100) * 1000 / 200, true, fg);
    uiValue(120, y, vl, ed, fg);
  }
  display.setTextColor(SSD1306_WHITE);
  uiScrollbar(top, 6);
  display.display();
}

// ================= THEME (LIGHT / DARK) =================
void drawThemeMenu() {
  display.clearDisplay();
  uiHeader("THEME", "");

  // Dua kartu contoh: LIGHT berlatar putih, DARK berlatar hitam
  for (int i = 0; i < 2; i++) {
    int x = i == 0 ? 8 : 68;
    int y = 19, w = 52, h = 30;
    bool sel = (themeIndex == i);
    if (i == 0) {
      display.fillRoundRect(x, y, w, h, 4, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.drawRoundRect(x, y, w, h, 4, SSD1306_WHITE);
      display.setTextColor(SSD1306_WHITE);
    }
    uiPrintC(x + w / 2, y + 11, i == 0 ? "LIGHT" : "DARK");
    display.setTextColor(SSD1306_WHITE);
    if (sel) {
      display.drawRoundRect(x - 3, y - 3, w + 6, h + 6, 6, SSD1306_WHITE);
      if (reverseEditMode) display.drawRoundRect(x - 4, y - 4, w + 8, h + 8, 7, SSD1306_WHITE);
      if (!reverseEditMode) display.fillTriangle(x + w / 2, y + h + 5, x + w / 2 - 3, y + h + 9, x + w / 2 + 3, y + h + 9, SSD1306_WHITE);
    }
  }
  if (reverseEditMode) uiPrintC(64, 56, "PUTAR LALU KLIK");
  display.display();
}

// ================= MENU KALIBRASI STICK =================
// Langkah 1: putar semua stick penuh ke semua sudut, lalu tekan SW.
// Langkah 2: lepas stick ke tengah, lalu tekan SW. Hasil disimpan otomatis.
// Tahan SW atau tekan BTN untuk kembali ke daftar menu. Kalibrasi baru
// tersimpan setelah langkah 2 selesai.
static uint8_t  calibStep = 0;
static uint16_t calTmpMin[4];
static uint16_t calTmpMax[4];
static const char* STICK_NAME[4] = {"J1X", "J1Y", "J2X", "J2Y"};

void gotoMode(uint8_t m);   // didefinisikan di bagian navigasi

void calibBegin() {
  calibStep = 0;
  for (int i = 0; i < 4; i++) {
    calTmpMin[i] = 4095;
    calTmpMax[i] = 0;
  }
}

void calibPress() {
  if (calibStep == 0) {
    // Pastikan semua stick sudah digerakkan cukup jauh
    for (int i = 0; i < 4; i++) {
      if ((int)calTmpMax[i] - (int)calTmpMin[i] < 1000) {
        buzzerBeep(300);   // belum cukup, ulangi
        return;
      }
    }
    buzzerBeep(60);
    calibStep = 1;
  } else {
    // Titik tengah harus berada di antara min dan max
    for (int i = 0; i < 4; i++) {
      uint16_t c = rawStick[i];
      if (c <= calTmpMin[i] + 100 || c >= calTmpMax[i] - 100) {
        buzzerBeep(300);   // stick belum di tengah
        return;
      }
    }
    for (int i = 0; i < 4; i++) {
      calMin[i] = calTmpMin[i];
      calCenter[i] = rawStick[i];
      calMax[i] = calTmpMax[i];
    }
    saveCalibration();
    buzzerBeep(150);
    gotoMode(MODE_LIST);
  }
}

// Posisi horizontal nilai ADC 0..4095 di dalam bar (bar x=22, lebar 76, area dalam 23..96)
static int calBarX(uint16_t v) {
  if (v > 4095) v = 4095;
  return 23 + (int)((uint32_t)v * 73 / 4095);
}

void drawCalibMenu() {
  if (calibStep == 0) {
    for (int i = 0; i < 4; i++) {
      uint16_t v = rawStick[i];
      if (v < calTmpMin[i]) calTmpMin[i] = v;
      if (v > calTmpMax[i]) calTmpMax[i] = v;
    }
  }

  display.clearDisplay();
  char st[8];
  sprintf(st, "%d/2", calibStep + 1);
  uiHeader("KALIBRASI", st);

  display.setTextColor(SSD1306_WHITE);
  uiPrintC(64, 46, calibStep == 0 ? "Gerak penuh lalu KLIK" : "Tengahkan lalu KLIK");

  // Satu baris per stick: label, bar posisi dengan rentang yang dilewati, nilai mentah
  for (int i = 0; i < 4; i++) {
    int y = 13 + i * 8;
    uint16_t v = rawStick[i];

    display.setTextColor(SSD1306_WHITE);
    display.setCursor(1, y);
    display.print(STICK_NAME[i]);
    display.setTextColor(SSD1306_WHITE);

    display.drawRect(24, y, 74, 7, SSD1306_WHITE);
    if (calTmpMax[i] >= calTmpMin[i]) {
      int x1 = calBarX(calTmpMin[i]) + 2;
      int x2 = calBarX(calTmpMax[i]) + 2;
      if (calibStep == 0) {
        display.fillRect(x1, y + 2, (x2 - x1) + 1, 3, SSD1306_WHITE);
      } else {
        display.drawFastVLine(x1, y + 1, 5, SSD1306_WHITE);
        display.drawFastVLine(x2, y + 1, 5, SSD1306_WHITE);
      }
    }
    int px = calBarX(v) + 2;
    if (calibStep == 0) {   // di atas blok terisi: penanda dibuat kontras dengan garis hitam di sisinya
      display.drawFastVLine(px - 1, y + 1, 5, SSD1306_BLACK);
      display.drawFastVLine(px + 1, y + 1, 5, SSD1306_BLACK);
      display.drawFastVLine(px, y + 1, 5, SSD1306_WHITE);
    } else {
      display.fillRect(px - 1, y + 1, 3, 5, SSD1306_WHITE);
    }
    char vb[8];
    sprintf(vb, "%d", v);
    uiPrintR(127, y, vb);
  }

  uiPrintC(64, 56, "HOLD = batal");

  display.display();
}

// ========= FreeRTOS synchronization =========
SemaphoreHandle_t pktMutex;

// ================= MENU KONEKTIVITAS =================
int connCurrentValue(int idx) {
  if (idx == 0) return wifiOn ? 1 : 0;
  if (idx == 1) return btOn ? 1 : 0;
  return inputSrc;
}

// Terapkan pilihan menu Konektivitas. EEPROM.commit() dilakukan oleh commitEdit().
void connApply(int idx, int val) {
  if (idx == 0) {
    if (val == 1 && btOn) { buzzerBeep(300); return; }   // tolak: matikan Bluetooth dulu
    wifiOn = (val == 1);
    EEPROM.write(EEPROM_ADDR_WIFI, wifiOn ? 1 : 0);
  } else if (idx == 1) {
    if (val == 1 && wifiOn) { buzzerBeep(300); return; } // tolak: matikan WiFi dulu
    btOn = (val == 1);
    EEPROM.write(EEPROM_ADDR_BT, btOn ? 1 : 0);
    if (!btOn && inputSrc == INPUT_HP) inputSrc = INPUT_REMOT;   // tanpa Bluetooth, kembali ke joystick
  } else {
    if (val == INPUT_HP && !(btActive && btConnected)) {
      buzzerBeep(300);          // tolak: Bluetooth harus ON dan ada HP yang tersambung
      return;
    }
    inputSrc = (uint8_t)val;
  }
}

void drawConnMenu() {
  display.clearDisplay();
  uiHeader("KONEKTIVITAS", "");
  if (millis() - lastBlink > 300) {
    lastBlink = millis();
    reverseBlinkState = !reverseBlinkState;
  }

  static const char* const NAMES[3] = {"WiFi", "Bluetooth", "Input"};
  for (int i = 0; i < 3; i++) {
    int y = UI_ROW_Y0 + i * UI_ROW_H;
    bool sel = (i == connIndex);
    bool ed = sel && reverseEditMode;
    uint16_t fg = uiRow(y, sel, ed, false);
    display.setTextColor(fg);
    display.setCursor(6, y + 1);
    display.print(NAMES[i]);
    int v = ed ? connEdit : connCurrentValue(i);
    if (i == 2) uiValue(122, y, v ? "HP" : "REMOT", ed, fg);
    else        uiPill(122, y, v != 0, fg);
  }
  display.setTextColor(SSD1306_WHITE);

  // Panel status nyata
  display.drawRoundRect(0, 44, 128, 20, 3, SSD1306_WHITE);
  uiDot(7, 50, wifiActive);
  display.setCursor(14, 47);
  display.print("WiFi");
  if (wifiActive) {
    display.setCursor(40, 47);
    display.print("192.168.4.1");
    char nb[6];
    sprintf(nb, "x%d", WiFi.softAPgetStationNum());
    uiPrintR(123, 47, nb);
  } else {
    display.setCursor(40, 47);
    display.print(wifiOn ? "MENYALAKAN" : "OFF");
  }
  uiDot(7, 58, btActive && btConnected);
  display.setCursor(14, 55);
  display.print("BT");
  display.setCursor(40, 55);
  if (!btActive)        display.print(btOn ? "MENYALAKAN" : "OFF");
  else if (btConnected) display.print(inputSrc == INPUT_HP ? (hpDataOk ? "HP DATA" : (reverseBlinkState ? "HP FS!" : "")) : "TERHUBUNG");
  else                  display.print("MENUNGGU");

  display.display();
}

// ================= MENU SUARA =================
int soundCurrentValue(int idx) {
  if (idx == 0) return soundVol;
  return (soundMask >> (idx - 1)) & 1;
}

// Terapkan pilihan menu Suara. EEPROM.commit() dilakukan oleh commitEdit().
void soundApply(int idx, int val) {
  if (idx == 0) {
    soundVol = (uint8_t)constrain(val, 1, VOL_MAX);
    EEPROM.write(EEPROM_ADDR_VOL, soundVol);
  } else {
    uint16_t bit = (uint16_t)(1u << (idx - 1));
    if (val) soundMask |= bit; else soundMask &= (uint16_t)~bit;
    soundMaskSave();
    if (val) sndPreview((uint8_t)(idx - 1), 0);      // beri contoh suaranya
  }
}

void drawSoundMenu() {
  display.clearDisplay();
  char cn[10];
  uiCounter(cn, soundIndex, SOUND_ITEMS);
  uiHeader("SUARA", cn);

  static const char* const NAMES[SOUND_ITEMS] = {
    "VOLUME", "DISCONNECT", "BOOTING", "PINDAH MENU", "SCROLL", "KLIK", "BT TERHUBUNG", "WIFI TERHUBUNG", "RX PUTUS", "BATERAI HABIS", "TIMER"
  };

  int top = uiTop(soundIndex, SOUND_ITEMS);
  for (int r = 0; r < UI_ROWS && top + r < SOUND_ITEMS; r++) {
    int i = top + r;
    int y = UI_ROW_Y0 + r * UI_ROW_H;
    bool sel = (i == soundIndex);
    bool ed = sel && reverseEditMode;
    uint16_t fg = uiRow(y, sel, ed, true);
    display.setTextColor(fg);
    display.setCursor(6, y + 1);
    display.print(NAMES[i]);
    int v = ed ? soundEdit : soundCurrentValue(i);
    if (i == 0) {
      uiMeter(50, y + 2, 46, 6, v * 1000 / VOL_MAX, false, fg);
      char vb[4];
      sprintf(vb, "%d", v);
      uiValue(120, y, vb, ed, fg);
    } else {
      uiPill(120, y, v != 0, fg);
      if (ed) display.fillTriangle(98, y + 5, 101, y + 3, 101, y + 7, SSD1306_WHITE);
    }
  }
  display.setTextColor(SSD1306_WHITE);
  uiScrollbar(top, SOUND_ITEMS);
  display.display();
}

#ifndef TRIM_LIMIT
#define TRIM_LIMIT 100
#endif

// ================= MODEL, RATE, EXPO, ENDPOINT, MIXING, FAILSAFE, TIMER =================
// Urutan pemrosesan tiap channel (nilai bawaan membuat hasilnya sama persis seperti sebelumnya):
//   stick, dual rate dan expo (CH1..CH4), mixing, reverse, trim, endpoint.

// x = -2048..2047 (0 di tengah). Dual rate mengecilkan gerak, expo melembutkan sekitar tengah.
static int32_t rateExpo(int32_t x, int dr, int ex) {
  x = x * dr / 100;
  if (ex > 0) {
    int32_t c = (x * x / 2048) * x / 2048;      // x^3 dinormalkan
    x = (x * (100 - ex) + c * ex) / 100;
  }
  return x;
}

static int32_t clamp4095(int32_t v) { return v < 0 ? 0 : (v > 4095 ? 4095 : v); }

// raw: 6 nilai 0..4095 (sebelum pengolahan). out: 6 nilai yang dikirim ke RX.
void applyChannels(const uint16_t* raw, uint16_t* out) {
  int32_t v[6];
  for (int i = 0; i < 6; i++) v[i] = raw[i];
  for (int i = 0; i < 4; i++) v[i] = 2048 + rateExpo(v[i] - 2048, dualRate[i], expoAmt[i]);

  if (mixType != 0 && mixA >= 1 && mixA <= 6 && mixB >= 1 && mixB <= 6 && mixA != mixB) {
    int ia = mixA - 1, ib = mixB - 1;
    if (mixType == 1) {          // ELEVON: A = aileron, B = elevator. A' = elevator + aileron, B' = elevator - aileron
      int32_t a = v[ia] - 2048, e = v[ib] - 2048;
      v[ia] = clamp4095(2048 + (e + a) * mixWeight / 100);
      v[ib] = clamp4095(2048 + (e - a) * mixWeight / 100);
    } else {                     // V-TAIL: A = elevator, B = rudder. A' = elevator + rudder, B' = elevator - rudder
      int32_t e = v[ia] - 2048, r = v[ib] - 2048;
      v[ia] = clamp4095(2048 + (e + r) * mixWeight / 100);
      v[ib] = clamp4095(2048 + (e - r) * mixWeight / 100);
    }
  }

  for (int i = 0; i < 6; i++) {
    int32_t val = channelReverse[i] ? (4095 - v[i]) : v[i];
    val += channelTrim[i] * 10;
    val = clamp4095(val);
    int32_t d = val - 2048;
    d = d * (d >= 0 ? epHi[i] : epLo[i]) / 100;
    out[i] = (uint16_t)clamp4095(2048 + d);
  }
}

// Nilai failsafe (0..4095) untuk satu channel saat mode HP kehilangan data
static uint16_t failsafeRaw(int i) { return (uint16_t)(((uint32_t)failsafePct[i] * 4095 + 50) / 100); }

// Pasangan channel bawaan tiap tipe mixing (ELEVON: CH1 aileron dan CH2 elevator. V-TAIL: CH2 elevator dan CH4 rudder)
void mixSetType(uint8_t t) {
  if (t > 2) t = 0;
  if (t != mixType) {
    if (t == 1) { mixA = 1; mixB = 2; }
    else if (t == 2) { mixA = 2; mixB = 4; }
  }
  mixType = t;
}

// Ubah salah satu channel mixing (which 0 = A, 1 = B). Kalau sama dengan pasangannya, pasangannya ditukar ke nilai lama.
void mixSetChan(int which, int v) {
  if (v < 1 || v > 6) return;
  if (which == 0) { if (v == mixB) mixB = mixA; mixA = (uint8_t)v; }
  else            { if (v == mixA) mixA = mixB; mixB = (uint8_t)v; }
}

// Data lama atau rusak: pasangan channel dibuat valid sesuai tipenya
static void mixFixChannels() {
  if (mixA < 1 || mixA > 6 || mixB < 1 || mixB > 6 || mixA == mixB) {
    if (mixType == 2) { mixA = 2; mixB = 4; } else { mixA = 1; mixB = 2; }
  }
}

// ----- Mix bebas: 4 mix, tiap mix = sumber (CH1..CH8) ditambahkan ke tujuan (CH1..CH8) dengan bobot -100..+100 persen -----
#define FM_BYTES 12
static void fmDefault(uint8_t* b) {
  for (int m = 0; m < 4; m++) { b[m * 3] = 0; b[m * 3 + 1] = 1; b[m * 3 + 2] = 150; }   // mati, tujuan CH1, +50%
}
static void fmPack(uint8_t* b) {
  for (int m = 0; m < 4; m++) { b[m * 3] = fmSrc[m]; b[m * 3 + 1] = fmDst[m]; b[m * 3 + 2] = (uint8_t)(fmW[m] + 100); }
}
static void fmUnpack(const uint8_t* b) {
  for (int m = 0; m < 4; m++) {
    fmSrc[m] = (b[m * 3] <= 8) ? b[m * 3] : 0;
    fmDst[m] = (b[m * 3 + 1] >= 1 && b[m * 3 + 1] <= 8) ? b[m * 3 + 1] : 1;
    fmW[m]   = (b[m * 3 + 2] <= 200) ? (int8_t)((int)b[m * 3 + 2] - 100) : 50;
  }
}
// o = 8 nilai akhir (CH1..CH8, 0..4095). Tiap mix menambah (sumber - tengah) x bobot ke tujuan. Sumber dibaca sebelum ada mix lain.
static rf24_pa_dbm_e nrfPaLevel(uint8_t p) {
  switch (p) { case 0: return RF24_PA_MIN; case 1: return RF24_PA_LOW; case 2: return RF24_PA_HIGH; }
  return RF24_PA_MAX;
}
void freeMixApply(uint16_t* o) {
  int32_t add[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  bool any = false;
  for (int m = 0; m < 4; m++) {
    if (fmSrc[m] < 1 || fmSrc[m] > 8 || fmDst[m] < 1 || fmDst[m] > 8 || fmW[m] == 0) continue;
    add[fmDst[m] - 1] += ((int32_t)o[fmSrc[m] - 1] - 2048) * fmW[m] / 100;
    any = true;
  }
  if (!any) return;
  for (int i = 0; i < 8; i++) o[i] = (uint16_t)clamp4095((int32_t)o[i] + add[i]);
}

// ----- Penyimpanan model -----
static void modelBlobDefault(uint8_t* b) {
  memset(b, 0, MODEL_BLOB);
  for (int i = 0; i < 6; i++) b[1 + i] = 128;          // trim 0
  for (int i = 0; i < 4; i++) { b[7 + i] = 100; b[11 + i] = 0; }
  for (int i = 0; i < 6; i++) { b[15 + i] = 100; b[21 + i] = 100; }
  static const uint8_t fs[8] = {50, 50, 0, 50, 0, 0, 50, 50};
  for (int i = 0; i < 8; i++) b[27 + i] = fs[i];
  b[35] = 0; b[36] = 50; b[37] = 1; b[38] = 2;
}

static void modelPack(uint8_t* b) {
  memset(b, 0, MODEL_BLOB);
  uint8_t rv = 0;
  for (int i = 0; i < 6; i++) if (channelReverse[i]) rv |= (uint8_t)(1 << i);
  b[0] = rv;
  for (int i = 0; i < 6; i++) b[1 + i] = (uint8_t)(channelTrim[i] + 128);
  for (int i = 0; i < 4; i++) { b[7 + i] = dualRate[i]; b[11 + i] = expoAmt[i]; }
  for (int i = 0; i < 6; i++) { b[15 + i] = epLo[i]; b[21 + i] = epHi[i]; }
  for (int i = 0; i < 8; i++) b[27 + i] = failsafePct[i];
  b[35] = mixType; b[36] = mixWeight; b[37] = mixA; b[38] = mixB;
}

static uint8_t inRange(int v, int lo, int hi, int def) { return (v >= lo && v <= hi) ? (uint8_t)v : (uint8_t)def; }

static void modelUnpack(const uint8_t* b) {
  uint8_t d[MODEL_BLOB];
  if (b[0] == 0xFF && b[1] == 0xFF) modelBlobDefault(d); else memcpy(d, b, MODEL_BLOB);
  for (int i = 0; i < 6; i++) channelReverse[i] = (d[0] >> i) & 1;
  for (int i = 0; i < 6; i++) { int t = (int)d[1 + i] - 128; channelTrim[i] = (int8_t)((t >= -TRIM_LIMIT && t <= TRIM_LIMIT) ? t : 0); }
  for (int i = 0; i < 4; i++) { dualRate[i] = inRange(d[7 + i], 30, 100, 100); expoAmt[i] = inRange(d[11 + i], 0, 100, 0); }
  for (int i = 0; i < 6; i++) { epLo[i] = inRange(d[15 + i], 20, 120, 100); epHi[i] = inRange(d[21 + i], 20, 120, 100); }
  static const uint8_t fsDef[8] = {50, 50, 0, 50, 0, 0, 50, 50};
  for (int i = 0; i < 8; i++) failsafePct[i] = inRange(d[27 + i], 0, 100, fsDef[i]);
  mixType = inRange(d[35], 0, 2, 0);
  mixWeight = inRange(d[36], 10, 100, 50);
  mixA = d[37]; mixB = d[38];
  mixFixChannels();
}

// Tulis salinan kerja (model aktif) ke EEPROM. EEPROM.commit() dilakukan pemanggil.
static void miscSave();
void cfgSaveWorking() {
  for (int i = 0; i < 6; i++) {
    EEPROM.write(EEPROM_ADDR_REVERSE + i, channelReverse[i] ? 1 : 0);
    EEPROM.write(EEPROM_ADDR_TRIM + i, (uint8_t)(channelTrim[i] + 128));
    EEPROM.write(EEPROM_ADDR_EPLO + i, epLo[i]);
    EEPROM.write(EEPROM_ADDR_EPHI + i, epHi[i]);
  }
  for (int i = 0; i < 4; i++) { EEPROM.write(EEPROM_ADDR_DR + i, dualRate[i]); EEPROM.write(EEPROM_ADDR_EXPO + i, expoAmt[i]); }
  for (int i = 0; i < 8; i++) EEPROM.write(EEPROM_ADDR_FS + i, failsafePct[i]);
  EEPROM.write(EEPROM_ADDR_MIXT, mixType);
  EEPROM.write(EEPROM_ADDR_MIXW, mixWeight);
  EEPROM.write(EEPROM_ADDR_TMIN, timerCountdownMin);
  EEPROM.write(EEPROM_ADDR_MIXA, mixA);
  EEPROM.write(EEPROM_ADDR_MIXB, mixB);
  EEPROM.write(EEPROM_ADDR_TTRIG, timerTrig);
  EEPROM.write(EEPROM_ADDR_TTHR, timerThr);
  EEPROM.write(EEPROM_ADDR_TDIR, timerDir);
  EEPROM.write(EEPROM_ADDR_TRTRIG, timerRstTrig);
  EEPROM.write(EEPROM_ADDR_TRDIR, timerRstDir);
  EEPROM.write(EEPROM_ADDR_BLOW, battLowTenths);
  uint8_t f[FM_BYTES];
  fmPack(f);
  for (int i = 0; i < FM_BYTES; i++) EEPROM.write(EEPROM_ADDR_FMIXW + i, f[i]);
  miscSave();    // daya NRF, buzzer, gaya dashboard
}

// Pengaturan umum (bukan per model): daya NRF dan buzzer
static void miscSave() {
  EEPROM.write(EEPROM_ADDR_NRFPWR, nrfPower);   // termasuk OFF: sesudah restart tetap pada pengaturan terakhir
  EEPROM.write(EEPROM_ADDR_DASH, dashStyle);
  EEPROM.write(EEPROM_ADDR_BUZTYPE, buzPassive);
  EEPROM.write(EEPROM_ADDR_BUZTONE, buzTone);
}

static void modelSlotSave(int slot) {
  uint8_t b[MODEL_BLOB];
  modelPack(b);
  for (int i = 0; i < MODEL_BLOB; i++) EEPROM.write(EEPROM_ADDR_SLOTS + slot * MODEL_BLOB + i, b[i]);
  uint8_t f[FM_BYTES];
  fmPack(f);
  for (int i = 0; i < FM_BYTES; i++) EEPROM.write(EEPROM_ADDR_FMIXS + slot * FM_BYTES + i, f[i]);
}

static void modelSlotLoad(int slot) {
  uint8_t b[MODEL_BLOB];
  for (int i = 0; i < MODEL_BLOB; i++) b[i] = EEPROM.read(EEPROM_ADDR_SLOTS + slot * MODEL_BLOB + i);
  modelUnpack(b);
  uint8_t f[FM_BYTES];
  for (int i = 0; i < FM_BYTES; i++) f[i] = EEPROM.read(EEPROM_ADDR_FMIXS + slot * FM_BYTES + i);
  fmUnpack(f);
}

// Mix bebas kosong untuk satu slot (model baru)
static void fmSlotClear(int slot) {
  uint8_t f[FM_BYTES];
  fmDefault(f);
  for (int i = 0; i < FM_BYTES; i++) EEPROM.write(EEPROM_ADDR_FMIXS + slot * FM_BYTES + i, f[i]);
}

// ----- Nama dan jumlah model (diatur dari aplikasi) -----
// Nama dibersihkan: huruf besar, angka, spasi, titik, minus, garis bawah. Maksimum MODEL_NAME_MAX karakter.
static void modelNameClean(const char* in, char* out) {
  int n = 0;
  for (int i = 0; in[i] && n < MODEL_NAME_MAX; i++) {
    char c = in[i];
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    bool okc = (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '.' || c == '-' || c == '_';
    if (!okc) continue;
    if (c == ' ' && n == 0) continue;           // tanpa spasi di depan
    out[n++] = c;
  }
  while (n > 0 && out[n - 1] == ' ') n--;       // tanpa spasi di belakang
  out[n] = 0;
}

static void modelMetaSave() {
  EEPROM.write(EEPROM_ADDR_MCOUNT, modelCount);
  EEPROM.write(EEPROM_ADDR_MNMAGIC, 0xB5);
  for (int i = 0; i < MAX_MODELS; i++)
    for (int k = 0; k < 12; k++) EEPROM.write(EEPROM_ADDR_MNAMES + i * 12 + k, i < modelCount ? (uint8_t)modelName[i][k] : 0);
}

static void modelMetaLoad() {
  if (EEPROM.read(EEPROM_ADDR_MNMAGIC) != 0xB5) {          // pertama kali: lima model bawaan
    static const char* const defn[5] = {"PLANE", "BOAT", "ROVER", "UNIVERSAL", "MODEL 5"};
    modelCount = 5;
    for (int i = 0; i < MAX_MODELS; i++) { memset(modelName[i], 0, 12); if (i < 5) strcpy(modelName[i], defn[i]); }
    modelMetaSave();
    EEPROM.commit();
    return;
  }
  modelCount = EEPROM.read(EEPROM_ADDR_MCOUNT);
  if (modelCount < 1 || modelCount > MAX_MODELS) modelCount = 5;
  for (int i = 0; i < MAX_MODELS; i++) {
    char raw[12];
    for (int k = 0; k < 11; k++) raw[k] = (char)EEPROM.read(EEPROM_ADDR_MNAMES + i * 12 + k);
    raw[11] = 0;
    modelNameClean(raw, modelName[i]);
    if (modelName[i][0] == 0) sprintf(modelName[i], "MODEL %d", i + 1);
  }
}

// Tambah model baru (pengaturan bawaan). Mengembalikan indeks baru, atau -1 penuh, -2 nama salah.
int modelAdd(const char* nm) {
  char clean[12];
  modelNameClean(nm, clean);
  if (clean[0] == 0) return -2;
  if (modelCount >= MAX_MODELS) return -1;
  uint8_t b[MODEL_BLOB];
  modelBlobDefault(b);
  for (int i = 0; i < MODEL_BLOB; i++) EEPROM.write(EEPROM_ADDR_SLOTS + modelCount * MODEL_BLOB + i, b[i]);
  fmSlotClear(modelCount);
  memset(modelName[modelCount], 0, 12);
  strcpy(modelName[modelCount], clean);
  modelCount++;
  modelMetaSave();
  EEPROM.commit();
  return modelCount - 1;
}

int modelRename(int idx, const char* nm) {
  char clean[12];
  modelNameClean(nm, clean);
  if (idx < 0 || idx >= modelCount || clean[0] == 0) return 400;
  memset(modelName[idx], 0, 12);
  strcpy(modelName[idx], clean);
  modelMetaSave();
  EEPROM.commit();
  return 200;
}

// Hapus model. Model di atasnya bergeser turun. Model terakhir tidak bisa dihapus.
int modelDelete(int idx) {
  if (idx < 0 || idx >= modelCount) return 400;
  if (modelCount <= 1) return 409;
  modelSlotSave(activeModel);                    // simpan salinan kerja dulu
  for (int s = idx; s < modelCount - 1; s++) {
    for (int i = 0; i < MODEL_BLOB; i++)
      EEPROM.write(EEPROM_ADDR_SLOTS + s * MODEL_BLOB + i, EEPROM.read(EEPROM_ADDR_SLOTS + (s + 1) * MODEL_BLOB + i));
    for (int i = 0; i < FM_BYTES; i++)
      EEPROM.write(EEPROM_ADDR_FMIXS + s * FM_BYTES + i, EEPROM.read(EEPROM_ADDR_FMIXS + (s + 1) * FM_BYTES + i));
    memcpy(modelName[s], modelName[s + 1], 12);
  }
  memset(modelName[modelCount - 1], 0, 12);
  modelCount--;
  bool reload = false;
  if ((int)activeModel == idx) { if (activeModel >= modelCount) activeModel = modelCount - 1; reload = true; }
  else if ((int)activeModel > idx) activeModel--;
  if (reload) {
    bool got = pktMutex != NULL && xSemaphoreTake(pktMutex, (TickType_t)50) == pdTRUE;
    modelSlotLoad(activeModel);
    if (got) xSemaphoreGive(pktMutex);
  }
  EEPROM.write(EEPROM_ADDR_MODEL, activeModel);
  modelMetaSave();
  cfgSaveWorking();
  EEPROM.commit();
  return 200;
}

// Pindah model: salinan kerja model lama disimpan ke slotnya, lalu slot model baru dimuat.
void modelSwitch(int idx) {
  if (idx < 0 || idx >= modelCount || idx == (int)activeModel) return;
  modelSlotSave(activeModel);
  bool got = pktMutex != NULL && xSemaphoreTake(pktMutex, (TickType_t)50) == pdTRUE;   // TaskInputs tidak membaca setengah-setengah
  modelSlotLoad(idx);
  if (got) xSemaphoreGive(pktMutex);
  activeModel = (uint8_t)idx;
  EEPROM.write(EEPROM_ADDR_MODEL, activeModel);
  cfgSaveWorking();
  EEPROM.commit();
}

// Dipanggil sekali di setup() setelah pembacaan dan reset EEPROM
void modelInitStorage() {
  modelMetaLoad();        // jumlah dan nama model (dari aplikasi)
  if (EEPROM.read(EEPROM_ADDR_MAGIC) != EEPROM_MAGIC) {
    // Pertama kali dengan format ini: reverse dan trim lama (sudah dimuat) menjadi model 1, model lain bawaan
    uint8_t b[MODEL_BLOB];
    modelBlobDefault(b);
    bool rv[6]; int8_t tr[6];
    for (int i = 0; i < 6; i++) { rv[i] = channelReverse[i]; tr[i] = channelTrim[i]; }
    modelUnpack(b);   // field baru ke bawaan
    { uint8_t f[FM_BYTES]; fmDefault(f); fmUnpack(f); }
    for (int i = 0; i < 6; i++) { channelReverse[i] = rv[i]; channelTrim[i] = tr[i]; }   // reverse dan trim lama dipertahankan
    timerCountdownMin = 0; timerTrig = 0; timerThr = 10; timerDir = 0; battLowTenths = 64;
    activeModel = 0;
    modelSlotSave(0);
    for (int sl = 1; sl < MAX_MODELS; sl++)
      for (int i = 0; i < MODEL_BLOB; i++) EEPROM.write(EEPROM_ADDR_SLOTS + sl * MODEL_BLOB + i, b[i]);
    for (int sl = 1; sl < MAX_MODELS; sl++) fmSlotClear(sl);
    EEPROM.write(EEPROM_ADDR_MODEL, 0);
    cfgSaveWorking();
    EEPROM.write(EEPROM_ADDR_MAGIC, EEPROM_MAGIC);
    EEPROM.commit();
    return;
  }
  activeModel = EEPROM.read(EEPROM_ADDR_MODEL);
  if (activeModel >= modelCount) activeModel = 0;
  // Salinan kerja: reverse dan trim sudah dimuat setup(), sisanya dibaca di sini
  for (int i = 0; i < 4; i++) {
    dualRate[i] = inRange(EEPROM.read(EEPROM_ADDR_DR + i), 30, 100, 100);
    expoAmt[i]  = inRange(EEPROM.read(EEPROM_ADDR_EXPO + i), 0, 100, 0);
  }
  for (int i = 0; i < 6; i++) {
    epLo[i] = inRange(EEPROM.read(EEPROM_ADDR_EPLO + i), 20, 120, 100);
    epHi[i] = inRange(EEPROM.read(EEPROM_ADDR_EPHI + i), 20, 120, 100);
  }
  static const uint8_t fsDef[8] = {50, 50, 0, 50, 0, 0, 50, 50};
  for (int i = 0; i < 8; i++) failsafePct[i] = inRange(EEPROM.read(EEPROM_ADDR_FS + i), 0, 100, fsDef[i]);
  mixType = inRange(EEPROM.read(EEPROM_ADDR_MIXT), 0, 2, 0);
  mixWeight = inRange(EEPROM.read(EEPROM_ADDR_MIXW), 10, 100, 50);
  timerCountdownMin = inRange(EEPROM.read(EEPROM_ADDR_TMIN), 0, 99, 0);
  mixA = EEPROM.read(EEPROM_ADDR_MIXA); mixB = EEPROM.read(EEPROM_ADDR_MIXB);
  mixFixChannels();
  { uint8_t f[FM_BYTES]; for (int i = 0; i < FM_BYTES; i++) f[i] = EEPROM.read(EEPROM_ADDR_FMIXW + i); fmUnpack(f); }
  {
    uint8_t tt = EEPROM.read(EEPROM_ADDR_TTRIG);
    if (tt == 0xFF) {            // belum pernah diatur: versi sebelumnya hanya punya "otomatis dengan gas CH3"
      timerTrig = (EEPROM.read(EEPROM_ADDR_TAUTO) == 1) ? 3 : 0;
      timerThr = 10; timerDir = 0;
    } else {
      timerTrig = inRange(tt, 0, 8, 0);
      timerThr = inRange(EEPROM.read(EEPROM_ADDR_TTHR), 5, 95, 10);
      timerDir = inRange(EEPROM.read(EEPROM_ADDR_TDIR), 0, 1, 0);
    }
    uint8_t rt = EEPROM.read(EEPROM_ADDR_TRTRIG);
    timerRstTrig = (rt <= 8) ? rt : 0;
    timerRstDir = inRange(EEPROM.read(EEPROM_ADDR_TRDIR), 0, 1, 0);
  }
  battLowTenths = inRange(EEPROM.read(EEPROM_ADDR_BLOW), 50, 84, 64);
}

// ----- Timer -----
// Jalan kalau nilai channel pemicu melewati batas (searah pilihan), atau kalau pemicu mati dan dinyalakan manual. Hitung mundur berbunyi sekali saat habis.
void timerReset();
void timerTick(uint32_t now) {
  static uint32_t last = 0;
  uint32_t dt = last ? (uint32_t)(now - last) : 0;
  last = now;
  if (dt > 500) dt = 0;                         // frame terlalu lama (misalnya kalibrasi), jangan loncat
  bool run = timerRun;
  if (timerTrig > 0) {            // dipicu nilai channel pilihan (nilai yang dikirim ke RX)
    uint16_t cv = timerTrig <= 6 ? pkt.ch[timerTrig - 1] : (timerTrig == 7 ? pkt.ch7 : pkt.ch8);
    uint32_t thr = (uint32_t)timerThr * 4095UL / 100UL;
    run = timerDir ? (cv < thr) : (cv > thr);
  }
  if (run) timerElapsedMs += dt;
  // Pemicu RESET: sekali tiap channel MASUK ke zona (tepi), bukan selama berada di zona,
  // supaya tidak terus menerus mereset selama saklar dipegang. Memakai BATAS yang sama dengan pemicu JALAN,
  // jadi channelnya boleh dipakai bersama (misalnya saklar 3 posisi: tengah jalan, salah satu ujung reset).
  static bool rstWasIn = false;
  if (timerRstTrig > 0) {
    uint16_t cv = timerRstTrig <= 6 ? pkt.ch[timerRstTrig - 1] : (timerRstTrig == 7 ? pkt.ch7 : pkt.ch8);
    uint32_t thr = (uint32_t)timerThr * 4095UL / 100UL;
    bool inZone = timerRstDir ? (cv < thr) : (cv > thr);
    if (inZone && !rstWasIn) timerReset();
    rstWasIn = inZone;
  } else {
    rstWasIn = false;
  }
  if (timerCountdownMin > 0 && !timerBeeped && timerElapsedMs >= (uint32_t)timerCountdownMin * 60000UL) {
    timerBeeped = true;
    sndPlay(SND_TIMER);
  }
}

void timerReset() {
  timerElapsedMs = 0;
  timerBeeped = false;
}

// Sisa detik (hitung mundur) atau detik berjalan (stopwatch)
int timerShownSec() {
  uint32_t e = timerElapsedMs / 1000;
  if (timerCountdownMin > 0) {
    uint32_t tot = (uint32_t)timerCountdownMin * 60;
    return (int)(e >= tot ? 0 : tot - e);
  }
  return (int)e;
}

bool timerVisible() { return timerCountdownMin > 0 || timerElapsedMs > 0 || timerRun; }

// ----- Menu parameter umum (satu mesin untuk semua halaman) -----
// Nilai dalam satuan asli. paramUnit = langkah putaran encoder (satu klik).
int paramCount(uint8_t pg) {
  switch (pg) {
    case PG_MODEL: return modelCount;
    case PG_RATE:  return 8;
    case PG_LIMIT: return 12;
    case PG_FAIL:  return 8;
    case PG_MIX:   return 4;
    case PG_FMIX:  return 12;
    case PG_NRF:   return 1;
    case PG_BUZ:   return 3;
    case PG_DASH:  return 1;
    case PG_ANA:   return 1;
    case PG_TIMER: return 8;
    case PG_BATT:  return 1;
    case PG_OTA:   return 1;
  }
  return 1;
}

int paramGet(uint8_t pg, int i) {
  switch (pg) {
    case PG_MODEL: return (i == (int)activeModel) ? 1 : 0;
    case PG_RATE:  return i < 4 ? dualRate[i] : expoAmt[i - 4];
    case PG_LIMIT: return i < 6 ? epLo[i] : epHi[i - 6];
    case PG_FAIL:  return failsafePct[i];
    case PG_MIX:   return i == 0 ? mixType : (i == 1 ? mixA : (i == 2 ? mixB : mixWeight));
    case PG_TIMER:
      switch (i) { case 0: return timerCountdownMin; case 1: return timerTrig; case 2: return timerThr; case 3: return timerDir; case 4: return timerRun ? 1 : 0; case 5: return timerRstTrig; case 6: return timerRstDir; }
      return 0;
    case PG_FMIX:  { int m = i / 3, f = i % 3; return f == 0 ? fmSrc[m] : (f == 1 ? fmDst[m] : fmW[m]); }
    case PG_NRF:   return nrfPower;
    case PG_BUZ:   return i == 0 ? buzPassive : (i == 1 ? buzTone : 0);
    case PG_DASH:  return dashStyle;
    case PG_ANA:   return anaView;
    case PG_BATT:  return battLowTenths;
    case PG_OTA:   return 0;
  }
  return 0;
}

int paramMin(uint8_t pg, int i) {
  switch (pg) {
    case PG_MODEL: return 0;
    case PG_RATE:  return i < 4 ? 30 : 0;
    case PG_LIMIT: return 20;
    case PG_FAIL:  return 0;
    case PG_MIX:   return i == 0 ? 0 : (i == 3 ? 10 : 1);
    case PG_FMIX:  { int f = i % 3; return f == 0 ? 0 : (f == 1 ? 1 : -100); }
    case PG_NRF:   return 0;
    case PG_BUZ:   return i == 1 ? 10 : 0;
    case PG_DASH:  return 0;
    case PG_ANA:   return 0;
    case PG_TIMER: return i == 2 ? 5 : 0;
    case PG_BATT:  return 50;
    case PG_OTA:   return 0;
  }
  return 0;
}

int paramMax(uint8_t pg, int i) {
  switch (pg) {
    case PG_MODEL: return 1;
    case PG_RATE:  return 100;
    case PG_LIMIT: return 120;
    case PG_FAIL:  return 100;
    case PG_MIX:   return i == 0 ? 2 : (i == 3 ? 100 : 6);
    case PG_FMIX:  return (i % 3) == 2 ? 100 : 8;
    case PG_NRF:   return 4;
    case PG_BUZ:   return i == 1 ? 50 : 1;
    case PG_DASH:  return 4;
    case PG_ANA:   return 2;
    case PG_TIMER: return i == 0 ? 99 : ((i == 1 || i == 5) ? 8 : (i == 2 ? 95 : 1));
    case PG_BATT:  return 84;
    case PG_OTA:   return 1;
  }
  return 0;
}

// Langkah per klik encoder. Nilai min dan max selalu kelipatan langkahnya.
int paramUnit(uint8_t pg, int i) {
  switch (pg) {
    case PG_RATE: case PG_LIMIT: case PG_FAIL: return 5;
    case PG_MIX:  return i == 3 ? 5 : 1;
    case PG_FMIX: return (i % 3) == 2 ? 5 : 1;
    case PG_TIMER: return i == 2 ? 5 : 1;
  }
  return 1;
}

// Terapkan nilai ke variabel (tanpa menyimpan)
void paramSet(uint8_t pg, int i, int v) {
  v = constrain(v, paramMin(pg, i), paramMax(pg, i));
  switch (pg) {
    case PG_MODEL: if (v == 1) modelSwitch(i); break;
    case PG_RATE:  if (i < 4) dualRate[i] = (uint8_t)v; else expoAmt[i - 4] = (uint8_t)v; break;
    case PG_LIMIT: if (i < 6) epLo[i] = (uint8_t)v; else epHi[i - 6] = (uint8_t)v; break;
    case PG_FAIL:  failsafePct[i] = (uint8_t)v; break;
    case PG_MIX:   if (i == 0) mixSetType((uint8_t)v); else if (i == 3) mixWeight = (uint8_t)v; else mixSetChan(i - 1, v); break;
    case PG_TIMER:
      if (i == 0) { timerCountdownMin = (uint8_t)v; timerBeeped = false; }
      else if (i == 1) timerTrig = (uint8_t)v;
      else if (i == 2) timerThr = (uint8_t)v;
      else if (i == 3) timerDir = (uint8_t)v;
      else if (i == 4) timerRun = (v == 1);
      else if (i == 5) timerRstTrig = (uint8_t)v;
      else if (i == 6) timerRstDir = (uint8_t)v;
      else if (v == 1) timerReset();
      break;
    case PG_FMIX:  { int m = i / 3, f = i % 3; if (f == 0) fmSrc[m] = (uint8_t)v; else if (f == 1) fmDst[m] = (uint8_t)v; else fmW[m] = (int8_t)v; break; }
    case PG_NRF:   nrfPower = (uint8_t)v; break;     // dipasang ke radio oleh TaskInputs
    case PG_DASH:  dashStyle = (uint8_t)v; break;
    case PG_ANA:   anaView = (uint8_t)v; break;
    case PG_BUZ:
      if (i == 0) { buzPassive = (uint8_t)v; sndPreview(SND_MENU, 0); }
      else if (i == 1) { buzTone = (uint8_t)v; sndPreview(SND_MENU, 0); }
      else if (v == 1) sndPreview(SND_BOOT, 0);
      break;
    case PG_BATT:  battLowTenths = (uint8_t)v; break;
    case PG_OTA:   if (v == 1) otaRequest = true; break;
  }
}

// Terapkan lalu simpan. EEPROM.commit() dilakukan pemanggil.
void paramApply(uint8_t pg, int i, int v) {
  paramSet(pg, i, v);
  if (pg != PG_MODEL) cfgSaveWorking();     // pindah model menyimpannya sendiri
  if (pg == PG_NRF || pg == PG_BUZ || pg == PG_DASH) miscSave();
}

void paramLabel(uint8_t pg, int i, char* o) {
  switch (pg) {
    case PG_MODEL: strcpy(o, (i >= 0 && i < modelCount) ? modelName[i] : "-"); break;
    case PG_RATE:  sprintf(o, i < 4 ? "RATE CH%d" : "EXPO CH%d", (i % 4) + 1); break;
    case PG_LIMIT: sprintf(o, i < 6 ? "CH%d BAWAH" : "CH%d ATAS", (i % 6) + 1); break;
    case PG_FAIL:  sprintf(o, "FS CH%d", i + 1); break;
    case PG_MIX:
      if (i == 0) strcpy(o, "TIPE");
      else if (i == 3) strcpy(o, "BOBOT");
      else if (mixType == 1) strcpy(o, i == 1 ? "CH AILERON" : "CH ELEVATOR");
      else if (mixType == 2) strcpy(o, i == 1 ? "CH ELEVATOR" : "CH RUDDER");
      else strcpy(o, i == 1 ? "CH A" : "CH B");
      break;
    case PG_FMIX: {
      static const char* const FL[3] = {"DARI", "KE", "NILAI"};
      sprintf(o, "MIX%d %s", i / 3 + 1, FL[i % 3]);
      break;
    }
    case PG_NRF:   strcpy(o, "DAYA"); break;
    case PG_DASH:  strcpy(o, "GAYA"); break;
    case PG_ANA:   strcpy(o, "TAMPIL"); break;
    case PG_BUZ: {
      static const char* const BL[3] = {"JENIS", "NADA", "TES BUNYI"};
      strcpy(o, BL[i < 3 ? i : 0]);
      break;
    }
    case PG_TIMER: {
      static const char* const TL[8] = {"COUNTDOWN", "PEMICU", "BATAS", "JALAN BILA", "JALAN", "RESET PEMICU", "RESET BILA", "RESET"};
      strcpy(o, TL[i < 8 ? i : 0]);
      break;
    }
    case PG_BATT:  strcpy(o, "BATAS ALARM"); break;
    case PG_OTA:   strcpy(o, "MODE UPDATE"); break;
    default:       o[0] = 0;
  }
}

void paramText(uint8_t pg, int i, int v, char* o) {
  switch (pg) {
    case PG_MODEL: strcpy(o, v ? (i == (int)activeModel ? "AKTIF" : "PAKAI") : "-"); break;
    case PG_RATE: case PG_LIMIT: case PG_FAIL: sprintf(o, "%d%%", v); break;
    case PG_MIX:
      if (i == 0) strcpy(o, v == 0 ? "OFF" : (v == 1 ? "ELEVON" : "V-TAIL"));
      else if (i == 3) sprintf(o, "%d%%", v);
      else sprintf(o, "CH%d", v);
      break;
    case PG_TIMER:
      if (i == 0) { if (v == 0) strcpy(o, "OFF"); else sprintf(o, "%d MNT", v); }
      else if (i == 1) { if (v == 0) strcpy(o, "MANUAL"); else sprintf(o, "CH%d", v); }
      else if (i == 2) sprintf(o, "%d%%", v);
      else if (i == 3) strcpy(o, v ? "DI BAWAH" : "DI ATAS");
      else if (i == 5) { if (v == 0) strcpy(o, "MANUAL"); else sprintf(o, "CH%d", v); }
      else if (i == 6) strcpy(o, v ? "DI BAWAH" : "DI ATAS");
      else if (i == 7) strcpy(o, v ? "YA" : "-");
      else strcpy(o, v ? "ON" : "OFF");
      break;
    case PG_FMIX: {
      int f = i % 3;
      if (f == 0) { if (v == 0) strcpy(o, "OFF"); else sprintf(o, "CH%d", v); }
      else if (f == 1) sprintf(o, "CH%d", v);
      else sprintf(o, "%+d%%", v);
      break;
    }
    case PG_DASH: {
      static const char* const DN[5] = {"1 KLASIK", "2 BAR 8CH", "3 TIMER", "4 VERTIKAL", "5 GIMBAL"};
      strcpy(o, DN[v < 0 ? 0 : (v > 4 ? 4 : v)]);
      break;
    }
    case PG_NRF: {
      static const char* const NT[5] = {"MIN -18dBm", "LOW -12dBm", "HIGH -6dBm", "MAX 0dBm", "OFF MATI"};
      strcpy(o, NT[v < 0 ? 0 : (v > 4 ? 4 : v)]);
      break;
    }
    case PG_ANA:   strcpy(o, v == 0 ? "SPEKTRUM" : (v == 1 ? "KANAL WIFI" : "RIWAYAT")); break;
    case PG_BUZ:
      if (i == 0) strcpy(o, v ? "PASIF" : "AKTIF");
      else if (i == 1) sprintf(o, "%d.%dkHz", v / 10, v % 10);
      else strcpy(o, v ? "MAINKAN" : "-");
      break;
    case PG_BATT:  sprintf(o, "%d.%dV", v / 10, v % 10); break;
    case PG_OTA:   strcpy(o, v ? "MULAI" : "-"); break;
    default:       o[0] = 0;
  }
}

static const char* const PARAM_TITLES[PG_COUNT] = {
  "MODEL", "RATE DAN EXPO", "ENDPOINT", "FAILSAFE HP", "MIXING", "MIX BEBAS", "DAYA NRF", "NADA BUZZER", "TAMPILAN DASHBOARD", "ANALISIS SINYAL", "TIMER", "ALARM BATERAI", "UPDATE FW"
};

// Kurva rate dan expo di dalam kotak (x, y, w, h). Mendatar = gerak stick (-100..+100%), tegak = keluaran.
static void uiCurve(int x, int y, int w, int h, int dr, int ex) {
  display.drawRect(x, y, w, h, SSD1306_WHITE);
  for (int i = 2; i < w - 2; i += 3) display.drawPixel(x + i, y + h / 2, SSD1306_WHITE);      // garis tengah putus-putus
  for (int i = 2; i < h - 2; i += 3) display.drawPixel(x + w / 2, y + i, SSD1306_WHITE);
  for (int i = 2; i < w - 2; i += 5) display.drawPixel(x + i, y + h - 1 - i * (h - 1) / (w - 1), SSD1306_WHITE);   // acuan lurus 100%
  int iw = w - 3, ih = h - 3;
  int px0 = 0, py0 = 0;
  for (int i = 0; i <= iw; i++) {
    int32_t xin = -2048 + (int32_t)i * 4095 / iw;
    int32_t yo = 2048 + rateExpo(xin, dr, ex);
    if (yo < 0) yo = 0;
    if (yo > 4095) yo = 4095;
    int px = x + 1 + i;
    int py = y + 1 + ih - (int)(yo * ih / 4095);
    if (i > 0) { display.drawLine(px0, py0, px, py, SSD1306_WHITE); display.drawLine(px0, py0 + 1, px, py + 1, SSD1306_WHITE); }   // garis tebal 2 piksel
    px0 = px; py0 = py;
  }
}

// Halaman RATE DAN EXPO: daftar di kiri, grafik kurva channel terpilih di kanan (ikut berubah saat diedit)
static void drawParamRate() {
  display.clearDisplay();
  const int cnt = 8;
  char cn[10];
  uiCounter(cn, paramIndex, cnt);
  uiHeader(PARAM_TITLES[PG_RATE], cn);
  int top = uiTop(paramIndex, cnt);
  char lb[12], tx[16];
  for (int r = 0; r < UI_ROWS && top + r < cnt; r++) {
    int i = top + r;
    int y = UI_ROW_Y0 + r * UI_ROW_H;
    bool sel = (i == paramIndex);
    bool ed = sel && reverseEditMode;
    uint16_t fg = SSD1306_WHITE;
    if (sel && !ed) { display.fillRoundRect(1, y, 80, UI_ROW_H, 2, SSD1306_WHITE); fg = SSD1306_BLACK; }
    else if (ed)    display.drawRoundRect(1, y, 80, UI_ROW_H, 2, SSD1306_WHITE);
    sprintf(lb, i < 4 ? "RATE %d" : "EXPO %d", (i % 4) + 1);
    display.setTextColor(fg);
    display.setCursor(6, y + 1);
    display.print(lb);
    int v = ed ? paramEdit : paramGet(PG_RATE, i);
    sprintf(tx, "%d%%", v);
    uiValue(77, y, tx, ed, fg);
  }
  display.setTextColor(SSD1306_WHITE);
  int gc = paramIndex % 4;
  int dr = dualRate[gc], ex = expoAmt[gc];
  if (reverseEditMode) { if (paramIndex < 4) dr = paramEdit; else ex = paramEdit; }
  uiCurve(85, 13, 41, 37, dr, ex);
  sprintf(lb, "CH%d", gc + 1);
  display.setCursor(85, 54);
  display.print(lb);
  uiPrintR(126, 54, ex > 0 ? "EXP" : "LIN");
  display.display();
}

// Tambahan gambar untuk halaman DAYA NRF: batang kekuatan dan perkiraan jangkauan relatif
static void drawNrfExtra(int v) {
  for (int i = 0; i < 4; i++) {
    int h = 8 + i * 6, x = 8 + i * 14, y = 52 - h;
    if (v < 4 && i <= v) display.fillRect(x, y, 10, h, SSD1306_WHITE);
    else        display.drawRect(x, y, 10, h, SSD1306_WHITE);
  }
  static const char* const RG[5] = {"~12%", "~25%", "~50%", "100%", "MATI"};
  display.setCursor(72, 28);
  display.print("JANGKAU");
  display.setTextSize(2);
  display.setCursor(72, 38);
  display.print(RG[v < 0 ? 0 : (v > 4 ? 4 : v)]);
  display.setTextSize(1);
  display.setCursor(2, 56);
  display.print("Hanya TX, RX tetap");
}

// Rata-rata aktivitas di sekitar satu kanal NRF
static int anaAvg(int c, int r) {
  int a = c - r < 0 ? 0 : c - r, b = c + r > 125 ? 125 : c + r, s = 0;
  for (int i = a; i <= b; i++) s += anaLevel[i];
  return s / (b - a + 1);
}

// Kanal NRF paling bersih di seluruh pita (dipakai sebagai saran, tidak mengubah kanal TX sendiri)
static int anaCleanest() {
  int best = 0, bestLv = 999;
  for (int c = 3; c < 123; c++) {                 // jauhi ujung pita yang kurang akurat
    int lv = anaAvg(c, 2);
    if (lv < bestLv) { bestLv = lv; best = c; }
  }
  return best;
}

// Tambah satu baris riwayat (dipanggil tiap kali sapuan sempat menyegarkan seluruh kanal)
static void anaWfPush() {
  uint32_t now = millis();
  if (now - anaWfLast < 220) return;
  anaWfLast = now;
  memmove(&anaWf[1], &anaWf[0], (ANA_WF_ROWS - 1) * 126);
  for (int c = 0; c < 126; c++) {
    int lv = anaLevel[c] * 4 / 101;                // 0..3
    anaWf[0][c] = (uint8_t)(lv > 3 ? 3 : lv);
  }
}

// Halaman ANALISIS SINYAL: spektrum 2,4 GHz atau kepadatan 13 kanal WiFi. Berjalan selama halaman terbuka.
static void drawParamAna() {
  anaStart();
  display.clearDisplay();
  int view = reverseEditMode ? paramEdit : anaView;
  static const char* const VN[3] = {"SPEKTRUM", "WIFI", "RIWAYAT"};
  uiHeader("SINYAL 2.4G", VN[view]);
  char tx[24];
  display.setTextSize(1);
  if (view == 2) {
    // Riwayat (waterfall): tiap baris cuplikan masa lalu, paling baru di atas, turun ke bawah seiring waktu
    const int x0 = 1, y0 = 13, rowH = 2;
    for (int r = 0; r < ANA_WF_ROWS; r++) {
      int y = y0 + r * rowH;
      for (int c = 0; c < 126; c++) {
        uint8_t lv = anaWf[r][c];
        if (lv == 0) continue;
        bool on;
        if (lv == 3) on = true;
        else if (lv == 2) on = ((c + r) & 1) == 0;
        else on = ((c & 1) == 0 && (r & 1) == 0);                        // lv 1: jarang-jarang
        if (on) display.fillRect(x0 + c, y, 1, rowH - 1, SSD1306_WHITE);
      }
    }
    display.drawFastHLine(0, y0 - 2, 128, SSD1306_WHITE);
    int cl = anaCleanest();
    sprintf(tx, "~%ddtk  Bersih:%d", (ANA_WF_ROWS * 220) / 1000, cl);
    display.setCursor(2, 56);
    display.print(tx);
  } else if (view == 0) {
    const int x0 = 1, yb = 44, H = 27;
    for (int g = 1; g <= 3; g++) for (int x = 2; x < 126; x += 4) display.drawPixel(x, yb - H * g / 3, SSD1306_WHITE);   // garis bantu 33/66/100 persen
    int prevTop = -1;
    for (int c = 0; c < 126; c++) {
      int l0 = anaLevel[c > 0 ? c - 1 : 0], l1 = anaLevel[c], l2 = anaLevel[c < 125 ? c + 1 : 125];
      int h = ((l0 + 2 * l1 + l2) * H) / 400;                         // rata-rata tiga kanal: kurva halus
      int top = yb - h;
      if (h > 0) {
        for (int y = top + 1; y <= yb; y++) if (y > yb - h / 3 || ((c + y) & 1)) display.drawPixel(x0 + c, y, SSD1306_WHITE);   // isi berpola, bagian bawah padat
      }
      if (prevTop >= 0) display.drawLine(x0 + c - 1, prevTop, x0 + c, top, SSD1306_WHITE);                                    // garis tepi bersambung
      else display.drawPixel(x0 + c, top, SSD1306_WHITE);
      prevTop = top;
      int ph = (anaPeak[c] * H) / 100;
      if (ph > h + 1 && (c & 1) == 0) display.drawPixel(x0 + c, yb - ph, SSD1306_WHITE);                                       // puncak titik-titik
    }
    display.drawFastHLine(0, yb + 1, 128, SSD1306_WHITE);
    static const uint8_t WF[3] = {12, 37, 62};                         // kanal WiFi 1, 6, 11 (2412, 2437, 2462 MHz)
    static const char* const WN[3] = {"1", "6", "11"};
    for (int i = 0; i < 3; i++) { display.setCursor(x0 + WF[i] - (i == 2 ? 5 : 2), yb + 3); display.print(WN[i]); }
    static const uint8_t BL[3] = {2, 26, 80};                          // iklan Bluetooth 37, 38, 39
    for (int i = 0; i < 3; i++) { display.setCursor(x0 + BL[i] - 2, yb + 3); display.print("B"); }
    display.setCursor(x0 + 104, yb + 3);
    display.print("TX");
    display.fillTriangle(x0 + 110 - 3, 13, x0 + 110 + 3, 13, x0 + 110, 18, SSD1306_WHITE);      // kanal 110 milik pemancar ini
    int a = anaAvg(110, 2);
    int cl = anaCleanest();
    sprintf(tx, "110:%s Bersih:%d", a < 8 ? "bersih" : (a < 30 ? "agak" : "RAMAI"), cl);
    display.setCursor(2, 56);
    display.print(tx);
  } else if (view == 1) {
    int load[13], best = 0, worst = 0;
    for (int w = 0; w < 13; w++) {
      load[w] = anaAvg(12 + 5 * w, 8);                                 // lebar kanal WiFi 22 MHz
      if (load[w] < load[best]) best = w;
      if (load[w] > load[worst]) worst = w;
    }
    const int yb = 44, H = 27;
    static int16_t shown[13];                                          // tinggi bar yang bergerak halus menuju nilai baru (x16)
    for (int w = 0; w < 13; w++) {
      int t = load[w] * H * 16 / 50;
      if (t > H * 16) t = H * 16;
      shown[w] += (t - shown[w]) / 3 + ((t > shown[w]) ? 1 : (t < shown[w] ? -1 : 0));
      int h = (shown[w] + 8) / 16;
      int x = 4 + w * 9;
      if (h < 1) h = 1;
      if (w == best) { display.drawRect(x, yb - h, 7, h + 1, SSD1306_WHITE); }
      else {
        display.fillRect(x, yb - h + 1, 7, h, SSD1306_WHITE);
        display.drawPixel(x, yb - h + 1, SSD1306_BLACK); display.drawPixel(x + 6, yb - h + 1, SSD1306_BLACK);   // sudut atas membulat
      }
    }
    display.drawFastHLine(0, yb + 1, 128, SSD1306_WHITE);
    static const uint8_t LB[5] = {0, 5, 10, 12, 8};
    static const char* const LN[5] = {"1", "6", "11", "13", "9"};
    for (int i = 0; i < 3; i++) { display.setCursor(4 + LB[i] * 9 + (i == 2 ? 0 : 1), yb + 3); display.print(LN[i]); }
    display.setCursor(4 + 12 * 9, yb + 3);
    display.print("13");
    sprintf(tx, "Terbaik:%d  Ramai:%d", best + 1, worst + 1);
    display.setCursor(2, 56);
    display.print(tx);
  }
  if (reverseEditMode) { display.drawRoundRect(0, 11, 128, 53, 2, SSD1306_WHITE); }
  display.display();
}

void drawParamMenu() {
  if (paramPage == PG_RATE) { drawParamRate(); return; }
  if (paramPage == PG_ANA) { drawParamAna(); return; }
  if (paramPage == PG_DASH) {          // pratinjau langsung gaya yang dipilih, ikut putaran encoder tanpa perlu klik
    int dv = reverseEditMode ? paramEdit : dashStyle;
    dashDraw(dv);
    char nm[16];
    paramText(PG_DASH, 0, dv, nm);
    int w = (int)strlen(nm) * 6 + 4;
    display.fillRect(64 - w / 2, 56, w, 8, SSD1306_BLACK);
    display.drawRect(64 - w / 2 - 1, 55, w + 2, 9, SSD1306_WHITE);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(64 - w / 2 + 2, 56);
    display.print(nm);
    display.display();
    return;
  }
  display.clearDisplay();
  int cnt = paramCount(paramPage);
  char cn[10];
  cn[0] = 0;
  if (cnt > 1) uiCounter(cn, paramIndex, cnt);
  uiHeader(PARAM_TITLES[paramPage], cn);

  int top = uiTop(paramIndex, cnt);
  bool scr = cnt > UI_ROWS;
  char lb[20], tx[16];
  for (int r = 0; r < UI_ROWS && top + r < cnt; r++) {
    int i = top + r;
    int y = UI_ROW_Y0 + r * UI_ROW_H;
    bool sel = (i == paramIndex);
    bool ed = sel && reverseEditMode;
    uint16_t fg = uiRow(y, sel, ed, scr);
    paramLabel(paramPage, i, lb);
    display.setTextColor(fg);
    display.setCursor(6, y + 1);
    display.print(lb);
    int v = ed ? paramEdit : paramGet(paramPage, i);
    int xr = scr ? 120 : 123;
    if (paramPage == PG_TIMER && i == 4) {       // JALAN: tombol geser
      uiPill(xr, y, v != 0, fg);
      if (ed) display.fillTriangle(xr - 22, y + 5, xr - 19, y + 3, xr - 19, y + 7, SSD1306_WHITE);
    } else {
      paramText(paramPage, i, v, tx);
      uiValue(xr, y, tx, ed, fg);
    }
  }
  display.setTextColor(SSD1306_WHITE);
  if (paramPage == PG_NRF) drawNrfExtra(reverseEditMode ? paramEdit : nrfPower);
  uiScrollbar(top, cnt);
  display.display();
}

// ================= NAVIGASI MENU =================
// ==== NAV_CORE_BEGIN ====  (blok ini diuji di simulator, jangan diubah sembarangan)
//
// Aturan tombol (SW encoder dan BTN digabung jadi satu tombol):
//   KLIK = ditekan lalu dilepas sebelum 300 ms. Aksi dijalankan saat dilepas.
//   HOLD = ditahan sampai 300 ms. Aksi langsung dijalankan saat 300 ms tercapai,
//          lalu tombol diabaikan sampai benar-benar dilepas.
//
//   DASHBOARD : KLIK atau HOLD  -> DAFTAR MENU
//   DAFTAR    : KLIK            -> masuk menu terpilih
//               HOLD            -> DASHBOARD
//   BRIGHTNESS / CH READOUT     : putar = atur, HOLD = simpan lalu ke DAFTAR
//   REVERSE / TRIM / THEME /    : KLIK = mulai edit (kedip) / selesai edit
//   KONEKTIVITAS
//                                 HOLD = simpan lalu ke DAFTAR
//   KALIBRASI : KLIK = lanjut langkah, HOLD = batal ke DAFTAR
//
// Tombol dibaca task sendiri tiap 2 ms (TaskKey), jadi klik cepat tidak hilang
// walaupun satu frame layar lama. Hasilnya (KLIK/HOLD) masuk antrean dan diproses layar.
// Selama tombol ditekan, putaran encoder diabaikan (knob EC11 sering ikut bergeser saat ditekan).

#define CLICK_MAX_MS          300   // < 300 ms = KLIK, >= 300 ms = HOLD
#define KEY_PRESS_DEBOUNCE    20    // tekan harus stabil 20 ms
#define KEY_RELEASE_DEBOUNCE  40    // lepas harus stabil 40 ms
#define TRIM_LIMIT            100   // trim -100..+100 (x10 = +-1000 nilai ADC), aman untuk int8_t


int keyUpdate(bool raw, uint32_t now) {
  if (raw != navKey_raw) {
    // Sentuhan pertama dicatat hanya kalau sebelumnya tombol sudah lepas cukup lama
    // (pantulan kontak tidak menggeser waktu sentuhan pertama)
    if (raw && !navKey_stable && (now - navKey_rawChange) >= KEY_PRESS_DEBOUNCE) navKey_pressEdge = now;
    navKey_raw = raw;
    navKey_rawChange = now;
  }

  int ev = KEY_NONE;
  uint32_t need = navKey_raw ? KEY_PRESS_DEBOUNCE : KEY_RELEASE_DEBOUNCE;

  if (navKey_raw != navKey_stable && (now - navKey_rawChange) >= need) {
    navKey_stable = navKey_raw;
    if (navKey_stable) {
      // ===== DITEKAN =====
      if (!navKey_waitRelease) { navKey_tracking = true; navKey_pressStart = navKey_rawChange; }
    } else {
      // ===== DILEPAS =====
      if (navKey_tracking) {
        uint32_t dur = navKey_rawChange - navKey_pressStart;   // lama tekan sebenarnya
        ev = (dur < CLICK_MAX_MS) ? KEY_CLICK : KEY_HOLD;
      }
      navKey_tracking = false;
      navKey_waitRelease = false;
    }
  }

  // HOLD langsung saat 300 ms tercapai (hanya kalau tombol memang masih ditekan)
  if (navKey_tracking && navKey_stable && navKey_raw && (now - navKey_pressStart) >= CLICK_MAX_MS) {
    ev = KEY_HOLD;
    navKey_tracking = false;
    navKey_waitRelease = true;
  }
  return ev;
}

// Antrean kejadian tombol: diisi TaskKey, dibaca TaskDisplay
volatile uint8_t keyQ[8];
volatile uint8_t keyQHead = 0, keyQTail = 0;

void keyPoll(bool raw, uint32_t now) {
  int ev = keyUpdate(raw, now);
  if (ev != KEY_NONE) {
    uint8_t n = (keyQHead + 1) & 7;
    if (n != keyQTail) { keyQ[keyQHead] = ev; keyQHead = n; }
  }
}

bool keyPending() { return keyQHead != keyQTail; }

int keyPop() {
  if (keyQTail == keyQHead) return KEY_NONE;
  int ev = keyQ[keyQTail];
  keyQTail = (keyQTail + 1) & 7;
  return ev;
}

void gotoMode(uint8_t m) {
  reverseEditMode = false;
  currentMode = m;
}

// Simpan nilai yang sedang diedit (kalau ada), lalu keluar dari mode edit
void commitEdit() {
  if (!reverseEditMode) return;

  if (currentMode == MODE_REVERSE) {
    EEPROM.write(EEPROM_ADDR_REVERSE + reverseIndex, channelReverse[reverseIndex]);
    encoderPos = reverseIndex;
  } else if (currentMode == MODE_TRIM) {
    EEPROM.write(EEPROM_ADDR_TRIM + trimIndex, channelTrim[trimIndex] + 128);
    encoderPos = trimIndex;
  } else if (currentMode == MODE_THEME) {
    EEPROM.write(EEPROM_ADDR_OUTPUT, darkMode);
    encoderPos = themeIndex;
  } else if (currentMode == MODE_CONN) {
    connApply(connIndex, connEdit);
    encoderPos = connIndex;
  } else if (currentMode == MODE_SOUND) {
    soundApply(soundIndex, soundEdit);
    encoderPos = soundIndex;
  } else if (currentMode == MODE_PARAM) {
    paramApply(paramPage, paramIndex, paramEdit);
    encoderPos = paramIndex;
  }

  EEPROM.commit();
  reverseEditMode = false;
}

// Simpan brightness ke EEPROM kalau berubah
void saveBrightness() {
  if (brightnessLevel != lastBrightnessSaved) {
    EEPROM.write(EEPROM_ADDR_BRIGHT, brightnessLevel);
    EEPROM.commit();
    lastBrightnessSaved = brightnessLevel;
  }
}

void onKeyClick() {
  if (currentMode == MODE_DASHBOARD) {
    sndPlay(SND_MENU);
    gotoMode(MODE_LIST);
  }
  else if (currentMode == MODE_LIST) {
    sndPlay(SND_MENU);
    uint8_t target = LIST_MODES[listIndex];
    if (target >= 100) {
      paramPage = target - 100; paramIndex = 0; gotoMode(MODE_PARAM);
      if (paramPage == PG_DASH) { reverseEditMode = true; paramEdit = dashStyle; encoderPos = dashStyle; }   // gaya dashboard: putar langsung berubah, tidak perlu klik dulu
    }
    else gotoMode(target);
  }
  else if (currentMode == MODE_PARAM && paramPage == PG_DASH) {
    // Gaya dashboard: klik batal (kembali tanpa menyimpan gaya yang baru dilihat). Simpan hanya lewat tahan tombol.
    sndPlay(SND_CLICK);
    gotoMode(MODE_LIST);
  }
  else if (currentMode == MODE_REVERSE ||
           currentMode == MODE_TRIM ||
           currentMode == MODE_THEME ||
           currentMode == MODE_CONN ||
           currentMode == MODE_SOUND ||
           currentMode == MODE_PARAM) {
    if (!reverseEditMode) {
      // ===== MULAI EDIT (item terpilih kedip) =====
      reverseEditMode = true;
      if (currentMode == MODE_REVERSE)    encoderPos = channelReverse[reverseIndex] ? 1 : 0;
      else if (currentMode == MODE_TRIM)  encoderPos = channelTrim[trimIndex];
      else if (currentMode == MODE_CONN)  { connEdit = connCurrentValue(connIndex); encoderPos = connEdit; }
      else if (currentMode == MODE_SOUND) { soundEdit = soundCurrentValue(soundIndex); encoderPos = soundEdit; }
      else if (currentMode == MODE_PARAM) { paramEdit = paramGet(paramPage, paramIndex); encoderPos = paramEdit / paramUnit(paramPage, paramIndex); }
      else                                encoderPos = themeIndex;
      sndPlay(SND_CLICK);
    } else {
      // ===== SELESAI EDIT, kembali memilih item =====
      commitEdit();
      sndPlay(SND_CLICK);
    }
  }
  else if (currentMode == MODE_CALIB) {
    calibPress();            // kalibrasi punya bunyi sendiri (berhasil atau belum cukup)
  }
  else {
    sndPlay(SND_CLICK);      // BRIGHTNESS dan CHANNEL READOUT: klik tidak mengubah apa-apa
  }
}

void onKeyHold() {
  sndPlay(SND_MENU);
  if (currentMode == MODE_DASHBOARD) {
    gotoMode(MODE_LIST);
  }
  else if (currentMode == MODE_LIST) {
    gotoMode(MODE_DASHBOARD);
  }
  else {
    commitEdit();                                   // simpan kalau sedang edit
    if (currentMode == MODE_BRIGHTNESS) saveBrightness();
    gotoMode(MODE_LIST);                            // kembali ke daftar menu
  }
}

// Terapkan posisi encoder ke menu yang sedang aktif
void navApplyEncoder() {
  if (currentMode == MODE_LIST) {
    encoderPos = constrain(encoderPos, 0, LIST_COUNT - 1);
    listIndex = encoderPos;
  }
  else if (currentMode == MODE_REVERSE) {
    if (!reverseEditMode) {
      encoderPos = constrain(encoderPos, 0, 5);
      reverseIndex = encoderPos;
    } else {
      encoderPos = constrain(encoderPos, 0, 1);
      channelReverse[reverseIndex] = (encoderPos == 1);
    }
  }
  else if (currentMode == MODE_TRIM) {
    if (!reverseEditMode) {
      encoderPos = constrain(encoderPos, 0, 5);
      trimIndex = encoderPos;
    } else {
      encoderPos = constrain(encoderPos, -TRIM_LIMIT, TRIM_LIMIT);
      channelTrim[trimIndex] = (int8_t)encoderPos;   // disimpan ke EEPROM saat edit selesai
    }
  }
  else if (currentMode == MODE_THEME) {
    static int lastInvertSent = -1;
    encoderPos = constrain(encoderPos, 0, 1);
    themeIndex = encoderPos;
    if (reverseEditMode) {
      darkMode = (themeIndex == 1);
      if (lastInvertSent != (int)darkMode) {
        display.invertDisplay(!darkMode);
        lastInvertSent = (int)darkMode;
      }
    }
  }
  else if (currentMode == MODE_CONN) {
    if (!reverseEditMode) {
      encoderPos = constrain(encoderPos, 0, 2);
      connIndex = encoderPos;
    } else {
      encoderPos = constrain(encoderPos, 0, 1);
      connEdit = encoderPos;
    }
  }
  else if (currentMode == MODE_SOUND) {
    if (!reverseEditMode) {
      encoderPos = constrain(encoderPos, 0, SOUND_ITEMS - 1);
      soundIndex = encoderPos;
    } else if (soundIndex == 0) {
      encoderPos = constrain(encoderPos, 1, VOL_MAX);
      if (encoderPos != soundEdit) {
        soundEdit = encoderPos;
        sndPreview(SND_CLICK, (uint8_t)soundEdit);    // dengar volumenya langsung
      }
    } else {
      encoderPos = constrain(encoderPos, 0, 1);
      soundEdit = encoderPos;
    }
  }
  else if (currentMode == MODE_PARAM) {
    if (!reverseEditMode) {
      encoderPos = constrain(encoderPos, 0, paramCount(paramPage) - 1);
      paramIndex = encoderPos;
    } else {
      int u = paramUnit(paramPage, paramIndex);
      encoderPos = constrain(encoderPos, paramMin(paramPage, paramIndex) / u, paramMax(paramPage, paramIndex) / u);
      paramEdit = encoderPos * u;
    }
  }
  else if (currentMode == MODE_BRIGHTNESS) {
    encoderPos = constrain(encoderPos, 0, BRIGHTNESS_MAX);
    brightnessLevel = encoderPos;
    oledBrightness = map(encoderPos, 0, BRIGHTNESS_MAX, 0, 255);
  }

}

// Mode yang memakai putaran encoder (suara scroll hanya di sini).
// Volume di menu Suara punya contoh bunyi sendiri, jadi tidak ikut.
bool scrollSoundMode() {
  switch (currentMode) {
    case MODE_LIST: case MODE_BRIGHTNESS: case MODE_REVERSE:
    case MODE_TRIM: case MODE_THEME: case MODE_CONN: case MODE_PARAM:
      return true;
    case MODE_SOUND:
      return !(reverseEditMode && soundIndex == 0);
  }
  return false;
}

// Dipanggil sekali per frame dari TaskDisplay
void navTick(uint32_t now) {
  static uint8_t lastModeMemory = 255;
  static int sndPos = 0;
  static bool encFrozen = false;
  static int encAtPress = 0;
  // Catatan posisi encoder di 8 frame terakhir saat tombol bebas (nilai dan waktu)
  static int freePos[8];
  static uint32_t freeMs[8];
  static uint8_t freeIdx = 0;

  // ===== Kunci encoder selama tombol ditekan =====
  // Langkah encoder sejak 20 ms sebelum sentuhan pertama dianggap knob ikut goyang lalu dibatalkan:
  // posisi dikembalikan ke catatan frame terakhir sebelum itu, ditambah putaran sah sesudahnya.
  // 'sibuk' juga mencakup 60 ms sejak sentuhan pertama, supaya pantulan kontak di awal
  // tekanan tidak sempat dianggap tombol lepas.
  bool busy = navKey_raw || navKey_stable || keyPending() ||
              (uint32_t)(now - navKey_pressEdge) < 60;
  if (busy) {
    if (!encFrozen) {
      uint32_t w = navKey_pressEdge - 20;
      bool wobble = false;
      for (int i = 0; i < 8; i++) {
        if (encHistD[i] != 0 && (int32_t)(encHistT[i] - w) >= 0 && (int32_t)(now - encHistT[i]) >= 0) wobble = true;
      }
      if (wobble) {
        // catatan terbaru yang diambil sebelum jendela goyang
        int best = -1;
        for (int i = 0; i < 8; i++) {
          if (freeMs[i] == 0 || (int32_t)(freeMs[i] - w) >= 0) continue;
          if (best < 0 || (int32_t)(freeMs[i] - freeMs[best]) > 0) best = i;
        }
        if (best >= 0) {
          int pos = freePos[best];
          for (int i = 0; i < 8; i++) {
            uint32_t t = encHistT[i];
            if (encHistD[i] != 0 && (int32_t)(t - freeMs[best]) > 0 && (int32_t)(t - w) < 0) pos += encHistD[i];
          }
          encoderPos = pos;
        } else {
          // Tidak ada catatan sebelum jendela: pakai catatan paling awal, buang goyangan yang sudah masuk
          int first = -1;
          for (int i = 0; i < 8; i++) {
            if (freeMs[i] == 0) continue;
            if (first < 0 || (int32_t)(freeMs[i] - freeMs[first]) < 0) first = i;
          }
          if (first >= 0) {
            int pos = freePos[first];
            for (int i = 0; i < 8; i++) {
              uint32_t t = encHistT[i];
              if (encHistD[i] != 0 && (int32_t)(t - w) >= 0 && (int32_t)(freeMs[first] - t) >= 0) pos -= encHistD[i];
            }
            encoderPos = pos;
          }
        }
      }
      encAtPress = encoderPos;
      encFrozen = true;
      // Catatan lama tidak berlaku lagi setelah tekanan ini (encoder bisa diubah oleh aksi tombol)
      for (int i = 0; i < 8; i++) freeMs[i] = 0;
    } else {
      encoderPos = encAtPress;
    }
  }

  // Indeks menu diperbarui dulu, supaya klik memakai posisi kursor yang sudah dikoreksi
  navApplyEncoder();

  // Suara scroll: posisi berubah karena putaran (bukan karena aksi program) dan tombol tidak sedang ditekan
  if (!busy && currentMode == lastModeMemory && encoderPos != sndPos && scrollSoundMode()) sndPlay(SND_SCROLL);

  // ===== Proses satu kejadian tombol per frame =====
  int ev = keyPop();
  if (ev == KEY_CLICK) {
    DBG("[%lu] KLIK (mode %d)\n", (unsigned long)now, currentMode);
    onKeyClick();
  } else if (ev == KEY_HOLD) {
    DBG("[%lu] HOLD (mode %d)\n", (unsigned long)now, currentMode);
    onKeyHold();
  }

  // Sinkronisasi encoder saat pindah menu, sebelum kode per menu dijalankan
  if (currentMode != lastModeMemory) {
    if (currentMode == MODE_LIST)            encoderPos = listIndex;
    else if (currentMode == MODE_REVERSE)    encoderPos = reverseIndex;
    else if (currentMode == MODE_TRIM)       encoderPos = trimIndex;
    else if (currentMode == MODE_THEME)      { themeIndex = darkMode ? 1 : 0; encoderPos = themeIndex; }
    else if (currentMode == MODE_BRIGHTNESS) encoderPos = brightnessLevel;
    else if (currentMode == MODE_CALIB)      calibBegin();
    else if (currentMode == MODE_CONN)       encoderPos = connIndex;
    else if (currentMode == MODE_SOUND)      encoderPos = soundIndex;
    else if (currentMode == MODE_PARAM)      encoderPos = paramIndex;
    lastModeMemory = currentMode;
  }

  navApplyEncoder();

  // Setelah aksi (misalnya HOLD pindah menu) posisi encoder baru yang dikunci
  if (busy || navKey_raw || navKey_stable || keyPending()) {
    if (ev != KEY_NONE) encAtPress = encoderPos;
  } else {
    encFrozen = false;
    freePos[freeIdx & 7] = encoderPos;
    freeMs[freeIdx & 7] = now ? now : 1;
    freeIdx++;
  }

  sndPos = encoderPos;       // perubahan oleh aksi program tidak dianggap putaran
}
// ==== NAV_CORE_END ====

// ================= BLUETOOTH (BLE, Nordic UART Service) =================
// HP menulis ke karakteristik RX. Dua format diterima:
//  1) Teks : "c1,c2,c3,c4,c5,c6,c7,c8\n"  nilai 0..4095, boleh kurang dari 8 nilai (sisanya tetap)
//     CH5/CH6 dijadikan 0 atau 4095 (seperti switch fisik), CH7/CH8 boleh nilai berapa saja
//  2) Biner: 18 byte = 0xA5, 8 x uint16 little endian, lalu XOR byte 1..16
// Remot mengirim status lewat notify tiap 500 ms: "V7.42,H1,L5,K1\n"
//  V = baterai remot, H = sumber input (1 = HP), L = bar link NRF (0..5), K = data HP diterima
#define NUS_SERVICE "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_RX      "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define NUS_TX      "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

NimBLEServer* bleServer = nullptr;
NimBLECharacteristic* bleTx = nullptr;
volatile uint16_t bleConnHandle = 0;
bool bleInited = false;
volatile bool bleNeedAdv = false;
volatile uint32_t bleDiscMs = 0;

static char bleLine[128];
static uint8_t bleLineLen = 0;

void btStoreChannels(const uint16_t* v, const bool* has) {
  portENTER_CRITICAL(&btMux);
  for (int i = 0; i < 8; i++) if (has[i]) btCh[i] = v[i];
  btLastRxMs = millis();
  btRxCount++;
  portEXIT_CRITICAL(&btMux);
}

// Satu baris teks berisi angka dipisah koma
void btParseLine(const char* s) {
  uint16_t v[8]; bool has[8] = {false};
  int n = 0; long cur = 0; bool inNum = false;
  for (const char* p = s; ; p++) {
    char c = *p;
    if (c >= '0' && c <= '9') { cur = cur * 10 + (c - '0'); if (cur > 99999) cur = 99999; inNum = true; }
    else if (c == ',' || c == ' ' || c == '\0') {
      if (inNum && n < 8) { v[n] = (uint16_t)constrain(cur, 0L, 4095L); has[n] = true; n++; }
      cur = 0; inNum = false;
      if (c == '\0') break;
    }
    else { return; }   // karakter lain: baris diabaikan
  }
  if (n > 0) btStoreChannels(v, has);
}

// Perintah teks dari aplikasi (diawali huruf) masuk antrean, dijalankan TaskConn supaya satu konteks dengan web
#define BTQ_N   6
#define BTQ_LEN 128
static char btQ[BTQ_N][BTQ_LEN];
static volatile uint8_t btQHead = 0, btQTail = 0;

static void btQueueCmd(const char* s) {
  portENTER_CRITICAL(&btMux);
  uint8_t nx = (uint8_t)((btQHead + 1) % BTQ_N);
  if (nx != btQTail) {
    strncpy(btQ[btQHead], s, BTQ_LEN - 1);
    btQ[btQHead][BTQ_LEN - 1] = 0;
    btQHead = nx;
  }
  portEXIT_CRITICAL(&btMux);
}

static bool btQueuePop(char* out) {
  bool got = false;
  portENTER_CRITICAL(&btMux);
  if (btQTail != btQHead) {
    memcpy(out, btQ[btQTail], BTQ_LEN);
    btQTail = (uint8_t)((btQTail + 1) % BTQ_N);
    got = true;
  }
  portEXIT_CRITICAL(&btMux);
  return got;
}

void btHandleData(const uint8_t* d, size_t len) {
  // Mode update firmware lewat Bluetooth: semua byte adalah isi berkas
  if (bleRaw) {
    uint32_t used = otaHead - otaTail;
    if (used + len > OTA_RING) return;          // aplikasi melanggar jendela kirim: buang (hasil akhir ditolak oleh pemeriksaan berkas)
    uint32_t h = otaHead;
    for (size_t i = 0; i < len; i++) otaRing[(h + i) % OTA_RING] = d[i];
    otaLastRx = millis();
    otaHead = h + (uint32_t)len;
    return;
  }
  // Format biner
  if (len == 18 && d[0] == 0xA5) {
    uint8_t x = 0;
    for (int i = 1; i <= 16; i++) x ^= d[i];
    if (x != d[17]) return;
    uint16_t v[8]; bool has[8];
    for (int i = 0; i < 8; i++) {
      v[i] = (uint16_t)(d[1 + i * 2] | (d[2 + i * 2] << 8));
      if (v[i] > 4095) v[i] = 4095;
      has[i] = true;
    }
    btStoreChannels(v, has);
    return;
  }
  // Format teks, bisa terpotong jadi beberapa paket, baris diakhiri '\n'
  for (size_t i = 0; i < len; i++) {
    char c = (char)d[i];
    if (c == '\n' || c == '\r' || c == ';') {
      if (bleLineLen > 0) {
        bleLine[bleLineLen] = '\0';
        char c0 = bleLine[0];
        if ((c0 >= 'A' && c0 <= 'Z') || (c0 >= 'a' && c0 <= 'z')) btQueueCmd(bleLine);   // perintah dari aplikasi
        else btParseLine(bleLine);                                                         // angka channel
      }
      bleLineLen = 0;
    } else if (bleLineLen < sizeof(bleLine) - 1) {
      bleLine[bleLineLen++] = c;
    } else {
      bleLineLen = 0;   // baris terlalu panjang, buang
    }
  }
}

class EdgeServerCB : public NimBLEServerCallbacks {
 public:
  void onConnect(NimBLEServer* s, NimBLEConnInfo& info) override {
    btConnected = true;
    bleConnHandle = info.getConnHandle();
    // Minta jeda koneksi singkat (15 sampai 25 ms, tanpa latency) supaya kendali dari HP tetap cepat dan responsif
    s->updateConnParams(info.getConnHandle(), 0x0C, 0x14, 0, 400);
    bleLineLen = 0;
  }
  void onDisconnect(NimBLEServer* s, NimBLEConnInfo& info, int reason) override {
    btConnected = false;
    bleRaw = false;
    if (inputSrc == INPUT_HP) inputSrc = INPUT_REMOT;   // Bluetooth putus: kendali langsung kembali ke joystick remot
    bleLineLen = 0;
    bleDiscMs = millis();
    bleNeedAdv = true;          // iklankan lagi supaya HP bisa menyambung ulang
  }
};

class EdgeRxCB : public NimBLECharacteristicCallbacks {
 public:
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& info) override {
    NimBLEAttValue v = c->getValue();
    btHandleData((const uint8_t*)v.data(), v.size());
  }
};

void bleStart() {
  if (!bleInited) {
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);   // Bluetooth klasik tidak dipakai: bebaskan memorinya untuk WiFi
    NimBLEDevice::init(BLE_NAME);
    NimBLEDevice::setPower(9);        // daya pancar BLE maksimum (+9 dBm)
    NimBLEDevice::setMTU(185);
    bleServer = NimBLEDevice::createServer();
    bleServer->setCallbacks(new EdgeServerCB());
    NimBLEService* svc = bleServer->createService(NUS_SERVICE);
    NimBLECharacteristic* rx = svc->createCharacteristic(
      NUS_RX, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
    rx->setCallbacks(new EdgeRxCB());
    bleTx = svc->createCharacteristic(NUS_TX, NIMBLE_PROPERTY::NOTIFY);   // deskriptor 0x2902 dibuat otomatis
    svc->start();
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(NUS_SERVICE);
    adv->enableScanResponse(true);
    adv->setMinInterval(0x50);          // iklan tiap 50 sampai 100 ms: HP cepat menemukan dan menyambung ulang
    adv->setMaxInterval(0xA0);
    bleInited = true;
    if (wifiActive) WiFi.setSleep(true);   // WiFi dan Bluetooth berbagi satu radio: WiFi wajib mode modem sleep
  }
  NimBLEDevice::startAdvertising();
  btActive = true;
  DBG("[BT] aktif, nama %s\n", BLE_NAME);
}

// BLE tidak di-deinit (deinit lalu init ulang sering crash), cukup putus dan berhenti beriklan
void bleStop() {
  if (bleInited) {
    NimBLEDevice::getAdvertising()->stop();
    if (bleServer && bleServer->getConnectedCount() > 0) bleServer->disconnect(bleConnHandle);
  }
  bleNeedAdv = false;
  btConnected = false;
  btActive = false;
  DBG("[BT] mati\n");
}

// ================= WIFI + WEB MONITOR =================
WebServer web(80);
DNSServer dnsServer;            // captive portal: semua nama domain diarahkan ke remot
bool webRoutesSet = false;

static const char WEB_PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="id"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 TX</title>
<style>
:root{--bg:#0b0f14;--card:#121922;--line:#1f2a36;--tx:#e6edf3;--mut:#7d8b99;--acc:#3fb6ff;--ok:#3ddc84;--warn:#ffb020;--bad:#ff4d5e}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--tx);font:14px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
header{display:flex;align-items:center;justify-content:space-between;padding:12px 16px;position:sticky;top:0;background:var(--bg);z-index:3}
h1{font-size:17px;margin:0;letter-spacing:.08em}
h1 b{color:var(--acc)}
.pill{font-size:12px;padding:3px 10px;border-radius:99px;border:1px solid var(--line);color:var(--mut)}
.pill.on{color:var(--ok);border-color:#235c3c}
.pill.off{color:var(--bad);border-color:#5c2330}
.tabs{display:flex;gap:6px;padding:0 16px 10px;position:sticky;top:48px;background:var(--bg);z-index:3;border-bottom:1px solid var(--line)}
.tabs button{flex:1;padding:9px 0;border:1px solid var(--line);background:var(--card);color:var(--mut);border-radius:10px;font:inherit;font-weight:600;cursor:pointer}
.tabs button.on{color:#06121c;background:var(--acc);border-color:var(--acc)}
main{padding:14px 16px;max-width:980px;margin:0 auto;display:grid;gap:12px}
main[hidden]{display:none}
.grid{display:grid;gap:12px;grid-template-columns:repeat(auto-fit,minmax(150px,1fr))}.grid.wide{grid-template-columns:repeat(auto-fit,minmax(260px,1fr))}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:12px 14px}
.lbl{font-size:11px;text-transform:uppercase;letter-spacing:.1em;color:var(--mut);margin-bottom:6px}
.big{font-size:24px;font-weight:650;font-variant-numeric:tabular-nums;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.sub{font-size:12px;color:var(--mut);margin-top:2px;min-height:16px}
.meter{height:6px;background:#0a0e13;border-radius:6px;overflow:hidden;margin-top:8px}
.meter i{display:block;height:100%;background:var(--ok);width:0;transition:width .15s}
.bars{display:flex;gap:4px;align-items:flex-end;height:26px}
.bars i{width:8px;background:#26313d;border-radius:2px}
.bars i.on{background:var(--acc)}
.ok{color:var(--ok)}.warn{color:var(--warn)}.bad{color:var(--bad)}
.sticks{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.pad{position:relative;aspect-ratio:1;max-width:220px;margin:6px auto 4px;border:1px solid var(--line);border-radius:10px;background:
linear-gradient(var(--line),var(--line)) center/1px 100% no-repeat,linear-gradient(var(--line),var(--line)) center/100% 1px no-repeat,#0a0e13}
.dot{position:absolute;width:16px;height:16px;margin:-8px 0 0 -8px;border-radius:50%;background:var(--acc);box-shadow:0 0 12px var(--acc);left:50%;top:50%}
.nums{display:flex;justify-content:space-between;font-size:12px;color:var(--mut);font-variant-numeric:tabular-nums}
.ch{display:grid;grid-template-columns:64px 1fr 76px;gap:10px;align-items:center;padding:5px 0;border-top:1px solid var(--line)}
.ch:first-of-type{border-top:0}
.ch b{font-size:13px;display:flex;flex-direction:column;gap:2px}.ch b span{display:flex;gap:3px;flex-wrap:wrap}
.track{position:relative;height:12px;background:#0a0e13;border-radius:6px;overflow:hidden}
.track:after{content:"";position:absolute;left:50%;top:0;bottom:0;width:1px;background:var(--line)}
.fill{position:absolute;top:0;bottom:0;background:var(--acc);border-radius:6px}
.val{text-align:right;font-variant-numeric:tabular-nums;font-size:13px}
.val small{color:var(--mut);display:block;font-size:11px}
.tag{font-size:10px;font-weight:500;padding:0 4px;border-radius:4px;background:#2a1f12;color:var(--warn)}
.kv{display:grid;grid-template-columns:auto 1fr;gap:4px 12px;font-size:13px}.kv span{min-width:0;overflow-wrap:anywhere}.kv span:nth-child(even){text-align:right;font-variant-numeric:tabular-nums}
.kv span:nth-child(odd){color:var(--mut)}
footer{color:var(--mut);font-size:11px;text-align:center;padding:4px 16px 24px}
/* ---- pengaturan ---- */
.row{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:10px 0;border-top:1px solid var(--line)}
.lbl+.row{border-top:0}
.row .nm{min-width:0}.row .nm small{display:block;color:var(--mut);font-size:12px}
.rowv{padding:10px 0;border-top:1px solid var(--line)}.lbl+.rowv{border-top:0}
.rowv .top{display:flex;justify-content:space-between;align-items:baseline;gap:12px;margin-bottom:4px}
.rowv output{font-variant-numeric:tabular-nums;color:var(--acc);font-weight:600}
input[type=range]{width:100%;accent-color:var(--acc);margin:6px 0;height:24px}
.sw{position:relative;display:inline-block;width:46px;height:26px;flex:none}
.sw input{position:absolute;inset:0;width:100%;height:100%;margin:0;opacity:0;cursor:pointer;z-index:1}
.sw i{position:absolute;inset:0;background:#26313d;border-radius:99px;transition:.15s}
.sw i:after{content:"";position:absolute;left:3px;top:3px;width:20px;height:20px;border-radius:50%;background:#9aa7b4;transition:.15s}
.sw input:checked+i{background:var(--acc)}
.sw input:checked+i:after{left:23px;background:#fff}
.seg{display:inline-flex;border:1px solid var(--line);border-radius:10px;overflow:hidden;flex:none}
.seg button{border:0;background:transparent;color:var(--mut);padding:7px 14px;font:inherit;font-weight:600;cursor:pointer}
.seg button.on{background:var(--acc);color:#06121c}
.mini{border:1px solid var(--line);background:#0a0e13;color:var(--tx);border-radius:8px;padding:5px 10px;font:inherit;font-size:12px;cursor:pointer}
.sel{background:#0a0e13;color:var(--tx);border:1px solid var(--line);border-radius:8px;padding:7px 10px;font:inherit;max-width:55%}
.mini:disabled{opacity:.4;cursor:default}
.btn{border:0;border-radius:10px;padding:10px 16px;font:inherit;font-weight:650;cursor:pointer;background:var(--acc);color:#06121c}
.btn.ghost{background:#26313d;color:var(--tx)}
.btn:disabled{opacity:.35;cursor:default}
.btns{display:flex;gap:8px;flex-wrap:wrap;margin-top:10px}
.chs{display:grid;gap:10px;grid-template-columns:repeat(auto-fit,minmax(260px,1fr))}
.chc{border:1px solid var(--line);border-radius:10px;padding:8px 12px;background:#0e141b}
.chh{display:flex;justify-content:space-between;align-items:center}
.chh span{display:flex;align-items:center;gap:8px;color:var(--mut);font-size:12px}
.trim{display:grid;grid-template-columns:1fr 46px 30px;gap:8px;align-items:center}
.trim output{text-align:right;font-variant-numeric:tabular-nums;font-weight:600}
.cbar{display:grid;grid-template-columns:34px 1fr 44px;gap:8px;align-items:center;font-size:12px;margin:6px 0;color:var(--mut)}
.ct{position:relative;height:10px;background:#0a0e13;border-radius:5px;overflow:hidden}
.ct .rg{position:absolute;top:3px;bottom:3px;background:#2d4a60}
.ct .pt{position:absolute;top:0;bottom:0;width:4px;margin-left:-2px;background:var(--acc)}
.ct.okr .rg{background:#1f5a3a}
.cbar span:last-child{text-align:right;font-variant-numeric:tabular-nums}
.note{font-size:12px;color:var(--mut);margin:6px 0 0}
.toast{position:fixed;left:50%;bottom:18px;transform:translateX(-50%);background:#2a1f12;color:var(--warn);border:1px solid #5c4313;border-radius:10px;padding:9px 14px;font-size:13px;max-width:90vw;z-index:9;opacity:0;pointer-events:none;transition:opacity .2s}
.toast.show{opacity:1}
.sheet{position:fixed;inset:0;background:#000a;display:flex;align-items:flex-end;justify-content:center;z-index:10}
.sheet[hidden]{display:none}
.sheet .box{background:var(--card);border:1px solid var(--line);border-radius:14px 14px 0 0;padding:16px;width:100%;max-width:520px}
.sheet p{margin:0 0 12px}
.sheet .acts{display:flex;gap:8px;justify-content:flex-end}
.btn.danger{background:var(--bad);color:#fff}
</style></head><body>
<header><h1><b>ESP32</b> TX</h1><span id="st" class="pill">menghubungkan…</span></header>
<nav class="tabs"><button id="tMon" class="on">Monitor</button><button id="tSet">Pengaturan</button></nav>

<main id="mon">
<div class="grid">
 <div class="card"><div class="lbl">Baterai</div><div class="big" id="v">-</div><div class="sub" id="vs"></div><div class="meter"><i id="vm"></i></div></div>
 <div class="card"><div class="lbl">Link NRF24</div><div class="bars" id="lk"><i style="height:20%"></i><i style="height:40%"></i><i style="height:60%"></i><i style="height:80%"></i><i style="height:100%"></i></div><div class="sub" id="lks"></div></div>
 <div class="card"><div class="lbl">Input</div><div class="big" id="src">-</div><div class="sub" id="srcs"></div></div>
 <div class="card"><div class="lbl">WiFi</div><div class="big" id="rs">-</div><div class="sub" id="ws"></div></div>
 <div class="card"><div class="lbl">Bluetooth</div><div class="big" id="bt">-</div><div class="sub" id="bts"></div></div>
 <div class="card"><div class="lbl">Model</div><div class="big" id="mdlN">-</div><div class="sub" id="mdlS"></div></div>
 <div class="card"><div class="lbl">Baterai RX</div><div class="big" id="rxv">-</div><div class="sub" id="rxs"></div></div>
 <div class="card"><div class="lbl">Timer</div><div class="big" id="tmv">-</div><div class="sub" id="tms"></div></div>
</div>
<div class="sticks">
 <div class="card"><div class="lbl">Stick kiri</div><div class="pad"><div class="dot" id="d1"></div></div><div class="nums"><span id="n1x"></span><span id="n1y"></span></div></div>
 <div class="card"><div class="lbl">Stick kanan</div><div class="pad"><div class="dot" id="d2"></div></div><div class="nums"><span id="n2x"></span><span id="n2y"></span></div></div>
</div>
<div class="card"><div class="lbl">Output channel (dikirim ke RX)</div><div id="chs"></div></div>
<div class="grid wide">
 <div class="card"><div class="lbl">ADC stick</div><div class="kv" id="adc"></div></div>
 <div class="card"><div class="lbl">Sistem</div><div class="kv" id="sys"></div></div>
</div>
</main>

<main id="set" hidden>
 <div class="card"><div class="lbl">Model</div>
  <div class="row"><div class="nm">Model aktif<small>Pengaturan di bawah ini milik model yang dipilih</small></div><div class="seg" id="mdlSeg"></div></div>
 </div>
 <div class="card"><div class="lbl">Brightness</div>
  <div class="rowv"><div class="top"><span>Kecerahan layar OLED</span><output id="briV"></output></div><input type="range" id="bri" min="0" max="10" step="1"></div>
 </div>
 <div class="card"><div class="lbl">Theme</div>
  <div class="row"><div class="nm">Tema layar OLED</div><div class="seg" id="thm"><button data-v="0">LIGHT</button><button data-v="1">DARK</button></div></div>
 </div>
 <div class="card"><div class="lbl">Channel reverse dan trim</div><div class="chs" id="chset"></div>
  <p class="note">Trim -100 sampai +100. Reverse dan trim berlaku untuk CH1 sampai CH6, baik dari joystick remot maupun dari HP.</p>
 </div>
 <div class="card"><div class="lbl">Rate dan expo</div><div class="chs" id="drBox"></div>
  <p class="note">Rate mengecilkan gerak penuh stik. Expo melembutkan gerak di sekitar tengah dan ujung tetap penuh. Berlaku untuk CH1 sampai CH4. Nilai bawaan: rate 100, expo 0.</p>
 </div>
 <div class="card"><div class="lbl">Endpoint</div><div class="chs" id="epBox"></div>
  <p class="note">Batas gerak bawah dan atas tiap channel (20 sampai 120 persen). Nilai bawaan 100.</p>
 </div>
 <div class="card"><div class="lbl">Mixing</div>
  <div class="row"><div class="nm">Tipe<small>ELEVON: CH1 dan CH2. V-TAIL: CH2 dan CH4</small></div><div class="seg" id="mixSeg"><button data-v="0">OFF</button><button data-v="1">ELEVON</button><button data-v="2">V-TAIL</button></div></div>
  <div class="row"><div class="nm" id="mxaL">Channel A</div><select class="sel" id="mxa"></select></div>
  <div class="row"><div class="nm" id="mxbL">Channel B</div><select class="sel" id="mxb"></select></div>
  <div id="mixBox"></div>
  <p class="note" id="mixNote"></p>
 </div>
 <div class="card"><div class="lbl">Failsafe mode HP</div><div id="fsBox"></div>
  <p class="note">Nilai tiap channel yang dikirim ke RX kalau data dari HP terputus. 50 berarti tengah. Throttle (CH3) bawaan 0. CH5 dan CH6 hanya 0 atau 4095, jadi nilai di atas 50 berarti ON.</p>
 </div>
 <div class="card"><div class="lbl">Timer</div>
  <div class="rowv"><div class="top"><span>Waktu</span><output id="tmShow" style="font-size:20px"></output></div></div>
  <div id="tmBox"></div>
  <div class="row"><div class="nm">Pemicu<small>Channel yang menjalankan timer. Manual: pakai tombol di bawah</small></div><select class="sel" id="ttrig"></select></div>
  <div id="tthrBox"></div>
  <div class="row"><div class="nm">Jalan bila</div><div class="seg" id="tdirSeg"><button data-v="0">DI ATAS</button><button data-v="1">DI BAWAH</button></div></div>
  <div class="row"><div class="nm">Pemicu reset<small>Boleh sama dengan channel pemicu jalan. Memakai Batas yang sama di atas</small></div><select class="sel" id="trtrig"></select></div>
  <div class="row"><div class="nm">Reset bila</div><div class="seg" id="trdirSeg"><button data-v="0">DI ATAS</button><button data-v="1">DI BAWAH</button></div></div>
  <div class="btns"><button class="btn" id="tRun">Mulai</button><button class="btn ghost" id="tReset">Reset</button></div>
 </div>
 <div class="card"><div class="lbl">Alarm baterai</div><div id="blBox"></div>
  <p class="note">Alarm berbunyi saat tegangan baterai remot di bawah batas ini. Baterai 2S penuh 8,4 V.</p>
 </div>
 <div class="card"><div class="lbl">Cadangan pengaturan</div>
  <div class="btns"><a class="btn ghost" href="/backup" download="edgetx-backup.txt" style="text-decoration:none">Unduh cadangan</a><label class="btn ghost" style="margin:0">Pulihkan dari file<input type="file" id="rfile" accept=".txt,text/plain" hidden></label></div>
  <p class="note">Cadangan berisi semua pengaturan, semua model, dan kalibrasi. Setelah dipulihkan remot restart sendiri.</p>
 </div>
 <div class="card"><div class="lbl">Kalibrasi joystick</div>
  <div id="calTx" class="note" style="margin:0 0 6px"></div>
  <div id="calBars"></div>
  <div class="btns"><button class="btn" id="calGo">Mulai</button><button class="btn ghost" id="calCancel">Batal</button></div>
 </div>
 <div class="card"><div class="lbl">Konektivitas</div>
  <div class="row"><div class="nm">WiFi<small>Hotspot ESP32 TX untuk halaman ini</small></div><label class="sw"><input type="checkbox" id="wf"><i></i></label></div>
  <div class="row"><div class="nm">Bluetooth<small>Untuk aplikasi HP</small></div><label class="sw"><input type="checkbox" id="btsw"><i></i></label></div>
  <div class="row"><div class="nm">Input<small>HP: joystick remot diabaikan</small></div><div class="seg" id="src2"><button data-v="0">REMOT</button><button data-v="1">HP</button></div></div>
 </div>
 <div class="card"><div class="lbl">Suara</div>
  <div class="rowv"><div class="top"><span>Volume</span><output id="svolV"></output></div><input type="range" id="svol" min="1" max="10" step="1"></div>
  <div id="snds"></div>
  <p class="note">Alarm RX putus, baterai habis, dan timer ikut volume ini dan bisa dimatikan satu per satu.</p>
 </div>
</main>
<footer>Perubahan langsung dipakai dan disimpan di remot.</footer>
<div id="toast" class="toast"></div>
<div id="sheet" class="sheet" hidden><div class="box"><p id="sheetTx"></p><div class="acts"><button class="btn ghost" id="sheetNo">Batal</button><button class="btn danger" id="sheetYes">Lanjut</button></div></div></div>

<script>
const $=id=>document.getElementById(id);
let last=null,tab="mon";
const NM=5;
const lockT={};
 function lock(k,ms){lockT[k]=Date.now()+(ms||1500)}
 function locked(k){return (lockT[k]||0)>Date.now()}
 function toast(t){const e=$("toast");e.textContent=t;e.classList.add("show");clearTimeout(toast.t);toast.t=setTimeout(()=>e.classList.remove("show"),2600)}
 async function send(k,v){
 lock(k);
 try{
  const r=await fetch("/set",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:"k="+encodeURIComponent(k)+"&v="+encodeURIComponent(v)});
  if(!r.ok){lockT[k]=0;toast((await r.text())||"Ditolak")}
 }catch(e){toast("Gagal mengirim")}
 tick();
}
 function ask(text,yes,fn){
 $("sheetTx").textContent=text;$("sheetYes").textContent=yes;$("sheet").hidden=false;
 $("sheetYes").onclick=()=>{$("sheet").hidden=true;fn()};
 $("sheetNo").onclick=()=>{$("sheet").hidden=true;if(last)drawSet(last,true)};
}
 function showTab(t){tab=t;$("mon").hidden=t!=="mon";$("set").hidden=t!=="set";$("tMon").className=t==="mon"?"on":"";$("tSet").className=t==="set"?"on":"";if(last)(t==="mon"?draw:drawSet)(last,true);schedule()}
$("tMon").onclick=()=>showTab("mon");$("tSet").onclick=()=>showTab("set");

/* ---------- monitor ---------- */
(function(){let h="";for(let i=0;i<8;i++)h+='<div class="ch"><b>CH'+(i+1)+'<span id="tg'+i+'"></span></b><div class="track"><div class="fill" id="f'+i+'"></div></div><div class="val" id="cv'+i+'"></div></div>';$("chs").innerHTML=h;})();
 function tShown(j){return j.tmin>0?Math.max(0,j.tmin*60-j.tel):j.tel}
 function fmtT(s){const m=Math.floor(s/60),x=s%60;return String(m).padStart(2,"0")+":"+String(x).padStart(2,"0")}
 function fmtUp(s){const h=Math.floor(s/3600),m=Math.floor(s%3600/60),x=s%60;return (h?h+"j ":"")+m+"m "+x+"d"}
 function pos(d,x,y){d.style.left=(x/4095*100)+"%";d.style.top=(100-y/4095*100)+"%"}
 function draw(j){
 $("v").textContent=j.v.toFixed(2)+" V";
 $("vm").style.width=j.vp+"%";
 $("vm").style.background=j.lb?"var(--bad)":(j.vp<30?"var(--warn)":"var(--ok)");
 $("vs").innerHTML=j.vp+"%"+(j.lb?' · <span class="bad">LOW</span>':"")+(j.vf?' · <span class="warn">beku saat WiFi aktif</span>':"");
 [...$("lk").children].forEach((b,i)=>b.className=i<j.link?"on":"");
 $("lks").innerHTML=j.lost?'<span class="bad">RX HILANG</span>':'ACK '+j.ackr+'% · '+j.txr+' paket/s';
 $("src").textContent=j.src?"HP":"Remot";
 $("src").className="big"+(j.src?(j.hpok?" ok":" bad"):"");
 $("srcs").innerHTML=j.src?(j.hpok?"Data HP diterima":'<span class="bad">FAILSAFE, data HP terputus</span>'):"Joystick remot aktif";
 $("rs").textContent=j.rssi?j.rssi+" dBm":"-";
 $("ws").textContent=j.wc+" perangkat terhubung";
 $("bt").textContent=!j.bton?"Mati":(j.btc?"Terhubung":"Menunggu");
 $("bt").className="big"+(j.btc?" ok":"");
 $("bts").textContent=j.bton?("Paket diterima: "+j.btrx):"Bluetooth mati";
 $("mdlN").textContent=(j.mn&&j.mn[j.mdl])?j.mn[j.mdl]:"Model "+(j.mdl+1);
 $("mdlS").textContent=(j.mxt?(j.mxt==1?"Mixing ELEVON":"Mixing V-TAIL"):"Tanpa mixing");
 $("rxv").textContent=j.rxv>0?j.rxv.toFixed(2)+" V":"-";
 $("rxs").textContent=j.rxv>0?"dari telemetri RX":"belum ada telemetri";
 $("tmv").textContent=fmtT(tShown(j));
 $("tms").textContent=(j.tmin>0?"hitung mundur "+j.tmin+" mnt":"stopwatch")+(j.ttrig>0?", pemicu CH"+j.ttrig:(j.trun?", jalan":""));
 pos($("d1"),j.ch[3],j.ch[2]);pos($("d2"),j.ch[0],j.ch[1]);
 $("n1x").textContent="CH4 "+j.ch[3];$("n1y").textContent="CH3 "+j.ch[2];
 $("n2x").textContent="CH1 "+j.ch[0];$("n2y").textContent="CH2 "+j.ch[1];
 for(let i=0;i<8;i++){
  const v=j.ch[i],p=v/4095*100,f=$("f"+i);
  if(p>=50){f.style.left="50%";f.style.width=(p-50)+"%"}else{f.style.left=p+"%";f.style.width=(50-p)+"%"}
  $("cv"+i).innerHTML=v+"<small>"+Math.round(1000+v*1000/4095)+" µs</small>";
  let t="";if(i<6){if(j.rev[i])t+='<span class="tag">REV</span>';if(j.trim[i])t+='<span class="tag">'+(j.trim[i]>0?"+":"")+j.trim[i]+'</span>'}
  $("tg"+i).innerHTML=t;
 }
 const n=["J1X","J1Y","J2X","J2Y"];let a="";for(let i=0;i<4;i++)a+="<span>"+n[i]+"</span><span>"+j.raw[i]+" → "+j.cal[i]+"</span>";
 a+="<span>SW1 / SW2</span><span>"+j.sw[0]+" / "+j.sw[1]+"</span>";$("adc").innerHTML=a;
 $("sys").innerHTML="<span>Menu OLED</span><span>"+j.mode+"</span><span>Tema</span><span>"+(j.thm?"DARK":"LIGHT")+"</span><span>Brightness</span><span>"+j.bri+"/10</span><span>Uptime</span><span>"+fmtUp(j.up)+"</span><span>RAM bebas</span><span>"+Math.round(j.heap/1024)+" KB</span>";
}

/* ---------- pengaturan ---------- */
const SND=["Disconnect","Booting","Pindah menu","Scroll","Klik","Terhubung ke HP (Bluetooth)","HP masuk ke WiFi","Alarm RX putus","Alarm baterai habis","Timer selesai"];
(function(){
 let h="";
 for(let i=0;i<6;i++)h+='<div class="chc"><div class="chh"><b>CH'+(i+1)+'</b><span>Reverse <label class="sw"><input type="checkbox" id="rv'+i+'"><i></i></label></span></div><div class="trim"><input type="range" id="tr'+i+'" min="-100" max="100" step="1"><output id="trv'+i+'"></output><button class="mini" id="trz'+i+'">0</button></div></div>';
 $("chset").innerHTML=h;
 h="";
 for(let i=0;i<SND.length;i++)h+='<div class="row"><div class="nm">'+SND[i]+'</div><div style="display:flex;align-items:center;gap:10px"><button class="mini" id="sT'+i+'">Tes</button><label class="sw"><input type="checkbox" id="sn'+i+'"><i></i></label></div></div>';
 $("snds").innerHTML=h;
 h="";
 for(let i=0;i<4;i++)h+='<div class="cbar"><span>'+["J1X","J1Y","J2X","J2Y"][i]+'</span><div class="ct" id="cb'+i+'"><div class="rg"></div><div class="pt"></div></div><span id="cr'+i+'"></span></div>';
 $("calBars").innerHTML=h;
})();
const SL=[];
 function srow(id,label,min,max,step){return '<div class="rowv"><div class="top"><span>'+label+'</span><output id="'+id+'V"></output></div><input type="range" id="'+id+'" min="'+min+'" max="'+max+'" step="'+step+'"></div>'}
 function addSl(box,id,key,label,min,max,step,fmt,get){
 box.insertAdjacentHTML("beforeend",srow(id,label,min,max,step));
 slider(id,key,v=>$(id+"V").textContent=fmt(v));
 SL.push({id,key,fmt,get});
}
const pc=v=>v+"%";
(function(){
 window.mdlKey="";
 const dr=$("drBox"),ep=$("epBox");
 for(let i=0;i<4;i++){const b=document.createElement("div");b.className="chc";b.innerHTML='<div class="chh"><b>CH'+(i+1)+'</b></div>';dr.appendChild(b);
  addSl(b,"dr"+i,"dr"+i,"Rate",30,100,5,pc,j=>j.dr[i]);addSl(b,"ex"+i,"ex"+i,"Expo",0,100,5,pc,j=>j.ex[i]);}
 for(let i=0;i<6;i++){const b=document.createElement("div");b.className="chc";b.innerHTML='<div class="chh"><b>CH'+(i+1)+'</b></div>';ep.appendChild(b);
  addSl(b,"lo"+i,"lo"+i,"Bawah",20,120,5,pc,j=>j.lo[i]);addSl(b,"hi"+i,"hi"+i,"Atas",20,120,5,pc,j=>j.hi[i]);}
 seg("mixSeg","mixt");
 addSl($("mixBox"),"mixw","mixw","Bobot",10,100,5,pc,j=>j.mxw);
 const opt=(el,from,to,pre,first)=>{let h=first?'<option value="0">'+first+'</option>':"";for(let i=from;i<=to;i++)h+='<option value="'+i+'">'+pre+i+'</option>';el.innerHTML=h};
 opt($("mxa"),1,6,"CH");opt($("mxb"),1,6,"CH");opt($("ttrig"),1,8,"CH","Manual");opt($("trtrig"),1,8,"CH","Manual");
 [["mxa","mixa"],["mxb","mixb"],["ttrig","ttrig"],["trtrig","trtrig"]].forEach(([id,key])=>$(id).addEventListener("change",e=>send(key,e.target.value)));
 addSl($("tthrBox"),"tthr","tthr","Batas pemicu",5,95,5,pc,j=>j.tthr);
 seg("tdirSeg","tdir");seg("trdirSeg","trdir");
 for(let i=0;i<8;i++)addSl($("fsBox"),"fs"+i,"fs"+i,"CH"+(i+1),0,100,5,pc,j=>j.fs[i]);
 addSl($("tmBox"),"tmin","tmin","Hitung mundur",0,99,1,v=>v==0?"Mati (stopwatch)":v+" menit",j=>j.tmin);
 addSl($("blBox"),"blow","blow","Batas alarm",50,84,1,v=>(v/10).toFixed(1)+" V",j=>j.blow);
 $("tRun").onclick=()=>send("trun",last&&last.trun?0:1);
 $("tReset").onclick=()=>send("treset",1);
 $("rfile").onchange=e=>{
  const f=e.target.files[0];if(!f)return;const r=new FileReader();
  r.onload=()=>ask("Semua pengaturan sekarang diganti dengan isi file, lalu remot restart.","Pulihkan",async()=>{
   try{const x=await fetch("/restore",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:"d="+encodeURIComponent(String(r.result).trim())});
    toast(x.ok?"Berhasil, remot restart. Sambungkan lagi sebentar lagi.":(await x.text())||"Ditolak")}catch(err){toast("Gagal mengirim")}
  });
  r.readAsText(f);e.target.value="";
 };
})();
 function slider(id,key,fmt){
 const el=$(id);let t=0;
 el.addEventListener("input",()=>{lock(key,2000);fmt(el.value);const n=Date.now();if(n-t>120){t=n;send(key,el.value)}});
 el.addEventListener("change",()=>send(key,el.value));
}
 function sw(id,key,fn){$(id).addEventListener("change",e=>{const v=e.target.checked?1:0;if(fn)fn(v);else send(key,v)})}
 function seg(id,key,fn){[...$(id).children].forEach(b=>b.onclick=()=>{const v=+b.dataset.v;if(fn)fn(v);else send(key,v)})}
slider("bri","bri",v=>$("briV").textContent=v+" / 10");
slider("svol","svol",v=>$("svolV").textContent=v+" / 10");
seg("thm","thm");
for(let i=0;i<6;i++){
 sw("rv"+i,"rev"+i);
 slider("tr"+i,"trim"+i,v=>$("trv"+i).textContent=(v>0?"+":"")+v);
 $("trz"+i).onclick=()=>{$("tr"+i).value=0;$("trv"+i).textContent="0";send("trim"+i,0)};
}
for(let i=0;i<SND.length;i++){sw("sn"+i,"snd"+i);$("sT"+i).onclick=()=>send("test",i)}
sw("wf","wifi",v=>{
 if(v)return send("wifi",1);
 ask("Mematikan WiFi akan memutus halaman ini. Nyalakan lagi lewat menu KONEKTIVITAS di remot.","Matikan WiFi",()=>send("wifi",0));
});
sw("btsw","bt",v=>{
 if(v||!last||!last.src)return send("bt",v);
 ask("Input sedang HP. Mematikan Bluetooth mengembalikan input ke joystick remot.","Matikan Bluetooth",()=>send("bt",0));
});
seg("src2","src",v=>{
 if(!v)return send("src",0);
 ask("Joystick remot akan diabaikan. Remot hanya mengikuti data dari aplikasi HP, dan masuk FAILSAFE kalau data HP putus.","Pakai HP",()=>send("src",1));
});
let calWas=false,calPressedSave=false,calReady=false,calStep=0;
$("calGo").onclick=()=>{
 if(!last)return;
 if(!last.cb.on){calPressedSave=false;send("cal",1)}
 else{if(last.cb.st===1)calPressedSave=true;send("cal",2)}
};
$("calCancel").onclick=()=>{calPressedSave=false;send("cal",3)};
 function setv(id,key,val){if(locked(key))return;const e=$(id);if(e.type==="checkbox")e.checked=!!val;else e.value=val}
 function drawSet(j,force){
 setv("bri","bri",j.bri);if(!locked("bri"))$("briV").textContent=j.bri+" / 10";
 setv("svol","svol",j.svol);if(!locked("svol"))$("svolV").textContent=j.svol+" / 10";
 [...$("thm").children].forEach(b=>b.className=(+b.dataset.v===j.thm)?"on":"");
 for(let i=0;i<6;i++){
  setv("rv"+i,"rev"+i,j.rev[i]);
  setv("tr"+i,"trim"+i,j.trim[i]);
  if(!locked("trim"+i))$("trv"+i).textContent=(j.trim[i]>0?"+":"")+j.trim[i];
 }
 for(let i=0;i<SND.length;i++)setv("sn"+i,"snd"+i,(j.smask>>i)&1);
 setv("wf","wifi",j.wf);setv("btsw","bt",j.bts);
 [...$("src2").children].forEach(b=>b.className=(+b.dataset.v===j.src)?"on":"");
 {const nm=j.mn||[],k=nm.join("|");if(k!==window.mdlKey){window.mdlKey=k;let h="";nm.forEach((n,i)=>h+='<button data-v="'+i+'">'+n+'</button>');$("mdlSeg").innerHTML=h;seg("mdlSeg","model");}}
 [...$("mdlSeg").children].forEach(b=>b.className=(+b.dataset.v===j.mdl)?"on":"");
 SL.forEach(o=>{const v=o.get(j);setv(o.id,o.key,v);if(!locked(o.key))$(o.id+"V").textContent=o.fmt(v)});
 [...$("mixSeg").children].forEach(b=>b.className=(+b.dataset.v===j.mxt)?"on":"");
 [...$("tdirSeg").children].forEach(b=>b.className=(+b.dataset.v===j.tdir)?"on":"");[...$("trdirSeg").children].forEach(b=>b.className=(+b.dataset.v===j.trdir)?"on":"");
 const selSet=(id,key,v)=>{const e=$(id);if(!locked(key)&&document.activeElement!==e)e.value=v};
 selSet("mxa","mixa",j.mxa);selSet("mxb","mixb",j.mxb);selSet("ttrig","ttrig",j.ttrig);selSet("trtrig","trtrig",j.trtrig);
 const rl=j.mxt==1?["Channel aileron","Channel elevator"]:(j.mxt==2?["Channel elevator","Channel rudder"]:["Channel A","Channel B"]);
 $("mxaL").textContent=rl[0];$("mxbL").textContent=rl[1];
 $("mixNote").textContent=j.mxt==1?"ELEVON: A = elevator + aileron, B = elevator - aileron. Mengganti tipe mengembalikan pasangan channel ke bawaannya (ELEVON CH1 dan CH2, V-TAIL CH2 dan CH4)."
  :j.mxt==2?"V-TAIL: A = elevator + rudder, B = elevator - rudder. Mengganti tipe mengembalikan pasangan channel ke bawaannya (ELEVON CH1 dan CH2, V-TAIL CH2 dan CH4)."
  :"Pilih tipe ELEVON atau V-TAIL untuk mulai. Balik arah dengan Reverse.";
 $("tmShow").textContent=fmtT(tShown(j));
 $("tRun").textContent=j.trun?"Henti":"Mulai";$("tRun").disabled=j.ttrig>0;
 // kalibrasi
 const c=j.cb;
 let allSweep=true,allMid=true;
 for(let i=0;i<4;i++){
  const mn=c.mn[i],mx=c.mx[i],r=j.raw[i];
  const ok=(mx-mn)>=1000;if(!ok)allSweep=false;
  if(!(r>mn+100&&r<mx-100))allMid=false;
  const box=$("cb"+i),rg=box.children[0],pt=box.children[1];
  if(c.on&&mx>=mn){rg.style.left=(mn/4095*100)+"%";rg.style.width=((mx-mn)/4095*100)+"%"}else{rg.style.left="0";rg.style.width="0"}
  pt.style.left=(r/4095*100)+"%";
  box.className="ct"+(c.on&&c.st===0&&ok?" okr":"");
  $("cr"+i).textContent=r;
 }
 if(!c.on){
  $("calTx").textContent=calWas&&calPressedSave?"Kalibrasi tersimpan.":"Tekan Mulai. Layar remot ikut masuk ke menu kalibrasi.";
  $("calGo").textContent="Mulai";$("calGo").disabled=false;$("calCancel").disabled=true;
 }else if(c.st===0){
  $("calTx").textContent="Langkah 1 dari 2: gerakkan semua stik penuh ke semua sudut sampai keempat bar hijau.";
  $("calGo").textContent="Lanjut";$("calGo").disabled=!allSweep;$("calCancel").disabled=false;
 }else{
  $("calTx").textContent="Langkah 2 dari 2: lepaskan semua stik ke tengah, lalu tekan Simpan.";
  $("calGo").textContent="Simpan";$("calGo").disabled=!allMid;$("calCancel").disabled=false;
 }
 if(calWas&&!c.on&&calPressedSave){toast("Kalibrasi tersimpan");}
 calWas=!!c.on;
}

/* ---------- polling ---------- */
let busy=false,fail=0,timer=null;
 async function tick(){
 if(busy||document.hidden)return;busy=true;
 const c=new AbortController();const t=setTimeout(()=>c.abort(),800);
 try{
  const r=await fetch("/data",{signal:c.signal,cache:"no-store"});last=await r.json();
  (tab==="mon"?draw:drawSet)(last);
  fail=0;$("st").textContent="online";$("st").className="pill on";
 }catch(e){if(++fail>2){$("st").textContent="terputus";$("st").className="pill off"}}
 clearTimeout(t);busy=false;
}
 function schedule(){clearInterval(timer);timer=setInterval(tick,tab==="mon"?300:600)}
schedule();tick();
</script></body></html>
)rawliteral";

const char* modeName(uint8_t m) {
  switch (m) {
    case MODE_DASHBOARD:  return "DASHBOARD";
    case MODE_BRIGHTNESS: return "BRIGHTNESS";
    case MODE_CHANNEL:    return "CH READOUT";
    case MODE_REVERSE:    return "REVERSE";
    case MODE_TRIM:       return "TRIM";
    case MODE_THEME:      return "THEME";
    case MODE_CALIB:      return "KALIBRASI";
    case MODE_LIST:       return "DAFTAR MENU";
    case MODE_CONN:       return "KONEKTIVITAS";
    case MODE_SOUND:      return "SUARA";
    case MODE_PARAM:      return PARAM_TITLES[paramPage < PG_COUNT ? paramPage : 0];
  }
  return "?";
}

// Captive portal: HP mengecek internet ke alamat seperti connectivitycheck.gstatic.com.
// DNS kita arahkan ke remot, lalu permintaan yang bukan ke 192.168.4.1 dialihkan (302) ke halaman monitor.
// Android lalu menampilkan "Masuk ke jaringan" dan membuka halaman otomatis, tanpa ketik IP.
static bool hostIsUs() {
  String h = web.hostHeader();
  if (h.length() == 0) return true;
  String ip = WiFi.softAPIP().toString();
  return h == ip || h.startsWith(ip + ":");
}

static void webRedirect() {
  web.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  web.sendHeader("Cache-Control", "no-store");
  web.send(302, "text/plain", "");
}

void webFwPage();
void webRoot() {
  if (!hostIsUs()) { webRedirect(); return; }
  if (otaMode) { webFwPage(); return; }          // mode update: halaman unggah firmware terbuka otomatis
  web.sendHeader("Cache-Control", "no-store");
  web.send_P(200, "text/html", WEB_PAGE);
}

// Pengecekan internet dari HP dijawab dengan pengalihan ke halaman remot (captive portal),
// sehingga HP otomatis membuka halaman web begitu terhubung ke WiFi ini
void webProbe() { webRedirect(); }

void webNotFound() {
  if (!hostIsUs()) webRedirect();
  else web.send(404, "text/plain", "Not found");
}

// Isi JSON status lengkap, dipakai halaman web (/data) dan aplikasi lewat Bluetooth. Mengembalikan panjang, atau -1 kalau tidak muat.
int buildDataJson(char* buf, int cap) {
  // Daftar nama model sebagai isi larik JSON: "PLANE","BOAT",...  (nama sudah dibersihkan, tanpa tanda kutip)
  char mnb[MAX_MODELS * 14 + 4];
  {
    int mp = 0;
    mnb[0] = 0;
    int mc = modelCount;
    if (mc < 1 || mc > MAX_MODELS) mc = 1;
    for (int i = 0; i < mc; i++) {
      char nm[12];
      memcpy(nm, modelName[i], 11);
      nm[11] = 0;
      mp += snprintf(mnb + mp, sizeof(mnb) - mp, "%s\"%s\"", i ? "," : "", nm);
    }
  }
  uint16_t ch[8];
  uint8_t sw = 0;
  if (xSemaphoreTake(pktMutex, (TickType_t)5) == pdTRUE) {
    for (int i = 0; i < 6; i++) ch[i] = pkt.ch[i];
    ch[6] = pkt.ch7; ch[7] = pkt.ch8;
    sw = pkt.switches;
    xSemaphoreGive(pktMutex);
  } else {
    for (int i = 0; i < 8; i++) ch[i] = 0;
  }

  int rssi = 0;
  if (wifiActive) {
    wifi_sta_list_t sl;
    if (esp_wifi_ap_get_sta_list(&sl) == ESP_OK && sl.num > 0) rssi = sl.sta[0].rssi;
  }

  float v = batteryVoltage;
  int vp = (int)((v - 6.40f) / (8.40f - 6.40f) * 100.0f);
  vp = constrain(vp, 0, 100);

  // Tegangan baterai RX dari telemetri, -1 kalau belum ada atau sudah basi
  float rxv = -1.0f;
  {
    uint32_t tm = telemMs;
    if (tm != 0 && (uint32_t)(millis() - tm) < 2000 && telemetry.voltage > 1.0f && telemetry.voltage < 30.0f) rxv = telemetry.voltage;
  }

  int n = snprintf(buf, cap,
    "{\"up\":%lu,\"v\":%.2f,\"vp\":%d,\"vf\":%d,\"lb\":%d,"
    "\"link\":%d,\"lost\":%d,\"txr\":%u,\"ackr\":%u,"
    "\"src\":%d,\"bton\":%d,\"btc\":%d,\"hpok\":%d,\"btrx\":%lu,"
    "\"wc\":%d,\"rssi\":%d,\"heap\":%lu,\"mode\":\"%s\","
    "\"raw\":[%u,%u,%u,%u],\"cal\":[%u,%u,%u,%u],\"sw\":[%d,%d],"
    "\"ch\":[%u,%u,%u,%u,%u,%u,%u,%u],"
    "\"rev\":[%d,%d,%d,%d,%d,%d],\"trim\":[%d,%d,%d,%d,%d,%d],"
    "\"thm\":%d,\"bri\":%d,\"wf\":%d,\"bts\":%d,\"svol\":%d,\"smask\":%d,"
    "\"cb\":{\"on\":%d,\"st\":%d,\"mn\":[%u,%u,%u,%u],\"mx\":[%u,%u,%u,%u]},"
    "\"mdl\":%d,\"mc\":%d,\"mn\":[%s],\"dr\":[%d,%d,%d,%d],\"ex\":[%d,%d,%d,%d],"
    "\"lo\":[%d,%d,%d,%d,%d,%d],\"hi\":[%d,%d,%d,%d,%d,%d],"
    "\"fs\":[%d,%d,%d,%d,%d,%d,%d,%d],"
    "\"mxt\":%d,\"mxw\":%d,\"mxa\":%d,\"mxb\":%d,\"tmin\":%d,\"ttrig\":%d,\"tthr\":%d,\"tdir\":%d,\"trtrig\":%d,\"trdir\":%d,\"trun\":%d,\"tel\":%lu,\"blow\":%d,\"rxv\":%.2f,\"fms\":[%d,%d,%d,%d],\"fmd\":[%d,%d,%d,%d],\"fmw\":[%d,%d,%d,%d],\"nrf\":%d,\"btype\":%d,\"btone\":%d,\"dsh\":%d,\"fw\":\"%s\",\"ota\":%d}",
    (unsigned long)(millis() / 1000), v, vp, battFrozen ? 1 : 0, lowBattAlarm ? 1 : 0,
    linkBars, linkLost ? 1 : 0, (unsigned)txRate, (unsigned)ackRate,
    inputSrc, btActive ? 1 : 0, btConnected ? 1 : 0, hpDataOk ? 1 : 0, (unsigned long)btRxCount,
    WiFi.softAPgetStationNum(), rssi, (unsigned long)ESP.getFreeHeap(), modeName(currentMode),
    rawStick[0], rawStick[1], rawStick[2], rawStick[3],
    stickCal[0], stickCal[1], stickCal[2], stickCal[3],
    (sw & 1) ? 1 : 0, (sw & 2) ? 1 : 0,
    ch[0], ch[1], ch[2], ch[3], ch[4], ch[5], ch[6], ch[7],
    channelReverse[0], channelReverse[1], channelReverse[2], channelReverse[3], channelReverse[4], channelReverse[5],
    channelTrim[0], channelTrim[1], channelTrim[2], channelTrim[3], channelTrim[4], channelTrim[5],
    darkMode ? 1 : 0, brightnessLevel, wifiOn ? 1 : 0, btOn ? 1 : 0, (int)soundVol, (int)soundMask,
    currentMode == MODE_CALIB ? 1 : 0, (int)calibStep,
    (unsigned)calTmpMin[0], (unsigned)calTmpMin[1], (unsigned)calTmpMin[2], (unsigned)calTmpMin[3],
    (unsigned)calTmpMax[0], (unsigned)calTmpMax[1], (unsigned)calTmpMax[2], (unsigned)calTmpMax[3],
    (int)activeModel, (int)modelCount, mnb, dualRate[0], dualRate[1], dualRate[2], dualRate[3], expoAmt[0], expoAmt[1], expoAmt[2], expoAmt[3],
    epLo[0], epLo[1], epLo[2], epLo[3], epLo[4], epLo[5], epHi[0], epHi[1], epHi[2], epHi[3], epHi[4], epHi[5],
    failsafePct[0], failsafePct[1], failsafePct[2], failsafePct[3], failsafePct[4], failsafePct[5], failsafePct[6], failsafePct[7],
    (int)mixType, (int)mixWeight, (int)mixA, (int)mixB, (int)timerCountdownMin, (int)timerTrig, (int)timerThr, (int)timerDir, (int)timerRstTrig, (int)timerRstDir, timerRun ? 1 : 0, (unsigned long)(timerElapsedMs / 1000), (int)battLowTenths, rxv,
    fmSrc[0], fmSrc[1], fmSrc[2], fmSrc[3], fmDst[0], fmDst[1], fmDst[2], fmDst[3], fmW[0], fmW[1], fmW[2], fmW[3],
    (int)nrfPower, (int)buzPassive, (int)buzTone, (int)dashStyle, FW_VERSION, otaMode ? 1 : 0);
  if (n < 0 || n >= cap) return -1;
  if (anaActive() && n > 0 && buf[n - 1] == '}') {     // spektrum 126 kanal, hanya saat analisis dibuka
    int w = n - 1;
    w += snprintf(buf + w, cap - w, ",\"sp\":[");
    for (int i = 0; i < 126 && w < cap - 8; i++) w += snprintf(buf + w, cap - w, i ? ",%d" : "%d", (int)anaLevel[i]);
    if (w < cap - 4) { buf[w++] = ']'; buf[w++] = '}'; buf[w] = 0; n = w; }
  }
  return n;
}

void webData() {
  static char buf[2400];
  int n = buildDataJson(buf, (int)sizeof(buf));
  if (n < 0) { web.send(500, "text/plain", "overflow"); return; }
  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "application/json", buf);
}

// ----- Pengaturan lewat web -----
// Halaman web mengirim POST /set dengan k = nama pengaturan, v = nilai. Nilai langsung dipakai,
// penyimpanan ke EEPROM dan hal yang menyentuh layar dikerjakan TaskDisplay lewat webService().
#define WD_BRI     (1u << 0)
#define WD_THEME   (1u << 1)
#define WD_WIFI    (1u << 2)
#define WD_BT      (1u << 3)
#define WD_VOL     (1u << 4)
#define WD_SND     (1u << 5)
#define WD_CFG     (1u << 6)     // semua pengaturan model dan umum (rate, expo, endpoint, failsafe, mixing, timer, batas baterai)
#define WD_REV(i)  (1u << (8 + (i)))
#define WD_TRIM(i) (1u << (16 + (i)))
volatile uint32_t webDirty = 0;
volatile uint8_t webCalReq = 0;     // 1 = mulai kalibrasi, 2 = lanjut, 3 = batal
volatile int8_t webModelReq = -1;   // model yang diminta (0..4), -1 = tidak ada
// Operasi model dari aplikasi (tambah, ganti nama, hapus). Dikerjakan TaskDisplay di webService() karena EEPROM
// hanya boleh disentuh satu task. Pemanggil mengisi argumen, mengisi mdlOp terakhir, lalu menunggu mdlOp kembali 0.
volatile uint8_t mdlOp = 0;        // 0 = kosong, 1 = tambah, 2 = ganti nama, 3 = hapus
volatile int mdlIdx = 0;
volatile int mdlRes = 0;
static char mdlArgName[16];
volatile uint8_t webRestoreReq = 0; // 1 = data backup di webRestoreBuf siap dipasang
static uint8_t webRestoreBuf[EEPROM_SIZE];
portMUX_TYPE webMux = portMUX_INITIALIZER_UNLOCKED;

void webMark(uint32_t bits) {
  portENTER_CRITICAL(&webMux);
  webDirty = webDirty | bits;
  portEXIT_CRITICAL(&webMux);
}

bool webParseInt(const String& s, long& out) {
  int len = (int)s.length();
  if (len == 0 || len > 6) return false;
  int i = 0;
  bool neg = false;
  if (s[0] == '-') { neg = true; i = 1; if (len == 1) return false; }
  long v = 0;
  for (; i < len; i++) {
    char c = s[i];
    if (c < '0' || c > '9') return false;
    v = v * 10 + (c - '0');
  }
  out = neg ? -v : v;
  return true;
}

// Terapkan satu pengaturan. Dipakai web (/set) dan aplikasi (Bluetooth). Mengembalikan 200, 400 atau 409.
// Alasan penolakan 409 ada di settingMsg.
static const char* settingMsg = "";
int applySetting(const String& k, long n) {
  settingMsg = "";
  // Nama berakhiran angka (rev0, trim3, snd5) dipecah jadi nama dasar dan indeks
  String base = k;
  int ci = -1;
  int kl = (int)k.length();
  if (kl > 1 && k[kl - 1] >= '0' && k[kl - 1] <= '9') {
    ci = k[kl - 1] - '0';
    base = k.substring(0, kl - 1);
  }
  bool bin = (n == 0 || n == 1);
  bool ok = true;

  if (k == "bri" && n >= 0 && n <= BRIGHTNESS_MAX) {
    brightnessLevel = (int)n;
    oledBrightness = (int)map(n, 0L, (long)BRIGHTNESS_MAX, 0L, 255L);
    webMark(WD_BRI);
  } else if (k == "thm" && bin) {
    darkMode = (n == 1);
    webMark(WD_THEME);
  } else if (base == "rev" && ci >= 0 && ci < 6 && bin) {
    channelReverse[ci] = (n == 1);
    webMark(WD_REV(ci));
  } else if (base == "trim" && ci >= 0 && ci < 6 && n >= -TRIM_LIMIT && n <= TRIM_LIMIT) {
    channelTrim[ci] = (int8_t)n;
    webMark(WD_TRIM(ci));
  } else if (k == "wifi" && bin) {
    if (n == 1 && btOn) { settingMsg = "Matikan Bluetooth dulu"; return 409; }
    wifiOn = (n == 1);
    webMark(WD_WIFI);
  } else if (k == "bt" && bin) {
    if (n == 1 && wifiOn) { settingMsg = "Matikan WiFi dulu"; return 409; }
    btOn = (n == 1);
    if (!btOn && inputSrc == INPUT_HP) inputSrc = INPUT_REMOT;   // tanpa Bluetooth, kembali ke joystick
    webMark(WD_BT);
  } else if (k == "src" && bin) {
    if (n == 1 && !(btActive && btConnected)) { settingMsg = "Sambungkan HP lewat Bluetooth dulu"; return 409; }
    inputSrc = (uint8_t)n;
  } else if (k == "svol" && n >= 1 && n <= VOL_MAX) {
    soundVol = (uint8_t)n;
    sndPreview(SND_CLICK, soundVol);
    webMark(WD_VOL);
  } else if (base == "snd" && ci >= 0 && ci < SND_COUNT && bin) {
    if (n) soundMask |= (uint16_t)(1u << ci); else soundMask &= (uint16_t)~(1u << ci);
    if (n) sndPreview((uint8_t)ci, 0);
    webMark(WD_SND);
  } else if (k == "test" && n >= 0 && n < SND_COUNT) {
    sndPreview((uint8_t)n, 0);
  } else if (k == "cal" && n >= 1 && n <= 3) {
    webCalReq = (uint8_t)n;
  } else if (k == "model" && n >= 0 && n < modelCount) {
    webModelReq = (int8_t)n;
  } else if (base == "dr" && ci >= 0 && ci < 4 && n >= 30 && n <= 100) {
    dualRate[ci] = (uint8_t)n; webMark(WD_CFG);
  } else if (base == "ex" && ci >= 0 && ci < 4 && n >= 0 && n <= 100) {
    expoAmt[ci] = (uint8_t)n; webMark(WD_CFG);
  } else if (base == "lo" && ci >= 0 && ci < 6 && n >= 20 && n <= 120) {
    epLo[ci] = (uint8_t)n; webMark(WD_CFG);
  } else if (base == "hi" && ci >= 0 && ci < 6 && n >= 20 && n <= 120) {
    epHi[ci] = (uint8_t)n; webMark(WD_CFG);
  } else if (base == "fs" && ci >= 0 && ci < 8 && n >= 0 && n <= 100) {
    failsafePct[ci] = (uint8_t)n; webMark(WD_CFG);
  } else if (k == "mixt" && n >= 0 && n <= 2) {
    mixSetType((uint8_t)n); webMark(WD_CFG);
  } else if (k == "mixa" && n >= 1 && n <= 6) {
    mixSetChan(0, (int)n); webMark(WD_CFG);
  } else if (k == "mixb" && n >= 1 && n <= 6) {
    mixSetChan(1, (int)n); webMark(WD_CFG);
  } else if (k == "mixw" && n >= 10 && n <= 100) {
    mixWeight = (uint8_t)n; webMark(WD_CFG);
  } else if (k == "tmin" && n >= 0 && n <= 99) {
    timerCountdownMin = (uint8_t)n; timerBeeped = false; webMark(WD_CFG);
  } else if (k == "ttrig" && n >= 0 && n <= 8) {
    timerTrig = (uint8_t)n; webMark(WD_CFG);
  } else if (k == "tthr" && n >= 5 && n <= 95) {
    timerThr = (uint8_t)n; webMark(WD_CFG);
  } else if (k == "tdir" && bin) {
    timerDir = (uint8_t)n; webMark(WD_CFG);
  } else if (k == "trtrig" && n >= 0 && n <= 8) {
    timerRstTrig = (uint8_t)n; webMark(WD_CFG);
  } else if (k == "trdir" && bin) {
    timerRstDir = (uint8_t)n; webMark(WD_CFG);
  } else if (k == "trun" && bin) {
    timerRun = (n == 1);
  } else if (k == "treset" && n == 1) {
    timerReset();
  } else if (k == "blow" && n >= 50 && n <= 84) {
    battLowTenths = (uint8_t)n; webMark(WD_CFG);
  } else if (base == "fms" && ci >= 0 && ci < 4 && n >= 0 && n <= 8) {
    fmSrc[ci] = (uint8_t)n; webMark(WD_CFG);
  } else if (base == "fmd" && ci >= 0 && ci < 4 && n >= 1 && n <= 8) {
    fmDst[ci] = (uint8_t)n; webMark(WD_CFG);
  } else if (base == "fmw" && ci >= 0 && ci < 4 && n >= -100 && n <= 100) {
    fmW[ci] = (int8_t)n; webMark(WD_CFG);
  } else if (k == "ana" && bin) {
    if (n == 1) anaStart(); else anaTouch = 0;
  } else if (k == "nrf" && n >= 0 && n <= 4) {
    nrfPower = (uint8_t)n; webMark(WD_CFG);
  } else if (k == "btype" && bin) {
    buzPassive = (uint8_t)n; sndPreview(SND_MENU, 0); webMark(WD_CFG);
  } else if (k == "dash" && n >= 0 && n <= 4) {
    dashStyle = (uint8_t)n; webMark(WD_CFG);
  } else if (k == "btone" && n >= 10 && n <= 50) {
    buzTone = (uint8_t)n; sndPreview(SND_MENU, 0); webMark(WD_CFG);
  } else {
    ok = false;
  }
  return ok ? 200 : 400;
}

void webSet() {
  long n = 0;
  if (!webParseInt(web.arg("v"), n)) { web.send(400, "text/plain", "nilai salah"); return; }
  int code = applySetting(web.arg("k"), n);
  if (code != 200) { web.send(code, "text/plain", code == 409 ? settingMsg : "ditolak"); return; }
  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "text/plain", "ok");
}

// Cadangan semua pengaturan: teks "EDGETX1:" diikuti isi EEPROM dalam heksadesimal.
static char backupOut[8 + 2 * EEPROM_SIZE + 1];
static void backupText() {
  char* out = backupOut;
  memcpy(out, "EDGETX1:", 8);
  static const char* H = "0123456789ABCDEF";
  for (int i = 0; i < EEPROM_SIZE; i++) {
    uint8_t b = EEPROM.read(i);
    out[8 + 2 * i] = H[b >> 4];
    out[9 + 2 * i] = H[b & 15];
  }
  out[8 + 2 * EEPROM_SIZE] = 0;
}

void webBackup() {
  backupText();
  web.sendHeader("Cache-Control", "no-store");
  web.sendHeader("Content-Disposition", "attachment; filename=edgetx-backup.txt");
  web.send(200, "text/plain", backupOut);
}

static int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

// Pulihkan dari cadangan. Data diperiksa dulu (awalan, panjang, karakter, penanda model),
// dipasang oleh webService() lalu remot restart.
void webRestore() {
  String d = web.arg("d");
  int need = 8 + 2 * EEPROM_SIZE;
  int needOld = 8 + 2 * 300;      // cadangan versi lama (5 model tetap)
  int needMid = 8 + 2 * 650;      // cadangan versi 3.2 (tanpa mix bebas)
  int dl = (int)d.length();
  if ((dl != need && dl != needOld && dl != needMid) || d.substring(0, 8) != "EDGETX1:") { web.send(400, "text/plain", "File cadangan tidak cocok"); return; }
  static uint8_t tmp[EEPROM_SIZE];
  memset(tmp, 0xFF, sizeof(tmp));
  int nb = (dl - 8) / 2;
  for (int i = 0; i < nb; i++) {
    int h = hexVal(d[8 + 2 * i]), l = hexVal(d[9 + 2 * i]);
    if (h < 0 || l < 0) { web.send(400, "text/plain", "File cadangan rusak"); return; }
    tmp[i] = (uint8_t)(h * 16 + l);
  }
  if (tmp[EEPROM_ADDR_MAGIC] != EEPROM_MAGIC) { web.send(400, "text/plain", "File cadangan tidak cocok"); return; }
  if (webRestoreReq) { web.send(409, "text/plain", "Sedang memulihkan"); return; }
  memcpy(webRestoreBuf, tmp, EEPROM_SIZE);
  webRestoreReq = 1;
  web.send(200, "text/plain", "ok");
}

// ================= UPDATE FIRMWARE (OTA) =================
// Halaman unggah. Baris JavaScript diberi spasi di depan supaya penyusun prototipe PlatformIO tidak mengiranya fungsi C++.
static const char FW_PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="id"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Update Firmware</title>
<style>
body{font-family:system-ui,sans-serif;background:#0b0f14;color:#e6edf3;margin:0;padding:22px;max-width:520px}
h2{margin:0 0 6px}p{color:#7d8b99;line-height:1.5}
input[type=file]{display:block;margin:16px 0;color:#e6edf3}
button{background:#3fb6ff;color:#001018;border:0;border-radius:10px;padding:12px 18px;font-size:16px;font-weight:600}
#bar{height:14px;background:#121922;border-radius:7px;margin:18px 0 8px;overflow:hidden}
#fill{display:block;height:100%;width:0;background:#3ddc84}
</style></head><body>
<h2>Update Firmware ESP32 TX</h2>
<p>Pilih file <b>firmware.bin</b> hasil build PlatformIO, yaitu <code>.pio/build/esp32dev/firmware.bin</code>. Bukan file .ino atau .cpp. Jangan matikan remot selama proses.</p>
<input type="file" id="f" accept=".bin">
<button id="go">Unggah dan pasang</button>
<div id="bar"><i id="fill"></i></div>
<p id="msg"></p>
<script>
 var f=document.getElementById("f"),b=document.getElementById("go"),m=document.getElementById("msg"),fl=document.getElementById("fill");
 b.onclick=function(){
  if(!f.files.length){m.textContent="Pilih file .bin dulu";return;}
  var file=f.files[0],fd=new FormData(),x=new XMLHttpRequest();
  fd.append("firmware",file,file.name);
  x.open("POST","/update?size="+file.size);
  x.upload.onprogress=function(e){if(e.lengthComputable){var p=Math.round(e.loaded*100/e.total);fl.style.width=p+"%";m.textContent="Mengunggah "+p+"%";}};
  x.onload=function(){if(x.status==200){fl.style.width="100%";m.textContent="Berhasil. Remot restart sendiri, tunggu beberapa detik.";}else{m.textContent="Gagal: "+x.responseText;}};
  x.onerror=function(){m.textContent="Koneksi terputus";};
  m.textContent="Mengunggah...";
  x.send(fd);
 };
</script>
</body></html>)rawliteral";

static void otaFail(const char* m) {
  strncpy(otaMsg, m, sizeof(otaMsg) - 1);
  otaMsg[sizeof(otaMsg) - 1] = 0;
  otaState = 3;
  otaFailAt = millis();
}

void webFwPage() {
  web.sendHeader("Cache-Control", "no-store");
  web.send_P(200, "text/html", FW_PAGE);
}

// Menerima berkas firmware (multipart). Ukuran boleh dikirim lewat /update?size=N untuk persen kemajuan.
void webFwUpload() {
  HTTPUpload& up = web.upload();
  if (up.status == UPLOAD_FILE_START) {
    otaMode = true;
    otaState = 1;
    otaBytes = 0;
    otaTotal = 0;
    otaMsg[0] = 0;
    long sz = 0;
    if (webParseInt(web.arg("size"), sz) && sz > 0) otaTotal = (uint32_t)sz;
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) otaFail("Partisi tanpa OTA");
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (otaState != 1) return;
    if (otaBytes == 0 && up.currentSize > 0 && up.buf[0] != 0xE9) {
      otaFail("Bukan file firmware");
      Update.abort();
      return;
    }
    if (Update.write(up.buf, up.currentSize) != up.currentSize) {
      otaFail("Gagal menulis flash");
      Update.abort();
      return;
    }
    otaBytes += up.currentSize;
  } else if (up.status == UPLOAD_FILE_END) {
    if (otaState != 1) return;
    if (Update.end(true)) otaState = 2;
    else otaFail("Berkas tidak valid");
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    otaFail("Unggah dibatalkan");
  }
}

void webFwDone() {
  if (otaState == 2) {
    web.send(200, "text/plain", "OK");
    otaRestartAt = millis() + 1500;
  } else {
    web.send(500, "text/plain", otaMsg[0] ? otaMsg : "Gagal");
  }
}

// Layar OLED saat mode update firmware
void drawOtaScreen() {
  display.clearDisplay();
  uiHeader("UPDATE FIRMWARE", "");
  display.setTextColor(SSD1306_WHITE);
  if (otaBle) {
    display.setCursor(2, 14);
    display.print("Lewat Bluetooth");
    display.setCursor(2, 23);
    display.print("dari aplikasi HP");
  } else {
    display.setCursor(2, 14);
    display.print("WiFi ");
    display.print(WIFI_AP_SSID);
    display.setCursor(2, 23);
    display.print("Pass ");
    display.print(WIFI_AP_PASS);
    display.setCursor(2, 32);
    display.print("Buka 192.168.4.1");
  }

  char st[28];
  int pct = 0;
  if (otaTotal > 0) pct = (int)((uint64_t)otaBytes * 100 / otaTotal);
  if (pct > 100) pct = 100;
  if (otaState == 0)      strcpy(st, wifiActive ? "Menunggu .bin" : "Menyalakan WiFi...");
  else if (otaState == 1) { if (otaTotal > 0) snprintf(st, sizeof(st), "Menerima %d%%", pct); else snprintf(st, sizeof(st), "Menerima %luK", (unsigned long)(otaBytes / 1024)); }
  else if (otaState == 2) strcpy(st, "Selesai, restart...");
  else                    snprintf(st, sizeof(st), "%s", otaMsg[0] ? otaMsg : "Gagal");
  display.setCursor(2, 40);
  display.print(st);
  if (otaState == 0 && wifiActive) {
    char nb[6];
    snprintf(nb, sizeof(nb), "HP%d", WiFi.softAPgetStationNum());
    uiPrintR(126, 40, nb);
  }

  // Bar kemajuan
  display.drawRoundRect(2, 48, 124, 6, 2, SSD1306_WHITE);
  int w = otaState == 2 ? 120 : (otaState == 1 && otaTotal > 0 ? pct * 120 / 100 : 0);
  if (w > 0) display.fillRect(4, 50, w, 2, SSD1306_WHITE);
  if (otaState == 1 && otaTotal == 0) {
    int x = 4 + (int)((millis() / 20) % 100);
    display.fillRect(x, 50, 16, 2, SSD1306_WHITE);
  }

  display.setCursor(2, 56);
  display.print(otaState == 1 ? "Jangan matikan remot" : "HOLD = batal");
  display.display();
}

// ================= PERINTAH DARI APLIKASI LEWAT BLUETOOTH =================
// Satu baris perintah, jawaban berupa baris teks:
//   G              minta JSON status lengkap, dijawab "J{...}"
//   S,kunci,nilai  ubah pengaturan (kunci sama dengan /set), dijawab "A,S:kunci,kode"
//   MA,nama        tambah model, MN,i,nama ganti nama, MX,i hapus. Jawaban "A,MA:indeks,200" atau kode 400, 409
//   BK             minta cadangan, dijawab "BK:EDGETX1:..."
//   RS,i,hex       potongan pemulihan ke-i (50 byte), RC = pasang, dijawab "A,RC,kode"
//   OTA            masuk mode update firmware, dijawab "A,OTA,200"
static void bleSendBytes(const char* d, int n) {
  if (!(btActive && btConnected && bleTx)) return;
  int chunk = 20;
  if (bleServer) {
    int m = (int)bleServer->getPeerMTU(bleConnHandle);
    if (m > 23) chunk = m - 3;
  }
  if (chunk > 180) chunk = 180;
  for (int i = 0; i < n; i += chunk) {
    int l = (n - i < chunk) ? (n - i) : chunk;
    bleTx->setValue((uint8_t*)(d + i), l);
    bleTx->notify();
    vTaskDelay(pdMS_TO_TICKS(12));
    if (!btConnected) return;
  }
}

static void btReply(const char* name, int code) {
  char b[64];
  int n = snprintf(b, sizeof(b), "A,%s,%d\n", name, code);
  bleSendBytes(b, n);
}

static uint8_t btRestoreBuf[EEPROM_SIZE];
#define EEPROM_SIZE_OLD 300    // ukuran cadangan versi lama (5 model tetap), sisanya dianggap kosong (0xFF)
#define EEPROM_SIZE_MID 650    // cadangan versi 3.2 (tanpa mix bebas)
static uint32_t btRestoreMask = 0;

void btServiceCmds() {
  static char line[BTQ_LEN];
  static char jb[2400];
  int guard = 0;
  while (guard++ < 4 && btQueuePop(line)) {
    if (strcmp(line, "G") == 0) {
      jb[0] = 'J';
      int n = buildDataJson(jb + 1, (int)sizeof(jb) - 3);
      if (n < 0) { btReply("G", 500); continue; }
      jb[1 + n] = '\n';
      bleSendBytes(jb, n + 2);
    } else if (strncmp(line, "S,", 2) == 0) {
      char* k = line + 2;
      char* c = strchr(k, ',');
      long v = 0;
      char nm[28];
      if (!c) { btReply("S", 400); continue; }
      *c = 0;
      snprintf(nm, sizeof(nm), "S:%s", k);
      if (!webParseInt(String(c + 1), v)) { btReply(nm, 400); continue; }
      btReply(nm, applySetting(String(k), v));
    } else if (strncmp(line, "MA,", 3) == 0 || strncmp(line, "MN,", 3) == 0 || strncmp(line, "MX,", 3) == 0) {
      // Model: MA,nama (tambah)  MN,indeks,nama (ganti nama)  MX,indeks (hapus)
      char t0 = line[1];
      const char* nm = "MA";
      int op = 1, idx = 0;
      mdlArgName[0] = 0;
      if (t0 == 'A') {
        strncpy(mdlArgName, line + 3, sizeof(mdlArgName) - 1);
        mdlArgName[sizeof(mdlArgName) - 1] = 0;
      } else {
        nm = (t0 == 'N') ? "MN" : "MX";
        op = (t0 == 'N') ? 2 : 3;
        idx = atoi(line + 3);
        if (t0 == 'N') {
          char* c = strchr(line + 3, ',');
          if (!c) { btReply(nm, 400); continue; }
          strncpy(mdlArgName, c + 1, sizeof(mdlArgName) - 1);
          mdlArgName[sizeof(mdlArgName) - 1] = 0;
        }
      }
      if (mdlOp != 0) { btReply(nm, 409); continue; }
      mdlIdx = idx;
      mdlRes = 0;
      mdlOp = (uint8_t)op;
      uint32_t t1 = millis();
      while (mdlOp != 0 && (uint32_t)(millis() - t1) < 1500) vTaskDelay(pdMS_TO_TICKS(10));
      int code = (mdlOp == 0) ? mdlRes : 500;
      if (op == 1 && code == 200) { char nb[16]; snprintf(nb, sizeof(nb), "MA:%d", (int)mdlIdx); btReply(nb, 200); }
      else btReply(nm, code);
    } else if (strcmp(line, "BK") == 0) {
      backupText();
      int n = (int)strlen(backupOut);
      static char bk[8 + 2 * EEPROM_SIZE + 8];
      bk[0] = 'B'; bk[1] = 'K'; bk[2] = ':';
      memcpy(bk + 3, backupOut, n);
      bk[3 + n] = '\n';
      bleSendBytes(bk, n + 4);
    } else if (strncmp(line, "RS,", 3) == 0) {
      int idx = atoi(line + 3);
      char* h = strchr(line + 3, ',');
      int nChunk = EEPROM_SIZE / 50;
      if (idx == 0) { memset(btRestoreBuf, 0xFF, sizeof(btRestoreBuf)); btRestoreMask = 0; }
      bool good = (h != NULL) && idx >= 0 && idx < nChunk && strlen(h + 1) == 100;
      if (good) {
        for (int i = 0; i < 50; i++) {
          int hi = hexVal(h[1 + 2 * i]), lo = hexVal(h[2 + 2 * i]);
          if (hi < 0 || lo < 0) { good = false; break; }
          btRestoreBuf[idx * 50 + i] = (uint8_t)(hi * 16 + lo);
        }
      }
      if (good) btRestoreMask |= (1u << idx);
      btReply("RS", good ? 200 : 400);
    } else if (strcmp(line, "RC") == 0) {
      uint32_t full = (1u << (EEPROM_SIZE / 50)) - 1;
      uint32_t fullOld = (1u << (EEPROM_SIZE_OLD / 50)) - 1;
      uint32_t fullMid = (1u << (EEPROM_SIZE_MID / 50)) - 1;
      int code = 200;
      if ((btRestoreMask != full && btRestoreMask != fullOld && btRestoreMask != fullMid) || btRestoreBuf[EEPROM_ADDR_MAGIC] != EEPROM_MAGIC) code = 400;
      else if (webRestoreReq) code = 409;
      else { memcpy(webRestoreBuf, btRestoreBuf, EEPROM_SIZE); webRestoreReq = 1; }
      btRestoreMask = 0;
      btReply("RC", code);
    } else if (strncmp(line, "FWB,", 4) == 0) {
      long sz = atol(line + 4);
      if (otaMode || sz < 102400 || sz > 1966080) { btReply("FWB", otaMode ? 409 : 400); continue; }
      if (!Update.begin((size_t)sz)) { btReply("FWB", 500); continue; }
      otaMsg[0] = 0;
      otaBytes = 0; otaTotal = (uint32_t)sz; otaHead = 0; otaTail = 0; otaLastAck = 0;
      otaLastRx = millis();
      otaBle = true; otaState = 1; otaMode = true;
      bleRaw = true;                       // harus aktif sebelum balasan keluar: data pertama bisa tiba langsung setelahnya
      btReply("FWB", 200);
    } else if (strcmp(line, "OTA") == 0) {
      btReply("OTA", 200);
      vTaskDelay(pdMS_TO_TICKS(400));        // beri waktu jawaban terkirim sebelum Bluetooth dimatikan
      otaRequest = true;
    } else {
      btReply("?", 400);
    }
  }
}

// Menulis data firmware dari ring buffer ke flash. Dipanggil TaskConn selama update lewat Bluetooth.
static void otaBleService() {
  if (otaState == 3) {
    if ((int32_t)(millis() - otaFailAt) > 3500) {          // gagal: kembali ke tampilan normal
      otaMode = false; otaBle = false; otaState = 0; otaMsg[0] = 0;
    }
    return;
  }
  if (otaState != 1) return;
  if (!btConnected) {
    bleRaw = false; Update.abort(); otaFail("Bluetooth putus");
    return;
  }
  if ((int32_t)(millis() - otaLastRx) > 8000) {
    bleRaw = false; Update.abort(); otaFail("Tidak ada data");
    btReply("F", 408);
    return;
  }
  uint32_t avail = otaHead - otaTail;
  while (avail > 0 && otaState == 1) {
    uint32_t idx = otaTail % OTA_RING;
    uint32_t n = avail;
    if (n > OTA_RING - idx) n = OTA_RING - idx;
    if (n > 4096) n = 4096;
    if (otaBytes == 0 && otaRing[idx] != 0xE9) {
      bleRaw = false; Update.abort(); otaFail("Bukan file firmware"); btReply("F", 415);
      return;
    }
    if (otaBytes + n > otaTotal) n = otaTotal - otaBytes;
    if (n == 0) break;
    if (Update.write(&otaRing[idx], n) != n) {
      bleRaw = false; Update.abort(); otaFail("Gagal menulis flash"); btReply("F", 500);
      return;
    }
    otaTail += n;
    otaBytes += n;
    avail = otaHead - otaTail;
    if (otaBytes - otaLastAck >= 4096 && otaBytes < otaTotal) {
      otaLastAck = otaBytes;
      char b[24];
      int l = snprintf(b, sizeof(b), "F,%lu\n", (unsigned long)otaBytes);
      bleSendBytes(b, l);
    }
  }
  if (otaState == 1 && otaBytes >= otaTotal) {
    bleRaw = false;
    if (Update.end(true)) {
      otaState = 2;
      bleSendBytes("F,DONE\n", 7);
      otaRestartAt = millis() + 1500;
    } else {
      otaFail("Berkas tidak valid");
      btReply("F", 422);
    }
  }
}

// Dipanggil TaskDisplay tiap frame. Hanya satu task yang menyentuh EEPROM, layar (I2C) dan kalibrasi,
// jadi perubahan dari web diterapkan di sini.
void webService() {
  uint32_t d;
  portENTER_CRITICAL(&webMux);
  d = webDirty;
  webDirty = 0;
  portEXIT_CRITICAL(&webMux);
  if (d) {
    if (d & WD_BRI)   { EEPROM.write(EEPROM_ADDR_BRIGHT, brightnessLevel); lastBrightnessSaved = brightnessLevel; }
    if (d & WD_THEME) { EEPROM.write(EEPROM_ADDR_OUTPUT, darkMode ? 1 : 0); themeIndex = darkMode ? 1 : 0; display.invertDisplay(!darkMode); }
    for (int i = 0; i < 6; i++) {
      if (d & WD_REV(i))  EEPROM.write(EEPROM_ADDR_REVERSE + i, channelReverse[i] ? 1 : 0);
      if (d & WD_TRIM(i)) EEPROM.write(EEPROM_ADDR_TRIM + i, channelTrim[i] + 128);
    }
    if (d & WD_WIFI) EEPROM.write(EEPROM_ADDR_WIFI, wifiOn ? 1 : 0);
    if (d & WD_BT)   EEPROM.write(EEPROM_ADDR_BT, btOn ? 1 : 0);
    if (d & WD_VOL)  EEPROM.write(EEPROM_ADDR_VOL, soundVol);
    if (d & WD_SND)  soundMaskSave();
    if (d & WD_CFG)  cfgSaveWorking();
    EEPROM.commit();
  }
  {
    uint8_t op = mdlOp;
    if (op) {
      int res = 500;
      if (op == 1) {
        int r = modelAdd(mdlArgName);
        res = r >= 0 ? 200 : (r == -1 ? 409 : 400);
        mdlIdx = r >= 0 ? r : 0;
      } else if (op == 2) {
        res = modelRename(mdlIdx, mdlArgName);
      } else if (op == 3) {
        res = modelDelete(mdlIdx);
      }
      if (res == 200) {      // daftar berubah: kursor menu model tetap di dalam daftar
        if (paramIndex >= modelCount) paramIndex = modelCount - 1;
        if (currentMode == MODE_PARAM && paramPage == PG_MODEL) reverseEditMode = false;
      }
      mdlRes = res;
      mdlOp = 0;
    }
  }
  int8_t mreq = webModelReq;
  if (mreq >= 0) {
    webModelReq = -1;
    modelSwitch(mreq);
  }
  if (webRestoreReq) {
    webRestoreReq = 0;
    for (int i = 0; i < EEPROM_SIZE; i++) EEPROM.write(i, webRestoreBuf[i]);
    EEPROM.commit();
    vTaskDelay(pdMS_TO_TICKS(400));      // beri waktu jawaban "ok" sampai ke halaman
    ESP.restart();
  }
  uint8_t c = webCalReq;
  if (c) {
    webCalReq = 0;
    if (c == 1 && currentMode != MODE_CALIB)      gotoMode(MODE_CALIB);   // navTick memanggil calibBegin()
    else if (c == 2 && currentMode == MODE_CALIB) calibPress();
    else if (c == 3 && currentMode == MODE_CALIB) gotoMode(MODE_LIST);
  }
}

void wifiStart() {
  wifiActive = true;            // ditandai dulu supaya baterai di ADC2 berhenti dibaca sebelum WiFi jalan
  vTaskDelay(pdMS_TO_TICKS(30));
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS, WIFI_AP_CH, 0, 4);
  WiFi.setTxPower(WIFI_POWER_11dBm);    // daya pancar WiFi dikurangi: cukup untuk HP di dekat remot dan tidak mengganggu modul NRF24 di sebelahnya
  WiFi.setSleep(bleInited);             // WiFi saja: tanpa hemat daya. Kalau Bluetooth pernah menyala: modem sleep (wajib untuk koeksistensi)
  if (!webRoutesSet) {
    web.on("/", webRoot);
    // Alamat pengecekan internet Android, iPhone dan Windows: dialihkan ke halaman remot
    web.on("/generate_204", webProbe);
    web.on("/gen_204", webProbe);
    web.on("/hotspot-detect.html", webProbe);
    web.on("/library/test/success.html", webProbe);
    web.on("/connecttest.txt", webProbe);
    web.on("/ncsi.txt", webProbe);
    web.on("/redirect", webProbe);
    web.on("/canonical.html", webProbe);
    web.on("/fw", webFwPage);
    web.on("/update", HTTP_POST, webFwDone, webFwUpload);
    web.on("/data", webData);
    web.on("/set", webSet);
    web.on("/backup", webBackup);
    web.on("/restore", HTTP_POST, webRestore);
    web.onNotFound(webNotFound);
    webRoutesSet = true;
  }
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", WiFi.softAPIP());
  web.begin();
  DBG("[WiFi] heap bebas %u byte\n", (unsigned)ESP.getFreeHeap());
  DBG("[WiFi] AP %s aktif, halaman terbuka otomatis (captive portal), atau http://192.168.4.1\n", WIFI_AP_SSID);
}

void wifiStop() {
  dnsServer.stop();
  web.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  vTaskDelay(pdMS_TO_TICKS(50));
  wifiActive = false;           // baru boleh baca ADC2 lagi
  DBG("[WiFi] mati\n");
}

// Task core 1: menyalakan/mematikan WiFi dan Bluetooth sesuai menu, melayani web
void TaskConn(void *pvParameters) {
  (void) pvParameters;
  vTaskDelay(pdMS_TO_TICKS(800));   // tunggu baterai terbaca dan boot selesai
  uint32_t lastNotify = 0;
  for (;;) {
    // Permintaan mode update firmware (menu remot atau aplikasi)
    if (otaRequest) {
      otaRequest = false;
      if (!otaMode) { otaMode = true; otaState = 0; otaBytes = 0; otaTotal = 0; otaMsg[0] = 0; }
    }
    if (otaRestartAt && (int32_t)(millis() - otaRestartAt) >= 0) { vTaskDelay(pdMS_TO_TICKS(200)); ESP.restart(); }

    // WiFi dan Bluetooth tidak pernah menyala bersamaan. Mode update memakai WiFi sementara.
    bool wifiWant = wifiOn || (otaMode && !otaBle);
    bool btWant = btOn && !wifiWant;
    if (wifiWant && !wifiActive)      wifiStart();
    else if (!wifiWant && wifiActive) wifiStop();

    if (btWant && !btActive && !wifiActive) bleStart();
    else if (!btWant && btActive)           bleStop();

    // Pengaman: input HP hanya sah kalau Bluetooth aktif dan ada HP tersambung
    if (inputSrc == INPUT_HP && !(btActive && btConnected)) inputSrc = INPUT_REMOT;

    if (wifiActive) {
      dnsServer.processNextRequest();
      web.handleClient();
    }
    if (btActive && btConnected && !bleRaw) btServiceCmds();      // perintah dari aplikasi
    if (otaBle) otaBleService();

    // Suara terhubung / putus: Bluetooth ke HP dan HP yang masuk ke hotspot WiFi
    {
      static bool lastBtc = false;
      static int lastSta = 0;
      static uint32_t lastStaCheck = 0;
      uint32_t t = millis();
      bool btc = btActive && btConnected;
      if (btc && !lastBtc) sndPlay(SND_BT_CONN);
      if (!btc && lastBtc) sndPlay(SND_DISCONNECT);
      lastBtc = btc;
      if (t - lastStaCheck >= 200) {
        lastStaCheck = t;
        int sta = wifiActive ? (int)WiFi.softAPgetStationNum() : 0;
        if (sta > lastSta) sndPlay(SND_WIFI_CONN);
        if (sta < lastSta) sndPlay(SND_DISCONNECT);
        lastSta = sta;
      }
    }

    uint32_t now = millis();
    if (btActive && bleNeedAdv && now - bleDiscMs > 300) {
      NimBLEDevice::startAdvertising();
      bleNeedAdv = false;
    }

    if (btActive && btConnected && bleTx && !otaBle && now - lastNotify >= 500) {
      lastNotify = now;
      char s[32];
      int n = snprintf(s, sizeof(s), "V%.2f,H%d,L%d,K%d\n",
                       batteryVoltage, inputSrc, linkBars, hpDataOk ? 1 : 0);
      bleTx->setValue((uint8_t*)s, n);
      bleTx->notify();
    }

    vTaskDelay(pdMS_TO_TICKS((wifiActive || otaBle) ? 2 : 20));
  }
}

// ================= TASKS =================
// Task on core 0: read inputs and send radio
// Satu irisan pemindaian. Dipanggil oleh task pemilik radio di antara dua pengiriman NRF, lalu kanal 110 dipasang lagi.
// full = seluruh 126 kanal sekaligus (dipakai saat NRF OFF). Selain itu 21 kanal per putaran supaya pengiriman kendali tidak terganggu.
static bool anaPowered = false;
static void anaScanSlice(bool full) {
  static uint8_t pos = 0;
  int n = full ? 126 : 21;
  radio.startListening();
  for (int i = 0; i < n; i++) {
    uint8_t c = pos;
    pos = (uint8_t)((pos + 1) % 126);
    radio.setChannel(c);
    delayMicroseconds(140);                         // waktu pemantapan penerima
    int hits = radio.testRPD() ? 1 : 0;             // energi lebih kuat dari sekitar -64 dBm
    delayMicroseconds(25); hits += radio.testRPD() ? 1 : 0;
    delayMicroseconds(25); hits += radio.testRPD() ? 1 : 0;
    // rata-rata bergerak halus: naik cepat, turun pelan. Skala 0..1000, 3 cuplikan = 100 persen
    int target = hits * 333;
    int acc = anaAcc[c];
    if (target > acc) acc += (target - acc) / 2; else acc -= (acc - target) / 6;
    anaAcc[c] = (uint16_t)acc;
    uint8_t lv = (uint8_t)(acc / 10 > 100 ? 100 : acc / 10);
    anaLevel[c] = lv;
    if (lv >= anaPeak[c]) { anaPeak[c] = lv; anaHold[c] = 40; }
    else if (anaHold[c] > 0) anaHold[c]--;
    else if (anaPeak[c] > 0) anaPeak[c]--;
  }
  radio.stopListening();
  radio.setChannel(110);
  if (full) anaWfPush();                            // satu baris riwayat per sapuan penuh (dibatasi jarak waktu di dalam fungsi)
}

void TaskInputs(void *pvParameters) {
  (void) pvParameters;
  static bool lastSwState = HIGH;
  static bool lastBtnState = HIGH;
  

  for (;;) {
    bool btnState = digitalRead(BTN);

    // Stick dibaca stabil (oversampling + filter adaptif + deadband),
    // lalu dikoreksi kalibrasi supaya ujung stick = ujung pembacaan
    uint16_t a0 = applyCal(0, stableStickRead(0, J1X));
    uint16_t a1 = applyCal(1, stableStickRead(1, J1Y));
    uint16_t a2 = applyCal(2, stableStickRead(2, J2X));
    uint16_t a3 = applyCal(3, stableStickRead(3, J2Y));
    stickCal[0] = a0; stickCal[1] = a1; stickCal[2] = a2; stickCal[3] = a3;

    uint8_t switchesLocal = 0;
    if (!digitalRead(SW1)) switchesLocal |= 1;
    if (!digitalRead(SW2)) switchesLocal |= 2;
    uint16_t ch5 = digitalRead(SW1) ? 0 : 4095;
    uint16_t ch6 = digitalRead(SW2) ? 0 : 4095;


    // ====== VOLTAGE READ + CALIBRATION ======
    // Tanpa filter lambat. Hasil rata-rata sampel langsung dipakai,
    // jadi saat baru dinyalakan nilainya sudah benar di pembacaan pertama.
    // Pin ADC2 (termasuk GPIO15) tidak bisa dibaca selama WiFi aktif.
    // Saat itu nilai terakhir dipertahankan. Pindahkan sensor ke GPIO36/39 (ADC1) supaya selalu terbaca.
    battFrozen = BATT_ON_ADC2 && wifiActive;
    if (!battFrozen) {
      int rawV = readAnalogAvg(SW3, BATT_SAMPLES);
      float voltADC = rawV * (3.3 / 4095.0);

      const float R1 = 30000.0;
      const float R2 = 7500.0;
      const float calibrationFactor = 1.113;

      float divider = (R1 + R2) / R2;

      batteryVoltage = voltADC * divider * calibrationFactor;
    }

    // Histeresis kecil supaya alarm tidak berkedip di sekitar batas.
    // Alarm ON di bawah 6.40 V, OFF di atas 6.50 V.
    {
      float lowThr = battLowTenths / 10.0f;
      if (batteryVoltage < lowThr) lowBattAlarm = true;
      else if (batteryVoltage > lowThr + 0.10f) lowBattAlarm = false;
    }

    if (xSemaphoreTake(pktMutex, (TickType_t)10) == pdTRUE) {
      uint16_t rawCh[8];
      if (inputSrc != INPUT_HP) {
        // ===== MODE REMOT: joystick dan switch fisik =====
        rawCh[0] = a3;
        rawCh[1] = a2;
        rawCh[2] = a0;
        rawCh[3] = a1;
        rawCh[4] = ch5;   // CH5 = SW1
        rawCh[5] = ch6;   // CH6 = SW2
        rawCh[6] = 2048;  // CH7 hanya bisa diatur dari HP, di mode REMOT diam di tengah
        rawCh[7] = 2048;  // CH8 hanya bisa diatur dari HP, di mode REMOT diam di tengah
        hpDataOk = false;
      } else {
        // ===== MODE HP: joystick remot diabaikan =====
        bool fresh = btConnected && btRxCount > 0 &&
                     (int32_t)(millis() - btLastRxMs) < BT_TIMEOUT_MS;
        hpDataOk = fresh;
        portENTER_CRITICAL(&btMux);
        for (int i = 0; i < 8; i++) rawCh[i] = fresh ? btCh[i] : failsafeRaw(i);
        portEXIT_CRITICAL(&btMux);
        // CH5 dan CH6 meniru switch fisik remot: hanya 0 atau 4095 (nilai HP >= 2048 dianggap ON).
        // Nilai tengah hanya ada di CH7 dan CH8.
        rawCh[4] = (rawCh[4] >= 2048) ? 4095 : 0;
        rawCh[5] = (rawCh[5] >= 2048) ? 4095 : 0;
        // Bit switch mengikuti CH5/CH6 dari HP, supaya RX yang memakai pkt.switches tetap benar
        switchesLocal = (rawCh[4] ? 1 : 0) | (rawCh[5] ? 2 : 0);
      }

      // rate, expo, mixing, reverse, trim, endpoint (lihat applyChannels)
      applyChannels(rawCh, pkt.ch);
      pkt.ch7 = rawCh[6];
      pkt.ch8 = rawCh[7];
      {   // mix bebas pada 8 channel akhir
        uint16_t o8[8];
        for (int i = 0; i < 6; i++) o8[i] = pkt.ch[i];
        o8[6] = pkt.ch7; o8[7] = pkt.ch8;
        freeMixApply(o8);
        for (int i = 0; i < 6; i++) pkt.ch[i] = o8[i];
        pkt.ch7 = o8[6]; pkt.ch8 = o8[7];
      }

      pkt.switches = switchesLocal;
      // Mode baru (daftar menu dan kalibrasi) dikirim sebagai DASHBOARD supaya nilai
      // yang diterima RX tetap sama seperti sebelumnya (0 sampai 6)
      pkt.mode = (currentMode == MODE_LIST || currentMode == MODE_CALIB || currentMode == MODE_CONN || currentMode == MODE_SOUND || currentMode == MODE_PARAM) ? MODE_DASHBOARD : currentMode;
      pkt.outputMode = darkMode;
      xSemaphoreGive(pktMutex);
    }

   {   // daya pancar diubah dari menu atau aplikasi: dipasang di sini, di antara dua pengiriman, oleh task pemilik radio
     static uint8_t nrfApplied = 255;
     static bool nrfWasOff = false;
     uint8_t want = nrfPower;
     bool scan = anaActive();
     if (want >= 4 || scan) {    // OFF atau mode penganalisis: NRF tidak mengirim ke RX
       if (scan) {
         // Penganalisis: NRF dipakai penuh sebagai pemindai 126 kanal, sambungan dengan RX diputus sementara
         nrfWasOff = true;
         if (!anaPowered) { radio.powerUp(); vTaskDelay(pdMS_TO_TICKS(5)); anaPowered = true; }
         anaScanSlice(true);
         vTaskDelay(1);
       } else {
         if (!nrfWasOff || anaPowered) { radio.stopListening(); radio.powerDown(); nrfWasOff = true; anaPowered = false; }
         vTaskDelay(pdMS_TO_TICKS(20));
       }
       linkLost = false; linkBars = 0; txRate = 0; ackRate = 0; lastLinkOK = millis();
       continue;
     }
     if (nrfWasOff) {            // hidup lagi
       radio.powerUp();
       vTaskDelay(pdMS_TO_TICKS(5));
       radio.stopListening();
       anaPowered = false;
       nrfWasOff = false; nrfApplied = 255; lastLinkOK = millis();
     }
     if (want != nrfApplied) { radio.setPALevel(nrfPaLevel(want)); nrfApplied = want; }
   }
   bool ok = radio.write(&pkt, sizeof(pkt));

   // Statistik untuk halaman web: paket per detik dan persen ACK
   {
     static uint32_t statT0 = 0; static uint16_t nTx = 0, nOk = 0;
     nTx++; if (ok) nOk++;
     uint32_t tnow = millis();
     if (tnow - statT0 >= 1000) {
       txRate = nTx; ackRate = nTx ? (uint8_t)((uint32_t)nOk * 100 / nTx) : 0;
       nTx = 0; nOk = 0; statT0 = tnow;
     }
   }

// pindah ke mode receive
radio.startListening();

// kasih waktu RX mengirim balik telemetry
unsigned long timeout = millis();

while (!radio.available()) {

  if (millis() - timeout > 80) {
    break;
  }
}

// kalau ada telemetry → baca
if (radio.available()) {

  radio.read(&telemetry, sizeof(telemetry));
  telemMs = millis();

  // ================= SERIAL TELEMETRY =================
Serial.print("ROLL:");
Serial.print(telemetry.roll);

Serial.print(",PITCH:");
Serial.print(telemetry.pitch);

Serial.print(",ALT:");
Serial.print(telemetry.altitude);

Serial.print(",VBAT:");
Serial.println(telemetry.voltage);

  Serial.print("ROLL RX: ");
Serial.println(telemetry.roll);

  Serial.print("ALT: ");
  Serial.print(telemetry.altitude);

  Serial.print(" | VBAT RX: ");
  Serial.print(telemetry.voltage);

  Serial.print(" | PITCH: ");
  Serial.print(telemetry.pitch);

Serial.print(" | ROLL: ");
Serial.print(telemetry.roll);

Serial.print(" | SIGNAL: ");
Serial.print(linkBars * 20);

Serial.print(" | STAB: ");

if ((pkt.switches & 2) != 0)
  Serial.println("ON");
else
  Serial.println("OFF");
}

// balik lagi ke TX mode
radio.stopListening();
// ===== ANALISIS SINYAL 2,4 GHz (hanya saat halaman analisis atau aplikasi memintanya) =====
// ===== DETEKSI LINK RX =====
unsigned long now = millis();

if (ok) {
  lastLinkOK = now;   // RX masih ada
  linkLost = false;
} else {
  if (now - lastLinkOK > LINK_TIMEOUT_MS) {
    linkLost = true;  // RX benar-benar hilang
  }
}

    if (xSemaphoreTake(pktMutex, (TickType_t)10) == pdTRUE) {
      static int quality = 100;

      if (ok) quality = quality * 0.9 + 100 * 0.1;
      else    quality = quality * 0.9 +   0 * 0.1;

      linkBars = map(quality, 0, 100, 0, 5);
      xSemaphoreGive(pktMutex);
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// Task on core 1: update OLED, tombol, dan encoder
// Task kecil pembaca tombol tiap 2 ms (core 1, prioritas di atas layar)
// SW dan BTN digabung karena di hardware keduanya terbaca bersamaan
void TaskKey(void *pvParameters) {
  (void) pvParameters;
  for (;;) {
    bool raw = (digitalRead(ENC_SW) == LOW) || (digitalRead(BTN) == LOW);
    keyPoll(raw, millis());
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

void TaskDisplay(void *pvParameters) {
  (void) pvParameters;

  for (;;) {
    uint32_t now = millis();

    // ===== BUZZER LOST SIGNAL =====
    if (linkLost) {
      if (now - lastBeepTime >= BEEP_INTERVAL) {
        lastBeepTime = now;
        sndPlay(SND_RXLOST);
      }
    }

    // ================= TOMBOL + ENCODER =================
    // Tombol dibaca di TaskKey. Di sini hanya kejadian KLIK/HOLD yang diproses.
    timerTick(now);
    webService();       // simpan pengaturan dari web ke EEPROM, terapkan tema dan kalibrasi (satu task = tanpa tabrakan I2C/EEPROM)

    // Mode update firmware: layar khusus, menu dilewati. HOLD membatalkan (restart), kecuali sedang menerima berkas.
    if (otaMode) {
      while (keyPending()) {
        int ev = keyPop();
        if (ev == KEY_HOLD && otaState != 1 && !otaRestartAt) otaRestartAt = millis() + 200;
      }
      drawOtaScreen();
      vTaskDelay(pdMS_TO_TICKS(40));
      continue;
    }
    navTick(now);

    static unsigned long lastLowBattBeep = 0;

    if (lowBattAlarm && millis() - lastLowBattBeep > 2000) {
      lastLowBattBeep = millis();
      sndPlay(SND_LOWBATT);
    }

    // Kontras hanya dikirim ke OLED kalau nilainya berubah
    static int lastContrastSent = -1;
    if (oledBrightness != lastContrastSent) {
      display.ssd1306_command(SSD1306_SETCONTRAST);
      display.ssd1306_command(oledBrightness);
      lastContrastSent = oledBrightness;
    }

    // ================= GAMBAR LAYAR =================
    if (currentMode == MODE_DASHBOARD) {
      drawDashboard();
    }
    else if (currentMode == MODE_LIST) {
      drawListMenu();
    }
    else if (currentMode == MODE_BRIGHTNESS) {
      drawBrightnessMenu();
    }
    else if (currentMode == MODE_CHANNEL) {
      drawChannelMenu();
    }
    else if (currentMode == MODE_REVERSE) {
      drawReverseMenu();
    }
    else if (currentMode == MODE_TRIM) {
      drawTrimMenu();
    }
    else if (currentMode == MODE_THEME) {
      drawThemeMenu();
    }
    else if (currentMode == MODE_CALIB) {
      drawCalibMenu();
    }
    else if (currentMode == MODE_CONN) {
      drawConnMenu();
    }
    else if (currentMode == MODE_SOUND) {
      drawSoundMenu();
    }
    else if (currentMode == MODE_PARAM) {
      drawParamMenu();
    }

    // Jeda antar frame minimal agar refresh secepat yang dimungkinkan bus I2C
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

// ================= SETUP =================
void setup() {
  setCpuFrequencyMhz(240);          // kecepatan CPU maksimum
  Serial.begin(115200);

  // =====================================================
  // MATIKAN WIFI & BLUETOOTH
  // Kurangi interferensi ke NRF24 (satu pita 2.4GHz) dan
  // hemat daya karena WiFi/BT ESP32 nggak dipakai sama sekali.
  // =====================================================
  WiFi.mode(WIFI_OFF);
  btStop();

  // ===== EEPROM INIT =====
  EEPROM.begin(EEPROM_SIZE);

  // LOAD KONEKTIVITAS (sumber input selalu REMOT saat dinyalakan)
  wifiOn = (EEPROM.read(EEPROM_ADDR_WIFI) == 1);
  btOn   = (EEPROM.read(EEPROM_ADDR_BT) == 1);
  if (wifiOn && btOn) { wifiOn = false; EEPROM.write(EEPROM_ADDR_WIFI, 0); EEPROM.commit(); }   // data lama: dua-duanya ON, pilih Bluetooth
  inputSrc = INPUT_REMOT;

  // Pengaman boot loop: kalau restart terakhir karena crash (panic atau watchdog), WiFi dan Bluetooth dimatikan
  // dan disimpan mati. Remot tetap bisa dipakai normal, dan pelakunya bisa dicari lewat Serial Monitor.
  {
    esp_reset_reason_t rr = esp_reset_reason();
    DBG("[BOOT] alasan reset %d\n", (int)rr);
    if (rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT || rr == ESP_RST_TASK_WDT || rr == ESP_RST_WDT) {
      DBG("[BOOT] restart karena crash, WiFi dan Bluetooth dimatikan\n");
      if (btOn || wifiOn) {
        btOn = false; wifiOn = false;
        EEPROM.write(EEPROM_ADDR_BT, 0);
        EEPROM.write(EEPROM_ADDR_WIFI, 0);
        EEPROM.commit();
      }
    }
  }

  // LOAD SUARA
  {
    uint8_t v = EEPROM.read(EEPROM_ADDR_NRFPWR);   nrfPower = (v <= 4) ? v : 3;              // kosong: MAX seperti sebelumnya
    v = EEPROM.read(EEPROM_ADDR_DASH);             dashStyle = (v <= 4) ? v : 0;
    v = EEPROM.read(EEPROM_ADDR_BUZTYPE);          buzPassive = (v <= 1) ? v : 1;            // kosong: pasif
    v = EEPROM.read(EEPROM_ADDR_BUZTONE);          buzTone = (v >= 10 && v <= 50) ? v : 27;  // kosong: 2.7 kHz
  }
  soundVol = EEPROM.read(EEPROM_ADDR_VOL);
  if (soundVol < 1 || soundVol > VOL_MAX) soundVol = VOL_MAX;      // kosong (0xFF) atau rusak: volume penuh
  {
    uint8_t m0 = EEPROM.read(EEPROM_ADDR_SNDMASK);
    uint8_t m1 = EEPROM.read(EEPROM_ADDR_SNDMASK2);
    if (m1 == 0xFF) soundMask = (uint16_t)(m0 == 0xFF ? SND_ALL_MASK : (m0 | 0x380));   // kosong atau data lama: suara baru aktif
    else if (m1 & 0x80) soundMask = (uint16_t)(m0 | ((m1 & 3) << 8));                    // format sekarang
    else soundMask = (uint16_t)(m0 | ((m1 & 1) << 8) | 0x200);                           // format versi sebelumnya
  }

  // LOAD BRIGHTNESS
  encoderPos = EEPROM.read(EEPROM_ADDR_BRIGHT);
  encoderPos = constrain(encoderPos, 0, BRIGHTNESS_MAX);

  // LOAD CHANNEL REVERSE
  for (int i = 0; i < 6; i++) {
    channelReverse[i] = EEPROM.read(EEPROM_ADDR_REVERSE + i);
  }

  for (int i = 0; i < 6; i++) {
    int val = EEPROM.read(EEPROM_ADDR_TRIM + i);

    if (val == 0xFF) val = 128;   // default tengah

    channelTrim[i] = val - 128;
  }

  // LOAD THEME (0 = LIGHT, 1 = DARK)
  darkMode = EEPROM.read(EEPROM_ADDR_OUTPUT);

  if (darkMode > 1)
      darkMode = true;   // default DARK kalau EEPROM masih kosong/rusak (0xFF)

  themeIndex = darkMode ? 1 : 0;

  // LOAD KALIBRASI STICK
  loadCalibration();


  pinMode(SW1, INPUT_PULLUP);
  pinMode(SW2, INPUT_PULLUP);
  pinMode(SW3, INPUT);
  pinMode(BTN, INPUT_PULLUP);
  pinMode(BUZZER, OUTPUT);
  digitalWrite(BUZZER, LOW);
  // ================= EEPROM RESET =================
  // Tahan BTN saat boot untuk reset EEPROM

  if (digitalRead(BTN) == LOW) {

    // ===== RESET EEPROM MANUAL =====

    // BRIGHTNESS DEFAULT = 5
    EEPROM.write(EEPROM_ADDR_BRIGHT, 5);

    // REVERSE DEFAULT = NORMAL
    for (int i = 0; i < 6; i++) {
      EEPROM.write(EEPROM_ADDR_REVERSE + i, 0);
    }

    // TRIM DEFAULT = 0
    // disimpan sebagai 128 karena saat load dikurangi 128
    for (int i = 0; i < 6; i++) {
      EEPROM.write(EEPROM_ADDR_TRIM + i, 128);
    }

    // THEME DEFAULT = DARK
    EEPROM.write(EEPROM_ADDR_OUTPUT, 1);

    // KALIBRASI STICK DIHAPUS (kembali ke bawaan)
    EEPROM.write(EEPROM_ADDR_CALMAGIC, 0);
    EEPROM.write(EEPROM_ADDR_WIFI, 0);
    EEPROM.write(EEPROM_ADDR_BT, 0);
    EEPROM.write(EEPROM_ADDR_VOL, VOL_MAX);
    EEPROM.write(EEPROM_ADDR_NRFPWR, 0xFF);
    EEPROM.write(EEPROM_ADDR_DASH, 0xFF);
    EEPROM.write(EEPROM_ADDR_BUZTYPE, 0xFF);
    EEPROM.write(EEPROM_ADDR_BUZTONE, 0xFF);
    soundMask = SND_ALL_MASK;
    soundMaskSave();
    EEPROM.write(EEPROM_ADDR_MAGIC, 0xFF);     // model, rate, endpoint dan lainnya dibuat ulang
    EEPROM.write(EEPROM_ADDR_MNMAGIC, 0xFF);   // jumlah dan nama model kembali ke bawaan

    EEPROM.commit();

    // ===== RESET VARIABLE RUNTIME =====
    encoderPos = 5;

    for (int i = 0; i < 6; i++) {
      channelReverse[i] = false;
      channelTrim[i] = 0;
    }

    darkMode = true;
    themeIndex = 1;

    buzzOn(VOL_MAX);
    delay(600);
    buzzOff();

    display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
    display.clearDisplay();

    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(5,20);
    display.print("EEPROM");

    display.setCursor(18,42);
    display.print("RESET");

    display.display();

    delay(2000);

    ESP.restart();
  }

  modelInitStorage();     // model aktif, rate, endpoint, timer, batas baterai

  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);

  // Ambil state awal encoder, lalu pasang interrupt di pin A dan B
  encLast = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
  encSub = 0;
  encLastMicros = micros();
  attachInterrupt(digitalPinToInterrupt(ENC_A), handleEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B), handleEncoder, CHANGE);


  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.display();

  // ===== TERAPKAN THEME TERSIMPAN =====
  // DARK = normal (invert off), LIGHT = inverted (bg putih, instrumen hitam)
  display.invertDisplay(!darkMode);

  bootAnimation();

  radio.begin();
  radio.setPALevel(nrfPaLevel(nrfPower));
  radio.setDataRate(RF24_250KBPS);
  radio.setChannel(110);
  radio.setCRCLength(RF24_CRC_16);
  radio.setAutoAck(true);
  radio.setRetries(15, 15);
  radio.disableDynamicPayloads();
  radio.setPayloadSize(32);
  radio.openWritingPipe(address);
  radio.openReadingPipe(1, address);
  radio.stopListening();

  brightnessLevel = encoderPos;        // level awal dari EEPROM
  lastBrightnessSaved = brightnessLevel;
  oledBrightness = map(encoderPos, 0, BRIGHTNESS_MAX, 0, 255);

  display.ssd1306_command(SSD1306_SETCONTRAST);
  display.ssd1306_command(oledBrightness);

  pktMutex = xSemaphoreCreateMutex();
  if (pktMutex == NULL) {
    Serial.println("Failed to create pkt mutex!");
    while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
  }

  if (xSemaphoreTake(pktMutex, (TickType_t)10) == pdTRUE) {
    for (int i=0;i<6;i++) pkt.ch[i] = 0;
    pkt.ch7 = 2048;
    pkt.ch8 = 2048;
    pkt.switches = 0;
    pkt.mode = currentMode;
    pkt.outputMode = darkMode;
    linkBars = 5;
    xSemaphoreGive(pktMutex);
  }

  sndQ = xQueueCreate(10, sizeof(uint32_t));
  xTaskCreatePinnedToCore(TaskSound, "TaskSound", 3072, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(TaskInputs, "TaskInputs", 4096, NULL, 3, NULL, 0);
  xTaskCreatePinnedToCore(TaskDisplay, "TaskDisplay", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(TaskKey, "TaskKey", 2048, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(TaskConn, "TaskConn", 8192, NULL, 1, NULL, 1);
}

// ================= LOOP =================
void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
