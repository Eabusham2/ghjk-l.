#include "profiles.h"
#include <string.h>

/*
 * MW2 "Home Theater" audio mode notes
 * ====================================
 * Home Theater is a wide, cinematic 5.1/7.1 mix with:
 *   - Heavy sub-bass from LFE channel (explosions, vehicles, ambient rumble)
 *   - Large dynamic range: footsteps are quiet relative to gunfire
 *   - Strong center-channel dialogue/UI that bleeds into L/R on downmix
 *   - Gunshots have a sharp transient crack in 2-8 kHz with a bass thump
 *   - Footsteps sit in 60-180 Hz with a subtle high-freq tap around 3 kHz
 *   - Vehicle engine rumble is very low (20-60 Hz), distinct from footsteps
 *
 * Because the WASAPI capture drops the dedicated LFE channel during
 * downmix (it's omnidirectional and would blur panning), we actually
 * get cleaner directional cues than the player hears on physical speakers.
 * But footsteps are buried under the wide mix, so we need:
 *   - Lower foot_thresh (more sensitive) with tight band (60-180 Hz)
 *   - Slow noise-floor alpha (0.012) so transient steps aren't eaten
 *   - Longer warmup (60 frames) because the mix is loud and varied
 *   - High explosion threshold to avoid false triggers from the heavy bass
 *   - Vehicle band pushed very low (15-55 Hz) to separate from footsteps
 */

const GameProfile g_profiles[PROFILE_COUNT] = {

/* ---- 0  Universal --------------------------------------------------- */
{
    L"universal", L"Universal (Any Game)", L"Universal",
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
    L"fortnite", L"Fortnite", L"Fortnite",
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
    L"warzone", L"Call of Duty: Warzone", L"Warzone",
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

/* ---- 3  COD MW2 — Home Theater -------------------------------------- */
{
    L"mw2_ht", L"COD MW2 (Home Theater)", L"MW2 HomeThtr",
    /*
     * Footsteps: MW2 Home Theater places steps in a tight 60-180 Hz
     * band.  The high-frequency "tap" component (~3 kHz) is too quiet
     * to use reliably in this mix, so we focus on the bass thump.
     * Threshold is low (2.8) because steps are quiet in this mode.
     */
    60, 180,          /* foot — tight, avoids LFE bleed below 50 Hz */
    2000, 8000,       /* gun — MW2 rifles have a wide 2-8 kHz crack */
    8000, 16000,      /* gun ultra — snap/echo tail */
    15, 55,           /* vehicle — very low rumble, below footsteps */
    20, 140,          /* explosion — overlaps foot but much louder */

    /* Thresholds: foot is low for quiet steps; gun needs big flux;
     * explosion is high to avoid LFE bass false triggers. */
    2.8f,             /* foot_thresh — sensitive */
    5.8f,             /* gun_flux_thresh */
    3.6f,             /* gun_power_thresh */
    2.2f,             /* gun_ultra_thresh */
    3.5f,             /* veh_thresh */
    5.0f,             /* expl_thresh — high to reject bass rumble */

    /* Cooldowns: slightly longer than default because Home Theater
     * reverb tails can re-trigger. */
    0.12f,            /* foot cooldown */
    0.16f,            /* gun cooldown */
    0.45f,            /* veh cooldown — vehicles sustain */
    0.35f,            /* expl cooldown */

    0.012f,           /* nf_alpha — slow so quiet footsteps aren't eaten */
    60,               /* warmup — longer; the mix is loud and varied */

    1, 1, 1, 1,       /* all types enabled */

    RGB(34,230,130),  /* foot: green */
    RGB(255,64,80),   /* gun:  red */
    RGB(60,140,255),  /* veh:  blue */
    RGB(255,170,40),  /* expl: orange */

    380,              /* default_size — bigger for living-room distance */
    0.85f             /* default_sensitivity — slightly more sensitive */
},

/* ---- 4  Valorant ---------------------------------------------------- */
{
    L"valorant", L"Valorant", L"Valorant",
    60, 240,
    2000, 7500,
    7500, 14000,
    0, 0,
    30, 200,
    2.8f, 5.0f, 3.5f, 2.2f,
    99.0f, 4.0f,
    0.08f, 0.10f, 1.0f, 0.20f,
    0.022f, 30,
    1, 1, 0, 1,
    RGB(34,230,130), RGB(255,64,80), RGB(60,140,255), RGB(255,170,40),
    300, 0.85f
},

/* ---- 5  Counter-Strike 2 ------------------------------------------- */
{
    L"cs2", L"Counter-Strike 2", L"CS2",
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

/* ---- 6  Apex Legends ------------------------------------------------ */
{
    L"apex", L"Apex Legends", L"Apex",
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

/* ---- 7  PUBG -------------------------------------------------------- */
{
    L"pubg", L"PUBG: Battlegrounds", L"PUBG",
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

/* ---- 8  Rainbow Six Siege ------------------------------------------- */
{
    L"r6siege", L"Rainbow Six Siege", L"R6 Siege",
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

/* ---- 9  Overwatch 2 ------------------------------------------------- */
{
    L"ow2", L"Overwatch 2", L"Overwatch 2",
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

/* ---- 10  Escape from Tarkov ---------------------------------------- */
{
    L"tarkov", L"Escape from Tarkov", L"Tarkov",
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
