# RC522 Tester - ESP32C3 Supermini

Alat test kartu RFID MIFARE Classic (kartu putih polos 1K) pakai ESP32C3 Supermini + RC522. Nyala langsung jadi hotspot, buka dari HP lewat browser, semua kontrol dari situ. Data kartu tersimpan disimpan ke flash (NVS) jadi gak hilang walau ESP dimatiin.

## Wiring

| RC522 | ESP32C3 Supermini |
|-------|---------|
| SDA/SS | GPIO10 |
| SCK   | GPIO20 |
| MOSI  | GPIO21 |
| MISO  | GPIO0 |
| RST   | GPIO7 |
| 3.3V  | 3.3V |
| GND   | GND |

Buzzer: GPIO4 ke buzzer aktif, satu kaki lagi ke GND.

## Cara Pakai

1. Nyalain ESP32S3.
2. Konek WiFi HP ke SSID `RC522-Tester`, password `12345678`.
3. Buka browser ke `192.168.4.1`.

Menu yang ada:

- **Baca UID** - baca UID kartu yang ditempel. Ada tombol Mode Otomatis, sekali aktif tinggal gonta-ganti kartu tanpa klik ulang.
- **Baca Semua Sektor** - dump isi semua blok pakai key default `FFFFFFFFFFFF`.
- **Tulis Blok** - tulis data hex 16 byte ke blok tertentu, bisa pilih Key A atau Key B.
- **Ganti Key Sektor** - ganti key A dan B satu sektor.
- **Simpan Kartu** - simpan UID kartu terakhir yang dibaca ke flash, dikasih nama.
- **Lihat Kartu Tersimpan** - lihat semua kartu yang udah disimpan.
- **Hapus Semua Kartu Tersimpan** - reset data tersimpan.
- **Test Kartu** - tempel kartu, kalau UID-nya cocok sama yang tersimpan, buzzer bunyi. Ada Mode Otomatis juga, tinggal gonta-ganti kartu terus dites tanpa klik ulang.
- **Set Durasi Buzzer** - atur berapa lama buzzer bunyi saat cocok.
- **Baca Dengan Key Custom** - dump semua sektor pakai key selain default.
- **Cek / Brute Key Sektor** - coba beberapa key umum (default, MAD, NDEF, dll) per sektor buat cari key aktif.
- **Access Bits Sektor** - set access bits (C1/C2/C3) manual per sektor.
- **Value Block** - jadikan blok sebagai value block, lalu increment/decrement/baca nilainya.
- **Backup & Clone Kartu** - backup seluruh dump kartu ke flash, lalu clone ke kartu magic (UID changeable) lain.
- **Tulis Block 0** - ganti UID kartu, khusus kartu magic (kartu putih polos biasanya support ini).
- **Format Kartu** - kembalikan semua sektor ke key dan access bits default pabrik.
- **NFC Otomatis (NDEF)** - format kartu jadi NDEF, pilihan tipe: Link/URL, Teks, Nomor Telepon, SMS, Email, WiFi, Kontak (vCard). Bisa tambah Android Application Record (AAR) buat buka app tertentu. Setelah ditulis, tap kartu pakai NFC HP (bukan lewat web tool ini) buat trigger aksinya.
- **Cek Modul RC522** - baca versi chip dan self test hardware.
- **Hapus Kartu Ini Saja** - hapus satu entri kartu tersimpan tanpa reset semua.

## Build

Compile otomatis lewat GitHub Actions pakai `arduino-cli`, gak pakai PlatformIO. Firmware `.bin` hasil compile bisa diambil di tab Actions > Artifacts setelah workflow selesai.

Buat compile manual di Arduino IDE:

1. Install board `esp32` by Espressif.
2. Install library `MFRC522` by GithubCommunity.
3. Board pilih ESP32C3 Dev Module.
4. Upload `RC522_Tester.ino`.

## Catatan

- Key default kartu polos baru: `FFFFFFFFFFFF`.
- Jangan tulis ke block 0 (data manufacturer) dan block trailer (block 3, 7, 11, dst) lewat menu tulis blok biasa, itu udah dicegah otomatis.
- Ganti key sektor bikin permanen, kalau lupa key baru kartu sektor itu gak bisa diakses lagi.
- Fitur NDEF dan Format Kartu sekarang otomatis coba beberapa key umum (default, MAD, NDEF key), jadi tetap bisa nulis ulang walau kartu udah pernah diformat NDEF sebelumnya.
