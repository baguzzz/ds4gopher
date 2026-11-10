# DS4Gopher

Gopher360-style controller utility untuk DualShock 4, membaca input via **DirectInput** langsung tanpa DS4Windows.

## Download

Buka tab **Actions** di repo → pilih workflow run terakhir → download artifact:
- `ds4gopher-windows-x64` → `ds4gopher.exe`
- `config-editor` → `config-editor.html`
- `default-config` → `config.ini`
- `keyboard-virtual-ahk` → `keyboard-virtual.ahk`

Atau unduh dari tab **Releases** jika ada tag.

## Cara Pakai

1. Taruh `ds4gopher.exe` dan `config.ini` di folder yang sama.
2. (Opsional) Taruh `keyboard-virtual.ahk` di folder yang sama. Install AutoHotkey v1, lalu jalankan AHK script itu.
3. Klik kanan `ds4gopher.exe` → **Run as administrator**.
4. Colok DS4 via USB (lebih stabil dari Bluetooth).
5. Selesai.

## Custom Mapping

Buka `config-editor.html` di Chrome/Edge. Colok DS4, remap tombol, export `config.ini`. Taruh di folder yang sama dengan exe.

## Troubleshooting

**Tombol salah / tidak berfungsi:**
Jalankan `ds4gopher.exe --debug`, tekan tombol DS4 satu per satu. Console akan print indeks tombol. Edit `[ButtonIndices]` di `config.ini` sesuai nomor yang muncul.

**UAC prompt tidak bisa diklik:**
Windows memblokir semua input pihak ketiga di layar UAC (Secure Desktop). Ini tidak bisa di-workaround. Gunakan keyboard fisik untuk menyetujui UAC.

**L2/R2 tidak berfungsi:**
Trigger kadang muncul sebagai axes `lZ`/`lRz`, kadang sebagai tombol 6/7. Cek `--debug`, sesuaikan `[ButtonIndices]`.

## Build Sendiri

Repo ini sudah punya GitHub Actions. Push ke branch `main` → workflow otomatis build. Hasil ada di tab Actions.

Atau build lokal (butuh Visual Studio): cl /EHsc /std:c++17 /O2 /MT src\ds4gopher.cpp /Fe:ds4gopher.exe /link dinput8.lib dxguid.lib user32.lib
