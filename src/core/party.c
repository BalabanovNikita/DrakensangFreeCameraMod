/* Move the player party to any position: the game's FormationManager places the party in
 * formation around a matrix (position, facing -z), as Actions::TransferEntity does for an entry
 * point inside the current level. Runs from the message pump, never during a level load. */
#include "game.h"

#define DEG (3.14159265f / 180.0f)

static struct { float m[16]; int pending; } g_req;

typedef void (__fastcall *PFN_PlaceAt)(void *self, void *edx, const float *m);
typedef void (__fastcall *PFN_Void)(void *self, void *edx);

static void do_place(const char *a, const char *b)
{
    BYTE *fm;
    (void)a;
    (void)b;
    if (!g_req.pending) return;
    if (level_loading() || level_change_pending() || level_travel_queued()) {   /* try again later */
        defer_call(do_place, "", "");
        return;
    }
    g_req.pending = 0;
    fm = *(BYTE **)G->formation_mgr_ptr;
    if (!fm || !spawn_current_level()) {
        mlog("party: no formation manager / level");
        return;
    }
    ((PFN_PlaceAt)G->formation_place)(fm, NULL, g_req.m);
    ((PFN_Void)G->formation_finish)(fm, NULL);
    mlog("party: moved to %.1f %.1f %.1f", g_req.m[12], g_req.m[13], g_req.m[14]);
}

/* Party to (x, y, z) facing yaw (degrees; 0 = +z, 90 = +x, as the free camera). */
int party_teleport(float x, float y, float z, float yawDeg)
{
    float sy = sinf(yawDeg * DEG), cy = cosf(yawDeg * DEG), *m = g_req.m;
    if (!game_ok() || !G->formation_place || !G->formation_mgr_ptr) return 0;
    memset(m, 0, sizeof g_req.m);
    m[0] = cy, m[2] = -sy;          /* x: right */
    m[5] = 1.0f;                    /* y: up */
    m[8] = -sy, m[10] = -cy;        /* z: backwards (the party faces -z) */
    m[12] = x, m[13] = y, m[14] = z, m[15] = 1.0f;
    g_req.pending = 1;
    return defer_call(do_place, "", "");
}

/* F4 / "party cam": to the point under the free camera (or the game camera). */
int party_to_camera(char *msg, int msgsz)
{
    float p[3], yaw = 0.0f;
    if (!freecam_position(p)) {
        BYTE *cam = cam_main();
        if (!cam) return snprintf(msg, msgsz, "no camera"), 0;
        memcpy(p, cam + CAM_TRANSFORM + 12 * sizeof(float), sizeof p);
    }
    yaw = freecam_yaw();
    if (!party_teleport(p[0], p[1], p[2], yaw)) return snprintf(msg, msgsz, "not available"), 0;
    return snprintf(msg, msgsz, "party moving to %.1f %.1f %.1f", p[0], p[1], p[2]), 1;
}

int party_command(const char *cmd, const char *arg, char *out, int outsz)
{
    float x, y, z, yaw = 0.0f;
    if (_stricmp(cmd, "party")) return 0;
    if (!_stricmp(arg, "cam")) {
        party_to_camera(out, outsz);
        return 1;
    }
    if (sscanf(arg, "%f %f %f %f", &x, &y, &z, &yaw) < 3) return snprintf(out, outsz, "usage: party <x y z [yaw]> | cam"), 1;
    snprintf(out, outsz, party_teleport(x, y, z, yaw) ? "party moving to %.1f %.1f %.1f" : "FAILED", x, y, z);
    return 1;
}
