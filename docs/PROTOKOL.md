# Protokol EDGE TX: Bluetooth (HP ke remot) dan paket NRF24

## Bluetooth Low Energy (BLE), tumpukan NimBLE
- Nama perangkat: `ESP32 TX`
- Service: Nordic UART Service (NUS) `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- RX (HP menulis ke remot, write / write without response): `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
- TX (remot mengirim ke HP, notify): `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`
- MTU diminta 185 (aplikasi memanggil requestMtu(185)). Kalau HP tetap di MTU 20, format teks boleh terpotong ke beberapa paket, karena remot menggabungkannya sampai ketemu akhir baris.
- Bluetooth dan WiFi saling mengunci: kalau Bluetooth ON, WiFi tidak bisa dinyalakan (dan sebaliknya). Pengecualian: mode update firmware memakai WiFi sementara dan mematikan Bluetooth.
- Pemantauan dan semua pengaturan remot (dulu lewat web WiFi) sekarang lewat Bluetooth memakai perintah teks di bawah. Web WiFi tetap ada kalau WiFi dinyalakan sendiri.

### Jenis channel
| Channel | Mode REMOT | Mode HP |
|---|---|---|
| CH1 sampai CH4 | joystick remot (0..4095) | dari HP (0..4095) |
| CH5, CH6 | switch fisik SW1, SW2 (0 atau 4095) | dari HP, tapi dipaksa 0 atau 4095 (nilai ≥ 2048 menjadi 4095, selain itu 0) |
| CH7, CH8 | diam di 2048 (tidak ada input fisik) | dari HP, tiga nilai saja: 0, 2048, 4095 |

### Format perintah channel (HP ke remot)
1. **Teks**: `c1,c2,c3,c4,c5,c6,c7,c8\n`
   - Akhir baris boleh `\n`, `\r\n`, atau `;`
   - Boleh kirim kurang dari 8 nilai, channel yang tidak dikirim tetap pada nilai terakhir
   - Nilai di atas 4095 dibatasi ke 4095. Baris yang berisi huruf diabaikan.
2. **Biner (18 byte)**: `0xA5`, lalu 8 × uint16 little endian (CH1 sampai CH8), lalu 1 byte checksum = XOR byte ke-1 sampai ke-16. Checksum salah berarti paket ditolak.

### Status dari remot (notify tiap 500 ms)
`V7.42,H1,L5,K1\n`
- V = tegangan baterai remot
- H = sumber input (0 = REMOT, 1 = HP)
- L = bar link NRF24 (0 sampai 5)
- K = data HP diterima (1) atau FAILSAFE (0)

### Aturan keamanan
- Mode HP: joystick dan switch remot diabaikan. Kalau 500 ms tidak ada data dari HP, semua channel memakai nilai FAILSAFE `{2048, 2048, 0, 2048, 0, 0, 2048, 2048}` (CH3 = throttle minimum, sebelum reverse dan trim).
- Mode HP hanya bisa dipilih kalau Bluetooth ON dan ada HP yang tersambung. Bluetooth putus atau dimatikan berarti input otomatis kembali ke REMOT.
- Saat remot dinyalakan, sumber input selalu REMOT (WiFi dan Bluetooth mengikuti pilihan terakhir).
- Bit `switches` di paket NRF mengikuti CH5 dan CH6.
- Reverse dan trim diterapkan ke CH1 sampai CH6, baik dari remot maupun dari HP. CH7 dan CH8 dikirim apa adanya.

## Paket NRF24 ke RX (TXPacket, payload tetap 32 byte)
| Byte | Isi |
|---|---|
| 0–11 | `ch[6]`, uint16 little endian, CH1 sampai CH6 |
| 12 | `switches` (bit0 = CH5, bit1 = CH6) |
| 13 | `mode` |
| 14 | `outputMode` (tema) |
| 15 | padding |
| 16–17 | `ch7` (uint16) |
| 18–19 | `ch8` (uint16) |

RX lama yang membaca 16 byte pertama tetap berjalan. Untuk memakai CH7 dan CH8, struct di RX harus ditambah `uint16_t ch7; uint16_t ch8;` di bagian paling belakang, dengan urutan yang sama.

