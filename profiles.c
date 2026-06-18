#include "profiles.h"
#include <string.h>

/* Tuning notes
 * ============
 * Each game has a distinct audio mix.  The bands and thresholds below
 * were derived from analyzing typical gameplay recordings.  "Universal"
 * is a safe middle-ground.  Games with very bassy footsteps (Tarkov,
 * PUBG) have a wider low band; games with crisp audio (Valorant, CS2)
 * have tighter thresholds and shorter cooldowns.
 */

const GameProfile g_profiles[PROFILE_COUNT] = {

/* ---- 0  Universal --------------------------------------------------- */
{
    L"universal", L"Universal (Any Game)",
    40, 220,         /* foot */
    1500, 6000,      /* gun */
    6000, 12000,     /* gun ultra */
    20, 80,          /* vehicle */
    25, 180,         /* explosion */
    3.5f, 6.0f, 4.0f, 2.5f,   /* thresholds */
    3.0f, 4.0f,                /* veh, expl */
    0.10f, 0.13f, 0.30f, 0.25f, /* cooldowns */
    0.020f, 40,                /* nf_alpha, warmup */
    1, 1, 1, 1,                /* enabled */
    RGB(34,230,130), RGB(255,64,80), RGB(60,140,255), RGB(255,170,40),
    320, 1.0f
},

/* ---- 1  Fortnite ---------------------------------------------------- */
{
    L"fortnite", L"Fortnite",
    50, 250,
    1800, 7000,
    7000, 14000,
    20, 90,
    30, 200,
    3.2f, 5.5f, 3.8f, 2.3f,
    3.0f, 3.8f,
    0.09f, 0.12f, 0.35f, 0.25f,
    0.020f, 35,
    1, 1, 1, 1,
    RGB(34,230,130), RGB(255,64,80), RGB(60,140,255), RGB(255,170,40),
    340, 0.9f
},

/* ---- 2  Call of Duty: Warzone --------------------------------------- */
{
    L"warzone", L"Call of Duty: Warzone",
    40, 200,
    1200, 5500,
    5500, 11000,
    18, 70,
    20, 160,
    3.8f, 6.5f, 4.2f, 2.8f,
    2.8f, 3.5f,
    0.10f, 0.15f, 0.40f, 0.30f,
    0.018f, 45,
    1, 1, 1, 1,
    RGB(34,230,130), RGB(255,64,80), RGB(60,140,255), RGB(255,170,40),
    360, 1.1f
},

/* ---- 3  Valorant ---------------------------------------------------- */
{
    L"valorant", L"Valorant",
    60, 240,
    2000, 7500,
    7500, 14000,
    0, 0,           /* no vehicles in Valorant */
    30, 200,
    2.8f, 5.0f, 3.5f, 2.2f,
    99.0f, 4.0f,    /* vehicle disabled via impossible threshold */
    0.08f, 0.10f, 1.0f, 0.20f,
    0.022f, 30,
    1, 1, 0, 1,
    RGB(34,230,130), RGB(255,64,80), RGB(60,140,255), RGB(255,170,40),
    300, 0.85f
},

/* ---- 4  Counter-Strike 2 ------------------------------------------- */
{
    L"cs2", L"Counter-Strike 2",
    50, 230,
    2000, 8000,
    8000, 15000,
    0, 0,
    25, 180,
    2.6f, 4.8f, 3.2f, 2.0f,
    99.0f, 4.2f,
    0.07f, 0.10f, 1.0f, 0.22f,
    0.025f, 28,
    1, 1, 0, 1,
    RGB(34,230,130), RGB(255,64,80), RGB(60,140,255), RGB(255,170,40),
    300, 0.80f
},

/* ---- 5  Apex Legends ------------------------------------------------ */
{
    L"apex", L"Apex Legends",
    45, 260,
    1600, 6500,
    6500, 13000,
    20, 85,
    25, 190,
    3.0f, 5.5f, 3.8f, 2.4f,
    3.2f, 3.6f,
    0.09f, 0.12f, 0.35f, 0.25f,
    0.020f, 38,
    1, 1, 1, 1,
    RGB(34,230,130), RGB(255,64,80), RGB(60,140,255), RGB(255,170,40),
    340, 0.95f
},

/* ---- 6  PUBG -------------------------------------------------------- */
{
    L"pubg", L"PUBG: Battlegrounds",
    35, 200,
    1300, 5000,
    5000, 10000,
    18, 75,
    20, 150,
    4.0f, 7.0f, 4.5f, 3.0f,
    2.5f, 3.2f,
    0.11f, 0.16f, 0.45f, 0.30f,
    0.016f, 50,
    1, 1, 1, 1,
    RGB(34,230,130), RGB(255,64,80), RGB(60,140,255), RGB(255,170,40),
    360, 1.15f
},

/* ---- 7  Rainbow Six Siege ------------------------------------------- */
{
    L"r6siege", L"Rainbow Six Siege",
    55, 220,
    1800, 7000,
    7000, 14000,
    0, 0,
    30, 250,
    2.5f, 4.5f, 3.0f, 2.0f,
    99.0f, 3.5f,
    0.07f, 0.09f, 1.0f, 0.18f,
    0.025f, 30,
    1, 1, 0, 1,
    RGB(34,230,130), RGB(255,64,80), RGB(60,140,255), RGB(255,170,40),
    300, 0.80f
},

/* ---- 8  Overwatch 2 ------------------------------------------------- */
{
    L"ow2", L"Overwatch 2",
    50, 280,
    1500, 6000,
    6000, 12000,
    0, 0,
    30, 220,
    3.0f, 5.5f, 3.8f, 2.5f,
    99.0f, 3.8f,
    0.09f, 0.11f, 1.0f, 0.22f,
    0.022f, 35,
    1, 1, 0, 1,
    RGB(34,230,130), RGB(255,64,80), RGB(60,140,255), RGB(255,170,40),
    320, 0.90f
},

/* ---- 9  Escape from Tarkov ----------------------------------------- */
{
    L"tarkov", L"Escape from Tarkov",
    30, 180,
    1000, 4500,
    4500, 9000,
    15, 65,
    18, 140,
    4.5f, 7.5f, 5.0f, 3.2f,
    2.5f, 3.0f,
    0.12f, 0.18f, 0.50f, 0.35f,
    0.014f, 55,
    1, 1, 1, 1,
    RGB(34,230,130), RGB(255,64,80), RGB(60,140,255), RGB(255,170,40),
    380, 1.2f
},

};

const GameProfile *profile_find(const wchar_t *id) {
    if (!id) return &g_profiles[0];
    for (int i = 0; i < PROFILE_COUNT; ++i) {
        if (lstrcmpiW(g_profiles[i].id, id) == 0)
            return &g_profiles[i];
    }
    return &g_profiles[0];
}

const GameProfile *profile_by_index(int idx) {
    if (idx < 0 || idx >= PROFILE_COUNT) return &g_profiles[0];
    return &g_profiles[idx];
}

int profile_index_of(const wchar_t *id) {
    if (!id) return 0;
    for (int i = 0; i < PROFILE_COUNT; ++i) {
        if (lstrcmpiW(g_profiles[i].id, id) == 0)
            return i;
    }
    return 0;
}
