# Sistem Parkir Otomatis Berbasis ESP-32

Sebuah prototype sistem parkir pintar yang menggunakan ESP-32 untuk mengelola akses masuk kendaraan menggunakan teknologi RFID, sensor ultrasonik, dan palang pintu otomatis.

## Fitur Utama
* Akses Gerbang via RFID: Pengguna melakukan tap kartu (Tag/Kartu RFID) pada reader untuk membuka palang pintu.
* Tampilan Status Interaktif: Layar LCD I2C menampilkan status akses (diterima/ditolak), pesan sapaan, dan informasi ketersediaan slot parkir secara real-time.
* Palang Pintu Otomatis: Menggunakan motor servo yang akan membuka gerbang secara otomatis jika akses RFID valid, dan menutup kembali setelah kendaraan masuk.

## Cara Kerja Sistem
1.  Kondisi Awal (Standby): Palang pintu tertutup.
2.  Validasi Akses: Pengemudi menempelkan kartu pada RFID Reader. Mikrokontroler akan mengecek apakah UID kartu tersebut memiliki izin.
3.  Gerbang Terbuka: Jika kartu valid, Arduino menggerakkan motor servo untuk mengangkat palang pintu. LCD menampilkan pesan "Akses Diterima".
4.  Penutupan Gerbang: Setelah mobil melewati gerbang (dideteksi oleh sensor ultrasonik) dengan jeda waktu tertentu, servo akan menutup palang pintu secara otomatis.