## WiFi monitor (captive portal)
- Hotspot `ESP32 TX`, password `Farhan2005`, kanal WiFi 11
- Remot menjalankan DNS yang mengarahkan semua nama domain ke 192.168.4.1. Permintaan yang bukan ke 192.168.4.1 dijawab redirect 302 ke halaman monitor.
- Akibatnya HP (Android, iPhone, Windows) menganggap jaringan ini butuh login dan membuka halaman monitor sendiri. Android menampilkan notifikasi "Masuk ke jaringan", tinggal diketuk. Tidak perlu mengetik IP.
- Kalau popup tidak muncul, buka `http://192.168.4.1` manual. Halaman membaca data JSON di `/data` setiap 150 ms.
- Jawab "Tetap terhubung" kalau Android bertanya soal jaringan tanpa internet.
- Sensor baterai di GPIO15 (ADC2) tidak bisa dibaca saat WiFi aktif, jadi nilainya ditahan. Solusi hardware: pindahkan pembagi tegangan ke GPIO36 atau GPIO39 (ADC1), lalu ubah `#define SW3`.

## Perintah teks dari aplikasi (satu baris berakhiran `\n`, diawali huruf)
| Perintah | Fungsi | Jawaban |
|---|---|---|
| `G` | minta JSON status lengkap (isi sama dengan `/data`, plus `fw` dan `ota`) | `J{...}\n` |
| `S,kunci,nilai` | ubah pengaturan, kunci sama dengan `/set` (lihat bawah) | `A,S:kunci,kode\n` (200, 400, 409) |
| `BK` | minta cadangan | `BK:EDGETX1:<600 hex>\n` |
| `RS,i,hex` | potongan pemulihan ke-i (0..5), 50 byte = 100 karakter hex | `A,RS,kode\n` |
| `RC` | pasang hasil pemulihan lalu restart | `A,RC,kode\n` |
| `OTA` | masuk mode update firmware (Bluetooth mati, WiFi menyala) | `A,OTA,200\n` |
- Antrean perintah di remot hanya 5 baris, jadi aplikasi mengirim satu baris tiap 45 ms dan potongan RS satu per satu setelah balasan A,RS,200.
- Remot memecah balasan panjang jadi paket (MTU - 3, maks 180 byte). Aplikasi menyusunnya sampai ketemu `\n` (buffer 6000 karakter).
- Status singkat `V..,H..,L..,K..` tetap dikirim tiap 500 ms.

## Update firmware (OTA)
- Syarat: partisi punya dua slot aplikasi. `platformio.ini` memakai `board_build.partitions = min_spiffs.csv` (slot 1,9 MB). Pemasangan pertama yang membawa fitur ini harus lewat kabel USB. Arduino IDE: pilih Minimal SPIFFS (1.9MB APP with OTA).
- File yang diunggah harus `firmware.bin` hasil build (`.pio/build/esp32dev/firmware.bin`), byte pertama 0xE9. Bukan kode C++ dan bukan hex.
- Cara masuk: menu remot UPDATE FIRMWARE, atau perintah `OTA` dari aplikasi. OLED menampilkan SSID, password, alamat, persen unggah, dan hasil. Tahan tombol untuk batal (tidak bisa saat sedang menulis).
- WiFi `ESP32 TX` / `Farhan2005`, halaman unggah `http://192.168.4.1/fw` (juga halaman utama saat mode update). Unggah: `POST /update?size=N` multipart, satu berkas. Balasan 200 `OK` lalu remot restart 1,5 detik kemudian; gagal membalas 500 dengan pesan.
- **Update lewat Bluetooth (tanpa WiFi)**: `FWB,ukuran` (100 KB sampai 1.966.080 byte) dijawab `A,FWB,200` (400 ukuran salah, 409 sedang mode update, 500 `Update.begin` gagal). Sesudah 200, remot menganggap SEMUA byte yang ditulis adalah isi berkas (paket kendali, perintah teks, dan polling harus dihentikan). Data dikirim write without response seukuran MTU-3. Remot menaruh data di ring buffer 16 KB lalu TaskConn menulisnya ke flash. Remot mengabari `F,byteTertulis` tiap 4 KB. Aplikasi menjaga jendela maksimal 8 KB yang belum diakui. Selesai: `F,DONE` lalu restart 1,5 detik kemudian. Gagal: `A,F,kode` (408 tidak ada data 8 detik, 415 byte pertama bukan 0xE9, 422 berkas tidak valid, 500 gagal menulis flash). Bluetooth putus di tengah jalan membatalkan update dan firmware lama tetap utuh. OLED menampilkan "Lewat Bluetooth" dan persen.
- Cara lewat WiFi (menu remot UPDATE FIRMWARE atau perintah `OTA`) tetap ada untuk browser.

