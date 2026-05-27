## Project 1 Sistem Mikrokontroler 
###### Simulate this project on https://wokwi.com → https://wokwi.com/projects/462262710247219201
---
#### Anggota:
- Iven RivaL Pangestu (H1H024013)   - [Github](https://github.com/Ivenrp)
- Apriyudha (H1H024010) - [Github](https://github.com/avriyyy)
- Mohammad Zulfan Ramadhan (H1H024008) - [Github](https://github.com/mzulfanr13-code) 
- Maharani Tri Wahyuningrum (H1H024012)
- Febrian Theopilus Purba	(H1H0240)


---

<div align="center">
  <h3>Alur Kerja Rangkaian (Workflow)</h3>
</div>

#### 1. Fase Startup (Inisialisasi)
Saat sistem pertama kali dinyalakan:
* Semua indikator LED dalam keadaan **mati**.
* Palang parkir otomatis berada dalam posisi **tertutup**.
* Layar LCD menyala dan menampilkan pesan: `"Sistem Parkir Siap Beroperasi"`.

#### 2. Fase Deteksi Kendaraan
Sistem terus memantau area depan gerbang menggunakan sensor Ultrasonik:
* Jika terdeteksi ada kendaraan mendekat pada jarak **< 120 cm**, sistem bersiap.
* **LED Kuning menyala** sebagai indikator *standby*, dan layar LCD menampilkan instruksi: `"Silahkan Scan"`.

#### 3. Fase Verifikasi Akses (Scan RFID)
Saat pengendara melakukan tap kartu pada *reader*, sistem akan memberikan respons berdasarkan tiga kondisi:
* **Kartu Valid & Slot Tersedia:** **LED Hijau berkedip**, *buzzer* berbunyi 1 kali, dan palang pintu **terbuka** otomatis.
* **Kartu Invalid (Tidak Terdaftar):** **LED Merah menyala**, *buzzer* berbunyi cepat secara berulang, dan akses **ditolak** (palang tetap tertutup).
* **Slot Parkir Penuh:** Meskipun kartu terdaftar, jika kapasitas sudah penuh (3 kendaraan), **LED Merah menyala**, *buzzer* berbunyi 3 kali, dan akses **ditolak**.

#### 4. Fase Kendaraan Masuk & Penutupan Otomatis
Setelah palang terbuka (akses diterima):
* Sensor Ultrasonik memantau pergerakan kendaraan yang melintasi palang.
* Jika kendaraan berada tepat di bawah palang (jarak **≤ 3 cm**), sistem akan memulai **hitung mundur 5 detik**.
* Setelah jeda 5 detik selesai, palang akan **menutup secara otomatis** dan sistem mencatat penambahan 1 slot kendaraan.

#### 5. Sistem Penerangan Parkir (Otomatis)
Sensor cahaya (LDR) bekerja secara independen untuk mengatur penerangan:
* **Malam/Gelap:** Sensor LDR mendeteksi minimnya cahaya, lampu putih area parkir otomatis **menyala**.
* **Siang/Terang:** Sensor LDR mendeteksi cahaya cukup, lampu otomatis **mati**.

#### 6. Mode Reset Darurat (*Emergency Reset*)
Sistem dilengkapi dengan tombol *Push Button* (Interrupt) untuk kondisi darurat:
* Kapan pun tombol ditekan, sistem akan **mereset semua status** secara instan.
* Palang pintu langsung tertutup, dan data slot parkir **dikosongkan kembali menjadi 0**.
