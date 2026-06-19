/* Per-game detection profiles with tuned frequency bands, thresholds,
 * cooldowns, and overlay colors. */
#ifndef SOUND_OVERLAY_PROFILES_H
#define SOUND_OVERLAY_PROFILES_H

#include <windows.h>

typedef struct {
    const wchar_t *id;            /* internal key ("universal", "fortnite", ...) */
    const wchar_t *display_name;  /* full name shown in status / log */
    const wchar_t *short_name;    /* compact label for the game button */

    /* Footstep band (Hz). */
    float foot_lo, foot_hi;
    /* Gunshot transient band (Hz). */
    float gun_lo, gun_hi;
    /* Gunshot ultra band for crack detection (Hz). */
    float gun_ultra_lo, gun_ultra_hi;
    /* Vehicle rumble band (Hz). */
    float veh_lo, veh_hi;
    /* Explosion boom band (Hz). */
    float expl_lo, expl_hi;

    /* Per-class threshold multipliers (applied on top of user sensitivity). */
    float foot_thresh;
    float gun_flux_thresh;
    float gun_power_thresh;
    float gun_ultra_thresh;
    float veh_thresh;
    float expl_thresh;

    /* Refractory periods (seconds). */
    float foot_cooldown;
    float gun_cooldown;
    float veh_cooldown;
    float expl_cooldown;

    /* Noise floor adaptation speed (lower = slower). */
    float nf_alpha;

    /* Warmup frames before detection starts. */
    int warmup;

    /* Which event types this profile enables by default. */
    int enable_foot;
    int enable_gun;
    int enable_veh;
    int enable_expl;

    /* Overlay marker colors. */
    COLORREF color_foot;
    COLORREF color_gun;
    COLORREF color_veh;
    COLORREF color_expl;

    /* Default overlay size and sensitivity for this game. */
    int   default_size;
    float default_sensitivity;
} GameProfile;

#define PROFILE_COUNT 11

extern const GameProfile g_profiles[PROFILE_COUNT];

const GameProfile *profile_find(const wchar_t *id);
const GameProfile *profile_by_index(int idx);
int                profile_index_of(const wchar_t *id);

#endif
