# Dashboard Monitoring Extruder (ESP32-S3)

Dashboard web untuk memantau **suhu barrel**, **RPM extruder**, dan **RPM puller** dari ESP32-S3 N16R8.
Berupa satu file `index.html` (tanpa build), bisa dihosting gratis di GitHub Pages.

## Isi repo

| File | Fungsi |
|------|--------|
| `index.html` | Dashboard (dibuka di browser) |
| `extruder_esp32s3.ino` | Firmware ESP32-S3 (Arduino IDE) |

## Cara kerja

```
Sensor -> ESP32-S3 --WiFi--> broker MQTT (internet) --WebSocket--> Dashboard (GitHub Pages)
```

Format data (1 baris JSON per detik):

```json
{"temp":187.5,"rpm_ext":45.2,"rpm_pull":14.1}
```

## Mode sumber data (Pengaturan di dashboard)

| Mode | Kegunaan |
|------|----------|
| Demo | Data simulasi, untuk lihat tampilan tanpa ESP32 |
| MQTT | **Dipakai untuk GitHub Pages**, nirkabel lewat WiFi |
| USB Serial | Kabel USB ke komputer (Chrome/Edge desktop) |
| HTTP | Langsung ke IP ESP32, hanya jika halaman dibuka secara lokal (bukan https) |

## Setup

1. **Hosting dashboard**: Settings -> Pages -> Branch `main` / `(root)` -> Save.
   Alamat: `https://<username>.github.io/<nama-repo>/`
2. **Firmware**: buka `extruder_esp32s3.ino` di Arduino IDE.
   - Install board *esp32 by Espressif* dan library *PubSubClient*.
   - Board: ESP32S3 Dev Module, PSRAM: OPI PSRAM, Flash: 16MB.
   - Isi `WIFI_SSID`, `WIFI_PASS` (WiFi 2.4 GHz), dan ganti `MQTT_TOPIC` dengan nama unik.
3. **Dashboard**: Pengaturan -> Sumber data: MQTT -> isi Topic yang **sama persis** dengan firmware -> Simpan.

## I/O mapping (perkiraan, belum final)

| Fungsi | GPIO |
|--------|------|
| MAX6675 SCK / CS / SO | 12 / 10 / 13 |
| RPM extruder | 4 |
| RPM puller | 5 |

Nilai `PPR_EXT` dan `PPR_PULL` di firmware = jumlah pulsa per putaran poros, sesuaikan dengan sensor.

## Catatan keamanan

Broker MQTT publik bersifat terbuka: siapa pun yang tahu topic bisa membaca datanya.
Gunakan topic yang sulit ditebak dan hanya untuk monitoring (jangan untuk mengontrol mesin).
