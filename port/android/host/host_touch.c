/* UI thread writes one snapshot; the guest reads it on the input thread. */
#include <jni.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

static pthread_mutex_t touch_lock = PTHREAD_MUTEX_INITIALIZER;
static int32_t touch_state[7]; /* SDL axes followed by SDL button bits */
static float look_delta[2];
static struct timespec look_time;
static uint32_t cheat_pending, cheat_busy;
static int32_t cheat_commands[16], cheat_status[16];
static int rumble_amplitude;
static struct timespec rumble_time;

void host_touch_rumble(unsigned int low, unsigned int high)
{
    unsigned int strength = low > high ? low : high;
    pthread_mutex_lock(&touch_lock);
    rumble_amplitude = strength ? 64 + (strength * 191u / 65535u) : 0;
    clock_gettime(CLOCK_MONOTONIC, &rumble_time);
    pthread_mutex_unlock(&touch_lock);
}

JNIEXPORT jint JNICALL Java_com_halo_decomp_TouchControls_nativeRumble(JNIEnv *env, jclass cls)
{
    struct timespec now;
    int amplitude;
    (void)env; (void)cls;
    clock_gettime(CLOCK_MONOTONIC, &now);
    pthread_mutex_lock(&touch_lock);
    amplitude = rumble_amplitude;
    if ((now.tv_sec-rumble_time.tv_sec)*1000000000LL + now.tv_nsec-rumble_time.tv_nsec > 150000000LL)
        amplitude = 0;
    pthread_mutex_unlock(&touch_lock);
    return amplitude;
}

JNIEXPORT void JNICALL Java_com_halo_decomp_TouchControls_nativeLook(
    JNIEnv *env, jclass cls, jfloat dx, jfloat dy)
{
    (void)env; (void)cls;
    pthread_mutex_lock(&touch_lock);
    look_delta[0] += dx; look_delta[1] += dy;
    clock_gettime(CLOCK_MONOTONIC, &look_time);
    pthread_mutex_unlock(&touch_lock);
}

JNIEXPORT void JNICALL Java_com_halo_decomp_TouchControls_nativeLookReset(JNIEnv *env, jclass cls)
{
    (void)env; (void)cls;
    pthread_mutex_lock(&touch_lock);
    look_delta[0] = look_delta[1] = 0;
    pthread_mutex_unlock(&touch_lock);
}

void host_touch_look_read(float *delta)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    pthread_mutex_lock(&touch_lock);
    memcpy(delta, look_delta, sizeof(look_delta));
    /* Discard movement left over from menus, cutscenes or a suspended app. */
    if ((now.tv_sec-look_time.tv_sec)*1000000000LL + now.tv_nsec-look_time.tv_nsec > 150000000LL)
        delta[0] = delta[1] = 0;
    look_delta[0] = look_delta[1] = 0;
    pthread_mutex_unlock(&touch_lock);
}

JNIEXPORT jboolean JNICALL Java_com_halo_decomp_TouchControls_nativeCheatRequest(
    JNIEnv *env, jclass cls, jint id, jboolean enabled)
{
    (void)env; (void)cls;
    if (id < 0 || id >= 16) return JNI_FALSE;
    pthread_mutex_lock(&touch_lock);
    if (cheat_busy & (1u << id)) { pthread_mutex_unlock(&touch_lock); return JNI_FALSE; }
    cheat_commands[id] = enabled != 0;
    cheat_pending |= 1u << id;
    cheat_busy |= 1u << id;
    pthread_mutex_unlock(&touch_lock);
    return JNI_TRUE;
}

JNIEXPORT jint JNICALL Java_com_halo_decomp_TouchControls_nativeCheatStatus(
    JNIEnv *env, jclass cls, jint id)
{
    int status;
    (void)env; (void)cls;
    if (id < 0 || id >= 16) return -1;
    pthread_mutex_lock(&touch_lock);
    status = (cheat_busy & (1u << id)) ? -2 : cheat_status[id];
    pthread_mutex_unlock(&touch_lock);
    return status;
}

unsigned int host_touch_cheats_read(int *commands)
{
    unsigned int pending;
    pthread_mutex_lock(&touch_lock);
    pending = cheat_pending; cheat_pending = 0;
    memcpy(commands, cheat_commands, sizeof(cheat_commands));
    pthread_mutex_unlock(&touch_lock);
    return pending;
}

void host_touch_cheat_result(int id, int status)
{
    pthread_mutex_lock(&touch_lock);
    cheat_status[id] = status;
    cheat_busy &= ~(1u << id);
    pthread_mutex_unlock(&touch_lock);
}

void host_touch_cheat_sync(int id, int active)
{
    pthread_mutex_lock(&touch_lock);
    if (!(cheat_busy & (1u << id)) && cheat_status[id] >= 0) cheat_status[id] = active;
    pthread_mutex_unlock(&touch_lock);
}

JNIEXPORT void JNICALL Java_com_halo_decomp_TouchControls_nativeState(
	JNIEnv *env, jclass cls, jint lx, jint ly, jint rx, jint ry,
	jint lt, jint rt, jint buttons)
{
	int32_t next[] = { lx, ly, rx, ry, lt, rt, buttons };
	(void)env;
	(void)cls;
	pthread_mutex_lock(&touch_lock);
	memcpy(touch_state, next, sizeof(next));
	pthread_mutex_unlock(&touch_lock);
}

void host_touch_read(int32_t *state)
{
	pthread_mutex_lock(&touch_lock);
	memcpy(state, touch_state, sizeof(touch_state));
	pthread_mutex_unlock(&touch_lock);
}