## Aplikasi Android (proyek `EdgeTX_Remote`)
- Java murni tanpa library tambahan, minSdk 31 (Android 12+). Build lewat GitHub Actions (`buat_apk.bat`), hasil `ESP32-TX.apk`. Versi 2.0.
- Lima tab: Kendali, Monitor, Model, Remot, Update. Semua fitur web ada di aplikasi: pantauan, reverse, trim, rate, expo, endpoint, mixing, failsafe, timer, model, kecerahan, tema, suara, batas baterai, kalibrasi, cadangan dan pulihkan, update firmware.
- Kendali: stik kiri = CH4 (X) dan CH3 (Y, throttle bertahan di tempat), stik kanan = CH1 (X) dan CH2 (Y). Tombol Input REMOT/HP ada di tab ini.
- CH5 dan CH6 = tombol ON/OFF (0 atau 4095). CH7 dan CH8 = saklar 3 posisi: 0, 2048 (tengah), atau 4095.
- Mengirim paket biner 18 byte 40 kali per detik ke karakteristik RX. Membaca status notify untuk menampilkan baterai, link, dan mode HP atau FAILSAFE.
- Saat aplikasi ditinggal atau koneksi putus, semua kendali kembali aman (throttle 0) dan remot masuk FAILSAFE sendiri setelah 500 ms.
- Syarat di remot: Bluetooth ON. Input HP dipilih dari aplikasi (tab Kendali) atau dari menu Konektivitas.

## Menu SUARA (item ke-8 di daftar menu)
- Item: VOLUME (1 sampai 10), lalu 9 suara yang bisa ON/OFF: Disconnect, Booting, Pindah menu, Scroll, Klik, Terhubung HP (Bluetooth), HP masuk WiFi, Alarm RX putus, Alarm baterai habis.
- EEPROM: alamat 44 = volume (default 10), alamat 45 = mask suara bit 0..7, alamat 46 = bit 8 (default semua aktif, 0x1FF). EEPROM_SIZE 47.
- Volume buzzer aktif memakai PWM LEDC lambat (`VOL_PWM_HZ` 1000 Hz), tabel `VOL_DUTY[11]` bisa disetel. Alarm RX putus dan baterai habis ikut volume dan bisa dimatikan.
- Suara diputar lewat antrean (`sndQ`) dan TaskSound, jadi tidak memblokir tampilan.

## Pengaturan lewat web (tab Pengaturan)
- `POST /set` dengan field `k` dan `v`. Nilai langsung dipakai lalu disimpan ke EEPROM oleh `webService()`.
- Kunci: bri 0..10, thm 0/1, rev0..rev5 0/1, trim0..trim5 -100..100, wifi 0/1, bt 0/1, src 0/1 (409 jika HP tanpa Bluetooth), svol 1..10, snd0..snd8 0/1, test 0..8, cal 1 (mulai) 2 (lanjut/simpan) 3 (batal). Kunci lain dibalas 400.
- `/data` menambah: wf, bts, svol, smask, cb{on,st,mn[4],mx[4]}.

