/* Free-flying camera without collision.
 *
 * The main Graphics::CameraEntity is located through Graphics::Server -> current Level.
 * While active, a detour on CameraEntity::SetTransform substitutes our matrix whenever the
 * game's camera properties try to move that camera, and every tick we push our matrix
 * ourselves, so culling and LOD follow the free camera. Matrices are Nebula2 matrix44:
 * rows x (right), y (up), z (backwards; the camera looks along -z), position. */
#include "game.h"

typedef struct FreeCam {
    int   active;
    BYTE *cam;
    float pos[3];
    float yaw, pitch, roll;     /* radians */
    float fov, origFov;         /* radians */
    float speedMul;
    float m[16], saved[16];
    double last;
} FreeCam;

static FreeCam fc = {0, NULL, {0, 0, 0}, 0, 0, 0, 0, 0, 1.0f};
static Detour d_setTransform;
static void (__fastcall *orig_SetTransform)(BYTE *self, void *edx, const float *m);
static int g_ok = -1;

#define DEG (3.14159265f / 180.0f)

static void __fastcall hk_SetTransform(BYTE *self, void *edx, const float *m)
{
    if (fc.active && self == fc.cam) m = fc.m;
    orig_SetTransform(self, edx, m);
}

static int available(void)
{
    if (g_ok < 0) {
        g_ok = game_ok() &&
               detour_install(&d_setTransform, ADDR_CAM_SETTRANSFORM, PRO_CAM_SETTRANSFORM,
                              (void *)hk_SetTransform, (void **)&orig_SetTransform);
        mlog("freecam: %s", g_ok ? "available" : "DISABLED");
    }
    return g_ok;
}

/* The CameraEntity the world is rendered with (Graphics::Server::GetCamera). */
BYTE *cam_main(void)
{
    BYTE *srv, *lvl, *cam;
    if (!game_ok()) return NULL;
    srv = *(BYTE **)ADDR_GFXSERVER_PTR;
    if (!srv || !game_readable(srv + GFXSERVER_LEVEL, 4)) return NULL;
    lvl = *(BYTE **)(srv + GFXSERVER_LEVEL);
    if (!lvl || !game_readable(lvl + LEVEL_CAMERA, 4)) return NULL;
    cam = *(BYTE **)(lvl + LEVEL_CAMERA);
    if (!cam || !game_readable(cam, CAM_VIEWPROJ_DIRTY + 1)) return NULL;
    if (!game_readable(*(void **)cam, 0x34) || (*(DWORD **)cam)[12] != ADDR_CAM_SETTRANSFORM) return NULL;
    return cam;
}

static void build_matrix(void)
{
    float cy = cosf(fc.yaw), sy = sinf(fc.yaw), cp = cosf(fc.pitch), sp = sinf(fc.pitch);
    float cr = cosf(fc.roll), sr = sinf(fc.roll);
    float f[3] = {cp * sy, sp, cp * cy};
    float x[3] = {-cy, 0.0f, sy};
    float z[3] = {-f[0], -f[1], -f[2]};
    float y[3] = {z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]};
    float *m = fc.m;
    int i;
    for (i = 0; i < 3; i++) {
        m[0 + i] = x[i] * cr + y[i] * sr;
        m[4 + i] = y[i] * cr - x[i] * sr;
        m[8 + i] = z[i];
        m[12 + i] = fc.pos[i];
    }
    m[3] = m[7] = m[11] = 0.0f;
    m[15] = 1.0f;
}

static void from_matrix(const float *m)
{
    float fx = -m[8], fy = -m[9], fz = -m[10];
    float len = sqrtf(fx * fx + fy * fy + fz * fz);
    if (len > 1e-6f) {
        fx /= len;
        fy /= len;
        fz /= len;
    }
    if (fy > 1.0f) fy = 1.0f;
    if (fy < -1.0f) fy = -1.0f;
    fc.pitch = asinf(fy);
    fc.yaw = atan2f(fx, fz);
    fc.roll = 0.0f;
    fc.pos[0] = m[12];
    fc.pos[1] = m[13];
    fc.pos[2] = m[14];
}

static void apply(void)
{
    BYTE *nc = fc.cam + CAM_NCAMERA;
    build_matrix();
    orig_SetTransform(fc.cam, NULL, fc.m);
    if (*(float *)(nc + NCAM_AOV) != fc.fov) {
        *(float *)(nc + NCAM_AOV) = fc.fov;
        nc[NCAM_PROJ_DIRTY] = 1;
    }
    fc.cam[CAM_VIEWPROJ_DIRTY] = 1;
}

int freecam_active(void)
{
    return fc.active;
}

