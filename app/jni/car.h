// car.h - fisika mobil (raycast vehicle): suspensi pegas-redam, gaya ban, rem, drag.
#pragma once
#include "core.h"
#include "config.h"

struct Island;

struct Wheel {
    v3 pos;          // posisi dunia titik gantung
    f32 kompresi = 0;    // 0..travel (m)
    f32 kompresiPrev = 0;
    f32 sudutPutar = 0;  // rotasi roda (rad)
    f32 sudutSetir = 0;  // roda depan
    bool kontak = false;
    f32 gayaNormal = 0;
    v3 gayaTotal;
};

struct CarState {
    v3 pos;              // pusat massa dunia
    v3 vel;
    f32 yaw = 0;         // rotasi heading (rad, 0 = +Z)
    f32 yawRate = 0;
    f32 pitch = 0, roll = 0;      // kemiringan bodi (visual/fisika ringan)
    f32 pitchRate = 0, rollRate = 0;
    f32 setir = 0;       // -1..1 (input terhalus)
    f32 throttle = 0;    // 0..1
    f32 brake = 0;       // 0..1 (rem / mundur)
    bool handbrake = false;
    v3 vForward, vRight;
    Wheel wheels[4];
    f32 kecepatanMaju = 0;
    f32 gripSekarang = 0.9f;
    f32 tinggiRoda[4] = {0, 0, 0, 0};
    bool diLaut = false;
    f32 yawVisual = 0;
    f32 spinRoda[4] = {0, 0, 0, 0};
};

struct CarInput {
    f32 setir = 0;      // -1..1
    f32 gas = 0;        // 0..1
    f32 rem = 0;        // 0..1
    bool reset = false;
};

class Car {
public:
    CarState s;
    void reset(const v3& pos, f32 yaw);
    // step fisika; heightFn(x,z) = tinggi terrain
    void step(f32 dt, const CarInput& in, const Island& isl, f32 gripOverride);
    void computeBasis();
};