## Model, rate, expo, endpoint, mixing, failsafe, timer, alarm baterai
- Daftar menu sekarang 19 item (firmware 3.3). Tujuh item baru memakai satu mesin menu parameter (`MODE_PARAM`): MODEL, RATE DAN EXPO, ENDPOINT, FAILSAFE HP, MIXING, TIMER, ALARM BATERAI. Putar untuk pilih, klik untuk edit, klik lagi atau tahan untuk simpan.
- Urutan pengolahan tiap channel (`applyChannels`): stik, rate dan expo (CH1..CH4), mixing, reverse, trim, endpoint. Nilai bawaan memberi hasil sama persis dengan rumus lama.
- Rate 30..100 persen, expo 0..100 persen, endpoint bawah dan atas 20..120 persen per channel, bawaan 100.
- Mixing: tipe OFF, ELEVON, atau V-TAIL, dengan dua channel yang bisa dipilih (CH1..CH6) dan bobot 10..100 persen (bawaan 50). ELEVON: A = aileron, B = elevator, A' = ele + ail, B' = ele - ail. V-TAIL: A = elevator, B = rudder, A' = ele + rud, B' = ele - rud. Mengganti tipe mengembalikan pasangan ke bawaan (ELEVON CH1 dan CH2, V-TAIL CH2 dan CH4). Memilih channel yang sama dengan pasangannya menukar pasangannya ke nilai lama. Disimpan per model (byte 37 dan 38 di slot, alamat kerja 83 dan 84).
- Failsafe HP: nilai tiap channel (persen) yang dikirim ke RX saat data HP putus. Hanya untuk mode HP. Kalau remot sendiri mati atau link NRF putus, failsafe dijalankan RX, bukan remot.
- 1 sampai 10 model (bawaan 5: PLANE, BOAT, ROVER, UNIVERSAL, MODEL 5), bisa ditambah, diganti nama, dan dihapus dari aplikasi. Tiap model menyimpan reverse, trim, rate, expo, endpoint, failsafe, mixing. Alamat 100 sampai 499 adalah slot (40 byte per model), alamat 500 sampai 619 nama model (12 byte per model, maksimum 9 huruf besar, angka, spasi, titik, minus, garis bawah), alamat 49 jumlah model, alamat 88 penanda 0xB5. Salinan kerja model aktif ada di alamat lama (reverse 1, trim 7) dan 50 sampai 79. Pindah model menyimpan salinan kerja ke slot lama lalu memuat slot baru. Data lama otomatis menjadi model 1.
- Umum (tidak ikut model): timer (alamat 80, 81) dan batas alarm baterai (alamat 82, per 0,1 V, bawaan 6,4 V). Penanda format di alamat 47 (0xA7), model aktif di 48. EEPROM_SIZE 650 (versi lama 300, otomatis diperluas tanpa menghapus data).
- Timer: stopwatch, atau hitung mundur 1..99 menit yang berbunyi sekali saat habis (suara TIMER, bit 9 mask suara). Pemicu bisa dipilih: MANUAL (tombol JALAN) atau salah satu CH1..CH8 (nilai yang dikirim ke RX), dengan batas 5..95 persen dan arah DI ATAS atau DI BAWAH batas (bawaan 10 persen, di atas). Pengaturan timer berlaku untuk semua model (alamat 80, 85, 86, 87). Pengaturan "otomatis" versi sebelumnya (alamat 81) dimigrasikan menjadi pemicu CH3. Tampil di dashboard (mm:ss) bersama nomor model dan tegangan baterai RX.
- Tegangan baterai RX dibaca dari paket telemetri yang sudah ada (`telemetry.voltage`), dianggap valid kalau diterima dalam 2 detik terakhir.
- Mask suara: alamat 45 = bit 0..7, alamat 46 = 0x80 + bit 8..9.

### Kunci /set tambahan
model 0..(jumlah model - 1), dr0..dr3 (30..100), ex0..ex3 (0..100), lo0..lo5 dan hi0..hi5 (20..120), fs0..fs7 (0..100), mixt 0..2 (mengatur pasangan bawaan), mixa 1..6, mixb 1..6, mixw 10..100, tmin 0..99, ttrig 0..8 (0 = manual), tthr 5..95, tdir 0/1, trun 0/1, treset 1, blow 50..84, snd0..snd9, test 0..9.
`/data` menambah: mdl, dr, ex, lo, hi, fs, mxt, mxw, mxa, mxb, tmin, ttrig, tthr, tdir, trun, tel (detik berjalan), blow, rxv (tegangan RX, -1 kalau belum ada).