int freecam_set(int on)
{
    int dx, dy, dz;
    if (on == fc.active) return 1;
    if (on) {
        BYTE *cam;
        if (!available() || !(cam = cam_main())) return 0;
        memcpy(fc.saved, cam + CAM_TRANSFORM, sizeof fc.saved);
        from_matrix(fc.saved);
        fc.origFov = fc.fov = *(float *)(cam + CAM_NCAMERA + NCAM_AOV);
        if (fc.speedMul <= 0.0f) fc.speedMul = 1.0f;
        fc.cam = cam;
        fc.last = now_seconds();
        g_host->take_mouse(&dx, &dy, &dz);   /* drop motion collected before */
        fc.active = 1;
        apply();
        mlog("freecam on: cam %p pos %.2f %.2f %.2f yaw %.1f pitch %.1f fov %.1f", cam, fc.pos[0], fc.pos[1],
             fc.pos[2], fc.yaw / DEG, fc.pitch / DEG, fc.fov / DEG);
    } else {
        fc.active = 0;
        if (cam_main() == fc.cam && fc.cam) {
            BYTE *nc = fc.cam + CAM_NCAMERA;
            *(float *)(nc + NCAM_AOV) = fc.origFov;
            nc[NCAM_PROJ_DIRTY] = 1;
            orig_SetTransform(fc.cam, NULL, fc.saved);
            fc.cam[CAM_VIEWPROJ_DIRTY] = 1;
        }
        mlog("freecam off");
    }
    return 1;
}

void freecam_frame(void)
{
    double now = now_seconds();
    float dt = (float)(now - fc.last), speed, mv[3] = {0, 0, 0};
    float cy, sy, cp, sp, f[3], x[3];
    int dx, dy, dz, i;
    fc.last = now;
    g_host->take_mouse(&dx, &dy, &dz);
    if (!fc.active) return;
    if (cam_main() != fc.cam) {   /* level changed: the camera entity is gone */
        fc.active = 0;
        toast("Free camera off (level changed)");
        return;
    }
    if (dt > 0.1f) dt = 0.1f;
    if (dt < 0.0f) dt = 0.0f;

    /* look */
    fc.yaw -= dx * g_cfg.mouse_sens * DEG;
    fc.pitch -= dy * g_cfg.mouse_sens * DEG * (g_cfg.invert_y ? -1.0f : 1.0f);
    if (fc.pitch > 89.0f * DEG) fc.pitch = 89.0f * DEG;
    if (fc.pitch < -89.0f * DEG) fc.pitch = -89.0f * DEG;
    if (dz) {
        fc.speedMul *= powf(1.25f, dz / 120.0f);
        if (fc.speedMul < 0.02f) fc.speedMul = 0.02f;
        if (fc.speedMul > 50.0f) fc.speedMul = 50.0f;
        toast("Camera speed x%.2f", fc.speedMul);
    }

    /* move */
    speed = g_cfg.cam_speed * fc.speedMul;
    if (input_key_down(g_cfg.vk_fast)) speed *= g_cfg.cam_fast_mult;
    if (input_key_down(g_cfg.vk_slow)) speed *= g_cfg.cam_slow_mult;
    cy = cosf(fc.yaw), sy = sinf(fc.yaw), cp = cosf(fc.pitch), sp = sinf(fc.pitch);
    f[0] = cp * sy, f[1] = sp, f[2] = cp * cy;
    x[0] = -cy, x[1] = 0.0f, x[2] = sy;
    for (i = 0; i < 3; i++) {
        if (input_key_down(g_cfg.vk_fwd)) mv[i] += f[i];
        if (input_key_down(g_cfg.vk_back)) mv[i] -= f[i];
        if (input_key_down(g_cfg.vk_right)) mv[i] += x[i];
        if (input_key_down(g_cfg.vk_left)) mv[i] -= x[i];
    }
    if (input_key_down(g_cfg.vk_up)) mv[1] += 1.0f;
    if (input_key_down(g_cfg.vk_down)) mv[1] -= 1.0f;
    for (i = 0; i < 3; i++) fc.pos[i] += mv[i] * speed * dt;

    /* roll / fov */
    if (input_key_down(g_cfg.vk_roll_left)) fc.roll -= g_cfg.roll_speed * DEG * dt;
    if (input_key_down(g_cfg.vk_roll_right)) fc.roll += g_cfg.roll_speed * DEG * dt;
    if (input_key_down(g_cfg.vk_fov_in)) fc.fov -= g_cfg.fov_speed * DEG * dt;
    if (input_key_down(g_cfg.vk_fov_out)) fc.fov += g_cfg.fov_speed * DEG * dt;
    if (input_key_pressed(g_cfg.vk_reset)) {
        fc.roll = 0.0f;
        fc.fov = fc.origFov;
    }
    if (fc.fov < 5.0f * DEG) fc.fov = 5.0f * DEG;
    if (fc.fov > 150.0f * DEG) fc.fov = 150.0f * DEG;
    apply();
}

