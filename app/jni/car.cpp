// car.cpp - fisika mobil sederhana tapi stabil (model raycast 4 roda).
#include "car.h"
#include "world.h"

void Car::computeBasis() {
    s.vForward = v3(sinf(s.yaw), 0, cosf(s.yaw));
    s.vRight = v3(s.vForward.z, 0, -s.vForward.x);
}

void Car::reset(const v3& pos, f32 yaw) {
    memset(&s, 0, sizeof(s));
    s.pos = pos;
    s.yaw = yaw;
    computeBasis();
    for (int i = 0; i < 4; i++) {
        s.wheels[i].kompresi = 0.05f;
        s.wheels[i].kompresiPrev = 0.05f;
    }
}

void Car::step(f32 dt, const CarInput& in, const Island& isl, f32 grip) {
    if (dt <= 0) return;
    computeBasis();
    s.gripSekarang = grip;

    // ---- setir halus (analog) menuju target, kembali ke tengah pelan
    f32 targetSetir = vclamp(in.setir, -1.f, 1.f);
    if (vabs(targetSetir) < Cfg::zonaMatiSetir) targetSetir = 0;
    f32 rate = vabs(targetSetir) > vabs(s.setir) ? Cfg::kecepatanSetir : Cfg::kecepatanKembaliSetir;
    f32 d = targetSetir - s.setir;
    f32 maxd = rate * dt;
    if (vabs(d) > maxd) d = signf(d) * maxd;
    s.setir += d;
    // kurangi sudut setir di kecepatan tinggi (stabilitas)
    f32 kec = vabs(s.kecepatanMaju);
    f32 setirMaks = Cfg::sudutSetirMaks * (1.f - 0.55f * vclamp(kec / 50.f, 0.f, 1.f));
    s.throttle = vclamp(in.gas, 0.f, 1.f);
    s.brake = vclamp(in.rem, 0.f, 1.f);

    // ---- posisi & tinggi tanah per roda
    f32 hw = Cfg::lebarGandar, hl = Cfg::jarakSumbu * 0.5f;
    const f32 offs[4][2] = {{hw, hl}, {-hw, hl}, {hw, -hl}, {-hw, -hl}};  // FL FR RL RR
    f32 bodyY = s.pos.y;
    f32 travel = Cfg::suspensiTravel;
    f32 totalGayaY = 0, torsiPitch = 0, torsiRoll = 0;
    bool adaKontak = false;

    for (int i = 0; i < 4; i++) {
        f32 lx = offs[i][0], lz = offs[i][1];
        v3 wp = s.pos + s.vRight * lx + s.vForward * lz;
        f32 gh = isl.height(wp.x, wp.z);
        s.tinggiRoda[i] = gh;
        f32 hubY = bodyY - Cfg::tinggiGantung;
        f32 jarak = hubY - gh;                 // panjang suspensi terkompresi
        f32 kompresi = Cfg::suspensiRest - jarak;
        if (kompresi < 0) kompresi = 0;
        if (kompresi > travel) kompresi = travel;
        f32 kecepatanKompresi = (kompresi - s.wheels[i].kompresiPrev) / dt;
        s.wheels[i].kompresiPrev = kompresi;
        s.wheels[i].kompresi = kompresi;
        s.wheels[i].kontak = (jarak <= Cfg::suspensiRest + 0.02f);
        f32 gaya = 0;
        if (s.wheels[i].kontak) {
            adaKontak = true;
            gaya = Cfg::suspensiK * kompresi + Cfg::suspensiD * kecepatanKompresi;
            if (gaya < 0) gaya = 0;
        }
        s.wheels[i].gayaNormal = gaya;
        totalGayaY += gaya;
        torsiPitch += gaya * lz;
        torsiRoll += gaya * lx;
        s.wheels[i].pos = v3(wp.x, gh, wp.z);
    }

    // ---- gravitasi + gaya normal suspensi
    f32 aY = -Cfg::gravitasi;
    s.vel.y += aY * dt;
    s.vel.y += (totalGayaY / Cfg::massa) * dt;
    s.pos.y += s.vel.y * dt;

    // pitch/roll dari distribusi gaya (redam)
    f32 pitchTarget = -torsiPitch / (Cfg::inersiaPitch * 6.f);
    f32 rollTarget = torsiRoll / (Cfg::inersiaRoll * 6.f);
    s.pitch = vlerp(s.pitch, pitchTarget, vclamp(dt * 7.f, 0.f, 1.f));
    s.roll = vlerp(s.roll, rollTarget, vclamp(dt * 7.f, 0.f, 1.f));

    // pantulan kecil jika menembus tanah (anti jatuh menembus)
    f32 groundHere = isl.height(s.pos.x, s.pos.z);
    f32 minY = groundHere + Cfg::tinggiGantung - 0.30f;
    if (s.pos.y < minY) {
        s.pos.y = minY;
        if (s.vel.y < 0) s.vel.y = -s.vel.y * 0.12f;
    }

    // ---- kecepatan dalam koordinat bodi
    f32 vF = dot(s.vel, s.vForward);
    f32 vR = dot(s.vel, s.vRight);
    s.kecepatanMaju = vF;

    // ---- roda: sudut setir
    for (int i = 0; i < 2; i++) s.wheels[i].sudutSetir = s.setir * setirMaks;

    // ---- gaya ban
    f32 mu = Cfg::muAspal * grip;
    if (mu < 0.08f) mu = 0.08f;
    f32 downforce = 1.f + Cfg::gripDownforce * vF * vF;
    f32 muEff = vclamp(mu * downforce, 0.f, 1.9f);
    f32 gayaMaksRoda = muEff * (totalGayaY * 0.25f + 200.f);

    // gaya mesin: penggerak belakang (roda 2,3), lebih kecil saat menikung tajam
    f32 gayaMesin = 0;
    bool mundur = false;
    if (s.throttle > 0.01f) {
        if (s.brake > 0.01f && vF < 0.4f) {
            mundur = true;  // rem sambil berhenti -> mundur
        } else {
            gayaMesin = Cfg::gayaMesin * s.throttle;
            if (vF > Cfg::batasKecepatan) gayaMesin *= 0.25f;
        }
    }
    f32 gayaMundur = 0;
    if (mundur) {
        if (vF > -Cfg::kecepatanMundurMaks) gayaMundur = -Cfg::gayaMesin * 0.55f * s.brake;
    }
    // rem: hanya jika bergerak
    f32 gayaRem = 0;
    if (s.brake > 0.01f && vF > 0.4f) gayaRem = -Cfg::gayaRem * s.brake * signf(vF);

    // hambatan
    f32 hambat = -Cfg::hambatUdara * vF * vabs(vF) - Cfg::hambatGuling * vF;
    f32 aLong = (gayaMesin + gayaMundur + gayaRem + hambat) / Cfg::massa;
    // batas traksi roda belakang
    f32 traksiMaks = gayaMaksRoda * 2.0f;
    f32 gayaLongTotal = gayaMesin + gayaMundur;
    if (gayaLongTotal > traksiMaks) aLong = (traksiMaks + gayaRem + hambat) / Cfg::massa;
    if (gayaLongTotal < -traksiMaks) aLong = (-traksiMaks + gayaRem + hambat) / Cfg::massa;
    if (!adaKontak) aLong *= 0.15f;   // roda bebas di udara

    s.vel += s.vForward * (aLong * dt);

    // ---- gaya samping (grip) : kurangi kecepatan lateral sesuai traksi
    f32 gripLat = vclamp(Cfg::kakuLateral * muEff * 0.055f, 0.f, 0.95f);
    f32 vRafter = vR * (1.f - vclamp(gripLat * dt * 60.f * 0.25f, 0.f, 1.f));
    s.vel -= s.vRight * (vR - vRafter);

    // ---- yaw dari geometri setir (bicycle model sederhana)
    if (adaKontak && vabs(vF) > 0.4f) {
        f32 sudutRoda = s.wheels[0].sudutSetir;
        f32 yawTarget = vF * tanf(sudutRoda) / Cfg::jarakSumbu;
        // batas gaya samping ban
        f32 yawMaks = muEff * Cfg::gravitasi / vmax(1.f, vabs(vF));
        yawTarget = vclamp(yawTarget, -yawMaks * 2.2f, yawMaks * 2.2f);
        s.yawRate = vlerp(s.yawRate, yawTarget, vclamp(dt * 9.f, 0.f, 1.f));
    } else {
        s.yawRate = vlerp(s.yawRate, 0.f, vclamp(dt * 4.f, 0.f, 1.f));
    }
    s.yaw += s.yawRate * dt;

    // ---- percepatan lateral mempengaruhi roll
    s.roll += vclamp(-s.yawRate * vF * 0.006f, -0.16f, 0.16f) * dt * 8.f;

    // ---- posisi horizontal
    s.pos.x += s.vel.x * dt;
    s.pos.z += s.vel.z * dt;

    // ---- roda berputar sesuai kecepatan
    for (int i = 0; i < 4; i++) {
        f32 v = vF;
        s.wheels[i].sudutPutar += (v / Cfg::radiusRoda) * dt;
        while (s.wheels[i].sudutPutar > 2 * PI) s.wheels[i].sudutPutar -= 2 * PI;
        while (s.wheels[i].sudutPutar < 0) s.wheels[i].sudutPutar += 2 * PI;
    }

    // ---- jatuh ke laut -> tandai (game yang memindahkan)
    s.diLaut = (s.pos.y < Cfg::airY - 0.5f) ||
               (isl.height(s.pos.x, s.pos.z) < -1.5f && s.pos.y < 2.f);
}