### Cadangan
- `GET /backup` mengunduh teks `EDGETX1:` diikuti isi EEPROM (300 byte) dalam heksadesimal.
- `POST /restore` dengan field `d` berisi teks itu. Diperiksa (awalan, panjang, heksa, penanda 0xA7), dipasang lalu remot restart.

## Daftar model dari aplikasi (firmware 3.2)
- JSON `G` memuat `"mc"` (jumlah model) dan `"mn"` (larik nama), selain `"mdl"` (model aktif, indeks dari 0).
- `MA,nama` tambah model dengan pengaturan bawaan. Jawaban `A,MA:indeks,200`, `A,MA,409` (penuh, maksimum 10), `A,MA,400` (nama kosong atau salah).
- `MN,indeks,nama` ganti nama. Jawaban `A,MN,200` atau `A,MN,400`.
- `MX,indeks` hapus model. Model di atasnya bergeser turun. Model terakhir tidak bisa dihapus (`A,MX,409`).
- Pindah model tetap lewat `S,model,indeks`.
- Menu MODEL di remot berupa daftar nama. Klik nama, putar ke PAKAI, klik lagi untuk memakai model itu.
- Cadangan memuat 650 byte (13 potongan RS). Cadangan lama 300 byte (6 potongan) tetap bisa dipulihkan, sisanya otomatis kosong lalu dibuat ulang dengan lima model bawaan.

## Throttle stik HP (hanya di aplikasi)
Tab Remot, kartu Throttle stik HP. Tanpa centang throttle bertahan di posisi terakhir. Centang tengah: kembali ke 2048 saat dilepas. Centang bawah: kembali ke 0 saat dilepas. Disimpan di HP, tidak dikirim ke remot.

## Firmware 3.3: mix bebas, daya NRF, buzzer pasif, grafik
- Menu baru di remot: MIX BEBAS (4 mix, tiap mix: DARI OFF/CH1..CH8, KE CH1..CH8, NILAI -100..+100 persen langkah 5), DAYA NRF (MIN -18, LOW -12, HIGH -6, MAX 0 dBm, dengan batang dan perkiraan jangkauan), NADA BUZZER (JENIS aktif/pasif, NADA 1.0..5.0 kHz, TES BUNYI). Halaman RATE DAN EXPO memakai grafik kurva channel terpilih di sisi kanan yang ikut berubah saat nilai diedit.
- Mix bebas dihitung di akhir (`freeMixApply`), sesudah endpoint, pada 8 channel akhir. Tujuan += (sumber - 2048) x persen / 100, lalu dibatasi 0..4095. Semua sumber dibaca dari nilai sebelum ada mix bebas yang berlaku, jadi urutan mix tidak berpengaruh. CH7 dan CH8 ikut (normalnya 3 posisi, bergeser halus kalau jadi tujuan).
- Mix bebas disimpan per model: 3 byte per mix (sumber, tujuan, nilai + 100), 12 byte per model. Slot di alamat 620 sampai 739 (10 model), salinan kerja di 740 sampai 751. Model baru mulai dengan mix mati (0xFF dibaca sebagai mati).
- Pengaturan umum baru: alamat 89 daya NRF (0..3, 0xFF = MAX), alamat 90 jenis buzzer (0 aktif, 1 pasif, 0xFF = pasif), alamat 91 nada (satuan 100 Hz, 10..50, 0xFF = 27). EEPROM_SIZE 800 (kelipatan 50, 16 potongan RS). Cadangan 650 dan 300 byte tetap diterima.
- Daya NRF hanya mempengaruhi arah remot ke RX. Telemetri balik dari RX memakai daya RX sendiri. RX tidak perlu diubah. Nilai dipasang oleh TaskInputs (pemilik radio) di antara dua pengiriman, logika NRF lain tidak berubah. Dimuat dari EEPROM sebelum `radio.setPALevel` di setup.
- Buzzer pasif: nada = PWM kotak pada frekuensi NADA, volume = lebar pulsa (duty 8 bit, 128 = 50% = paling keras). Tabel `VOL_DUTY_PASSIVE` {3, 6, 10, 16, 25, 38, 56, 80, 108, 128} naik rata di telinga. Resonansi umum buzzer pasif 2.5 sampai 4 kHz, bawaan 2.7 kHz. Jingle booting dimainkan sebagai melodi yang ikut bergeser mengikuti NADA. Jenis aktif memakai jalur lama (ON/OFF dan PWM lambat).
- Animasi booting: gambar pemancar RC (badan naik, antena memanjang, stik bergerak, gelombang radio), judul ESP32 TX, bar kemajuan. Berjalan 2 detik bersama suara.

