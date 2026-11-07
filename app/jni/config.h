// config.h - SEMUA angka tuning fisika, kamera, streaming, kontrol, visual di sini.
#pragma once

// ------------------------------------------------------------------ fisika mobil (SI)
namespace Cfg {
// mass & geometri
constexpr f32 massa = 1250.f;             // kg
constexpr f32 jarakSumbu = 2.78f;         // m (wheelbase)
constexpr f32 lebarGandar = 0.80f;        // setengah lebar track roda
constexpr f32 tinggiPusatMassa = 0.42f;   // m di atas titik gantung
constexpr f32 radiusRoda = 0.35f;
constexpr f32 inersiaYaw = 2400.f;        // kg m^2
constexpr f32 inersiaPitch = 1800.f;
constexpr f32 inersiaRoll = 900.f;

// suspensi (4 roda)
constexpr f32 suspensiRest = 0.34f;       // panjang bebas (m)
constexpr f32 suspensiK = 34000.f;        // N/m
constexpr f32 suspensiD = 3300.f;         // Ns/m
constexpr f32 suspensiTravel = 0.22f;     // m
constexpr f32 tinggiGantung = 0.62f;      // titik gantung di atas dasar roda

// ban
constexpr f32 muAspal = 1.05f;
constexpr f32 muTanah = 0.62f;
constexpr f32 muRumput = 0.40f;
constexpr f32 muPasir = 0.30f;
constexpr f32 kakuLateral = 14.f;         // gaya samping per slip
constexpr f32 gayaMesin = 9500.f;         // N (total di roda belakang)
constexpr f32 gayaRem = 12000.f;
constexpr f32 gayaRemTangan = 2600.f;
constexpr f32 kecepatanMundurMaks = 7.0f;
constexpr f32 hambatUdara = 2.05f;        // koef drag (N per m/s^2)
constexpr f32 hambatGuling = 190.f;
constexpr f32 sudutSetirMaks = 32.f * DEG2RAD;
constexpr f32 kecepatanSetir = 2.6f;      // rad/s menuju target
constexpr f32 kecepatanKembaliSetir = 3.6f;
constexpr f32 gravitasi = 9.81f;
constexpr f32 batasKecepatan = 62.f;       // m/s (~223 km/jam)
constexpr f32 gripDownforce = 0.025f;      // mu tambahan per (m/s)^2

// ------------------------------------------------------------------ kamera
constexpr f32 kamJarak = 6.4f;
constexpr f32 kamTinggi = 2.25f;
constexpr f32 kamLookTinggi = 1.05f;
constexpr f32 kamLag = 6.5f;
constexpr f32 kamFov = 62.f;
constexpr f32 kamFovCepat = 72.f;
constexpr f32 kamNear = 0.35f;
constexpr f32 kamFar = 1600.f;

// ------------------------------------------------------------------ dunia / streaming
constexpr f32 chunkUkuran = 200.f;      // m per chunk instan
constexpr f32 jarakPandang = 340.f;     // m (300-400)
constexpr int maksInstance = 6000;
constexpr f32 lodJarak = 115.f;         // > ini pakai _lod1
constexpr f32 jalanJarakPandang = 420.f;
constexpr int resSegmenJalan = 200;     // m per segmen jalan (dari roads.json/glb)

// ------------------------------------------------------------------ kontrol
constexpr f32 tombolMinPx = 44.f;       // dp minimum
constexpr f32 tombolAlpha = 0.30f;
constexpr f32 zonaMatiSetir = 0.06f;

// ------------------------------------------------------------------ visual
constexpr f32 airY = 0.0f;
constexpr f32 langitAtas[3] = {0.33f, 0.56f, 0.86f};
constexpr f32 langitBawah[3] = {0.74f, 0.84f, 0.93f};
constexpr f32 kabutMulai = 210.f;
constexpr f32 kabutAkhir = 430.f;
constexpr f32 offsetsJalan = 0.05f;
}  // namespace Cfg
