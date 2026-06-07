import os
Import("env")

# Cegah berjalan saat IDE sedang mengindeks (integration dump)
if env.IsIntegrationDump():
    Return()

env_file = ".env"

if os.path.exists(env_file):
    print("=== Membaca konfigurasi dari .env ===")
    with open(env_file) as f:
        for line in f:
            line = line.strip()
            # Abaikan baris kosong atau komentar
            if not line or line.startswith("#"):
                continue
            
            # Pastikan format key=val valid
            if "=" not in line:
                continue
                
            key, val = line.split("=", 1)
            key = key.strip()
            val = val.strip().strip("'\"") # Hapus tanda kutip luar jika ada
            
            # Masukkan ke dalam CPPDEFINES (makro C++)
            # String dibungkus dengan kutip dua yang lolos (\")
            env.Append(CPPDEFINES=[(key, '\\"' + val + '\\"')])
            print(f"  Macro ditambahkan: {key}")
else:
    print("=== Peringatan: File .env tidak ditemukan! ===")