### Kunci tambahan (/set dan `S,kunci,nilai`)
fms0..fms3 (0..8), fmd0..fmd3 (1..8), fmw0..fmw3 (-100..100), nrf 0..3, btype 0/1, btone 10..50.
`/data` dan JSON `G` menambah: fms[4], fmd[4], fmw[4], nrf, btype, btone. fw = "3.3".

### Aplikasi 2.3
- Tab Model: kartu Rate, expo, dan endpoint dengan grafik kurva (rumus sama dengan firmware: rate, expo, reverse, trim, endpoint) dan titik posisi stik sekarang. Pilih CH1..CH6. Kartu Mix bebas dengan 4 mix. Mixing ELEVON dan V-TAIL tetap ada.
- Tab Remot: kartu Buzzer (jenis, nada, tes) dan Daya pancar NRF (empat tingkat dengan gambar).
- Ikon aplikasi baru (pemancar RC).

## Firmware 3.4: NRF bisa dimatikan
- Daya NRF punya tingkat ke-5: OFF (nilai 4). Kunci `nrf` 0..4, menu DAYA NRF memuat OFF MATI. Saat OFF, TaskInputs menghentikan pengiriman (`powerDown`), tidak ada paket keluar, alarm NO LINK dan RX hilang tidak berbunyi, dashboard menampilkan NRF OFF. Saat dihidupkan lagi radio `powerUp` lalu PA level dipasang ulang.
- RX akan masuk failsafe selama OFF, jangan dipakai saat terbang. (Sejak 4.0 OFF ikut disimpan, lihat di bawah.)
- Aplikasi 2.4: lima pilihan OFF, MIN, LOW, HIGH, MAX. OFF meminta konfirmasi.


## Firmware 4.0
- Daya NRF termasuk OFF disimpan di EEPROM alamat 89 (nilai 0..4). Sesudah restart remot memakai pengaturan terakhir. Perbaikan: `cfgSaveWorking()` kini memanggil `miscSave()`, sehingga perubahan nrf, btype, btone dan dash dari aplikasi ikut tersimpan (sebelumnya hanya perubahan lewat menu remot yang tersimpan).
- Lima gaya dashboard: 0 Klasik, 1 Bar 8CH, 2 Timer, 3 Vertikal, 4 Gimbal. Disimpan di EEPROM alamat 92 (0xFF = 0). Menu TAMPILAN DASHBOARD menampilkan pratinjau layar penuh yang berganti saat encoder diputar. Kunci `dash` 0..4, JSON `dsh`.
- Analisis sinyal 2.4 GHz (menu ANALISIS SINYAL): NRF24 membaca carrier detect (`testRPD`, sinyal sekitar -64 dBm ke atas) di 126 kanal. Tampilan SPEKTRUM (WiFi 1, 6, 11, penanda BLE, posisi TX kanal 110) dan KANAL WIFI (kepadatan 13 kanal, terbaik dan terramai). Kunci `ana` 1 menyalakan, JSON memuat `sp` saat aktif.
- Performa: kompilasi `-O2`, CPU 240 MHz, prioritas TaskInputs 3 dan TaskDisplay 2, daya BLE +9 dBm. Ukuran firmware 1.414.800 byte, muat di slot OTA 1.966.080 byte.
- Aplikasi 3.0: kartu Tampilan dashboard (5 pilihan), teks NRF OFF diperbarui.