void freecam_goto(float x, float y, float z, float yawDeg, float pitchDeg)
{
    if (!fc.active && !freecam_set(1)) return;
    fc.pos[0] = x, fc.pos[1] = y, fc.pos[2] = z;
    fc.yaw = yawDeg * DEG;
    fc.pitch = pitchDeg * DEG;
    fc.roll = 0.0f;
    apply();
}

void freecam_shutdown(void)
{
    freecam_set(0);
}

float freecam_yaw(void)
{
    return fc.active ? fc.yaw / DEG : 0.0f;
}

/* Position under the free camera, for "party to camera". */
int freecam_position(float out[3])
{
    if (!fc.active) return 0;
    memcpy(out, fc.pos, sizeof fc.pos);
    return 1;
}

int freecam_command(const char *cmd, const char *arg, char *out, int outsz)
{
    float a = 0, b = 0, c = 0, d = 0, e = 0;
    int n;
    if (!_stricmp(cmd, "cam")) {
        char sub[16] = {0};
        n = sscanf(arg, "%15s %f %f %f %f %f", sub, &a, &b, &c, &d, &e);
        if (!_stricmp(sub, "set") && n >= 4) {
            if (!fc.active && !freecam_set(1)) return snprintf(out, outsz, "freecam unavailable"), 1;
            fc.pos[0] = a, fc.pos[1] = b, fc.pos[2] = c;
            if (n >= 6) fc.yaw = d * DEG, fc.pitch = e * DEG;
        } else if (!_stricmp(sub, "move") && n >= 2) {   /* forward right up (metres) */
            float cy = cosf(fc.yaw), sy = sinf(fc.yaw), cp = cosf(fc.pitch), sp = sinf(fc.pitch);
            if (!fc.active && !freecam_set(1)) return snprintf(out, outsz, "freecam unavailable"), 1;
            fc.pos[0] += a * cp * sy - b * cy;
            fc.pos[1] += a * sp + c;
            fc.pos[2] += a * cp * cy + b * sy;
        } else if (!_stricmp(sub, "turn") && n >= 2) {   /* yaw pitch (degrees) */
            if (!fc.active && !freecam_set(1)) return snprintf(out, outsz, "freecam unavailable"), 1;
            fc.yaw += a * DEG;
            fc.pitch += b * DEG;
        } else if (sub[0] && _stricmp(sub, "get")) {
            return snprintf(out, outsz, "usage: cam [get|set x y z [yaw pitch]|move fwd right up|turn yaw pitch]"), 1;
        }
        {
            BYTE *cam = cam_main();
            const float *m = cam ? (const float *)(cam + CAM_TRANSFORM) : NULL;
            const float *nc = cam ? (const float *)(cam + CAM_NCAMERA) : NULL;
            if (fc.active) apply();
            snprintf(out, outsz,
                     "freecam=%d cam=%p pos %.2f %.2f %.2f yaw %.1f pitch %.1f roll %.1f fov %.1f speed x%.2f\n"
                     "game matrix: x(%.3f %.3f %.3f) y(%.3f %.3f %.3f) z(%.3f %.3f %.3f) p(%.2f %.2f %.2f)\n"
                     "nCamera2: type %d aov %.1f aspect %.3f near %.3f far %.1f",
                     fc.active, cam, fc.pos[0], fc.pos[1], fc.pos[2], fc.yaw / DEG, fc.pitch / DEG, fc.roll / DEG,
                     fc.fov / DEG, fc.speedMul, m ? m[0] : 0, m ? m[1] : 0, m ? m[2] : 0, m ? m[4] : 0,
                     m ? m[5] : 0, m ? m[6] : 0, m ? m[8] : 0, m ? m[9] : 0, m ? m[10] : 0, m ? m[12] : 0,
                     m ? m[13] : 0, m ? m[14] : 0, nc ? *(const int *)nc : -1, nc ? nc[3] / DEG : 0,
                     nc ? nc[4] : 0, nc ? nc[5] : 0, nc ? nc[6] : 0);
        }
        return 1;
    }
    if (!_stricmp(cmd, "fov") || !_stricmp(cmd, "roll") || !_stricmp(cmd, "speed")) {
        if (sscanf(arg, "%f", &a) != 1) return snprintf(out, outsz, "usage: %s <value>", cmd), 1;
        if (!fc.active && !freecam_set(1)) return snprintf(out, outsz, "freecam unavailable"), 1;
        if (!_stricmp(cmd, "fov")) fc.fov = a * DEG;
        else if (!_stricmp(cmd, "roll")) fc.roll = a * DEG;
        else fc.speedMul = a;
        apply();
        snprintf(out, outsz, "fov %.1f roll %.1f speed x%.2f", fc.fov / DEG, fc.roll / DEG, fc.speedMul);
        return 1;
    }
    return 0;
}