## Firmware 4.1: penganalisis sinyal khusus
- Saat halaman ANALISIS SINYAL terbuka (atau kunci `ana` 1 dari aplikasi), NRF24 dipakai penuh sebagai pemindai. TaskInputs berhenti mengirim paket ke RX (sambungan diputus sementara, tanpa alarm NO LINK), lalu menyapu 126 kanal terus menerus. Keluar dari halaman: radio kembali ke kanal 110 dan pengiriman lanjut. RX masuk failsafe selama analisis, jangan dibuka saat terbang.
- Tiga cuplikan `testRPD` per kanal, rata-rata bergerak naik cepat turun pelan, puncak ditahan lalu turun. Tampilan: kurva dirata-ratakan tiga kanal, tepi bersambung, isi berpola, bar WiFi bergerak halus.

## Firmware 4.2: perbaikan pratinjau gaya dashboard
- Bug: menu TAMPILAN DASHBOARD memulai mode edit otomatis (putar encoder langsung memilih gaya), tetapi layar pratinjau masih menggambar gaya lama (`dashStyle`) sampai tersimpan. Diperbaiki: pratinjau sekarang menggambar gaya yang sedang ditunjuk encoder (`paramEdit`) secara langsung, jadi berubah seketika saat diputar, tanpa perlu klik dulu.
- Klik pada menu ini membatalkan (kembali ke daftar menu tanpa menyimpan). Tahan tombol yang menyimpan gaya ke EEPROM alamat 92.


## Firmware 4.3: perluasan analisis sinyal
- Tampilan ketiga RIWAYAT: waterfall aktivitas 2,4 GHz, baris terbaru di atas, menyimpan sekitar 4 detik riwayat (22 baris, diperbarui tiap sapuan penuh selesai).
- Rekomendasi kanal paling bersih (`anaCleanest`) ditampilkan di SPEKTRUM dan RIWAYAT, dihitung dari rata-rata seluruh pita 2,4 GHz. Hanya saran, tidak mengubah kanal TX sendiri.
- Perbaikan kecil: penanda "halaman baru saja disentuh" (anaTouch) tidak lagi bisa bernilai 0, yang sebelumnya bisa membuat sesi analisis dianggap tidak aktif pada kondisi tertentu.

## Aplikasi 3.1: tema tampilan dan penyesuaian fitur remot 4.x
- Kartu baru "Tema tampilan aplikasi" di tab Remot (paling atas): 5 pilihan warna (Midnight Biru, Karbon Merah, Hijau Militer, Ungu Neon, Putih Bersih). Hanya mengubah tampilan di HP, tidak dikirim ke remot dan tidak memengaruhi layar OLED. Tersimpan di HP (SharedPreferences), dipilih lewat sentuh kartu warna, tampilan dibangun ulang otomatis.
- Stik virtual (StickView) kini ikut warna tema yang dipilih, bukan warna tetap.
- Kartu "Tampilan dashboard remot" (dari 3.0) tetap ada untuk memilih gaya layar OLED di remot.

## Firmware 4.4: pemicu reset timer, aplikasi 3.2
- Timer sekarang punya pemicu RESET sendiri (channel, 0 = manual seperti sebelumnya, 1..8 = CH1..CH8), terpisah dari pemicu JALAN tapi memakai BATAS (persen) yang sama. Boleh memakai channel yang sama dengan pemicu JALAN, misalnya saklar 3 posisi: satu arah menjalankan, arah lain mereset. Reset terjadi sekali setiap kali channel MASUK ke zona pemicunya (tepi), bukan selama berada di zona, supaya tidak reset berulang selama saklar dipegang.
- Menu remot TIMER sekarang 8 baris: Countdown, Pemicu, Batas, Jalan bila, Jalan, Reset Pemicu, Reset bila, Reset (tombol manual, tetap ada).
- Kunci baru `trtrig` (0..8) dan `trdir` (0/1), JSON `trtrig` dan `trdir`. Disimpan di EEPROM alamat 93 dan 94.
- Aplikasi 3.2: tab baru "Sinyal" meneruskan analisis sinyal dari remot ke HP (spektrum 126 kanal dan kepadatan 13 kanal WiFi, digambar ulang dengan gradien dan animasi halus). Tab ini mengirim `ana` otomatis selama terlihat dan mematikannya saat ditutup atau aplikasi ke latar belakang.
