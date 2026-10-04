/*
HOST_MOVIE.C

The movies an Android build plays for the game (port/linux/src/bink_null.c).

The port has no Bink decoder (the RAD SDK is proprietary), so the guest asks
for a movie here and the device's own player (MoviePlayer.java) plays the
port's transcoded file over the game's surface. Nothing blocks: the guest's
BinkOpen asks and goes on, and the engine's playback loop (bink_playback.c)
reads the frame the player is on and ends the movie when it is over.

The guest calls host_movie_*; the player calls the natives below (every
100 ms: what to play, how far it is, and that it ended).
*/

#include "host.h"

#include <jni.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

enum
{
	_movie_none = 0,
	_movie_intro,
	_movie_credits,
	_movie_attract1,
	_movie_attract2,
	_movie_attract3,
	_movie_count,
};

/* the part of the requested path that names the movie, the asset the player
opens, and the frames of the transcoded file (an estimate: the frame the
engine reads is taken from the player's position and duration) */
static struct
{
	const char *match;
	const char *file;
	unsigned long width;
	unsigned long height;
	unsigned long frames;
	int requested;
	int playing;
	int finished;
	unsigned long position_ms;
	unsigned long duration_ms;
} movies[_movie_count] = {
	{ "",         "",                   0,   0,   0,    0, 0, 0,  0, 0 },
	{ "intro", "intro_es.mp4", 640, 480, 481, 0, 0, 0, 0, 0 },
	{ "credits", "credits_es.mp4", 640, 480, 5214, 0, 0, 0, 0, 0 },
	{ "attract1", "attract1_es.mp4", 640, 480, 4538, 0, 0, 0, 0, 0 },
	{ "attract2", "attract2_es.mp4", 640, 480, 3982, 0, 0, 0, 0, 0 },
	{ "attract3", "attract3_es.mp4", 640, 480, 2042, 0, 0, 0, 0, 0 },
};

static pthread_mutex_t movie_lock = PTHREAD_MUTEX_INITIALIZER;

/* the player told us its view is attached and it can play */
static int movie_player_ready;

/* when the movie was asked for (a player that never starts one, its file
missing, would leave the engine on a black screen for ever) */
static int64_t movie_opened_ms;

/* the host has no game clock: its own is enough for the guard below */
static int64_t movie_now_ms(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int movie_from_name(const char *name)
{
	int movie;

	if (!name || !name[0])
		return _movie_none;

	for (movie = 1; movie < _movie_count; movie++)
	{
		if (strstr(name, movies[movie].match))
			return movie;
	}

	return _movie_none;
}

int host_movie_open(const char *name)
{
	int movie = movie_from_name(name);

	if (!movie || !movie_player_ready)
		return 0;

	pthread_mutex_lock(&movie_lock);
	movies[movie].requested = 1;
	movies[movie].playing = 1;
	movies[movie].finished = 0;
	movies[movie].position_ms = 0;
	pthread_mutex_unlock(&movie_lock);
	movie_opened_ms = movie_now_ms();

	return movie;
}

unsigned long host_movie_width(int movie)
{
	return ((movie) > 0 && (movie) < _movie_count) ? movies[movie].width : 0;
}

unsigned long host_movie_height(int movie)
{
	return ((movie) > 0 && (movie) < _movie_count) ? movies[movie].height : 0;
}

unsigned long host_movie_frames(int movie)
{
	return ((movie) > 0 && (movie) < _movie_count) ? movies[movie].frames : 0;
}

/* the frame the player is on: the engine ends the movie when it reaches the
last one (bink_playback.c, its update) */
unsigned long host_movie_frame(int movie)
{
	unsigned long frame = 0;

	if (!((movie) > 0 && (movie) < _movie_count))
		return 0;

	pthread_mutex_lock(&movie_lock);
	/* (the player never started it: a missing file, or a device that
	refused it. The movie is over as far as the engine is concerned) */
	if (!movies[movie].finished && movies[movie].position_ms == 0 &&
		movie_opened_ms && movie_now_ms() - movie_opened_ms > 5000)
	{
		movies[movie].finished = 1;
		movies[movie].playing = 0;
		movies[movie].requested = 0;
		host_logf(HOST_LOG_INFO, "movie: the player never started it; skipped");
	}
	if (movies[movie].finished)
	{
		frame = movies[movie].frames - 1;
	}
	else if (movies[movie].duration_ms && movies[movie].position_ms < movies[movie].duration_ms)
	{
		frame = movies[movie].frames * movies[movie].position_ms / movies[movie].duration_ms;
	}
	pthread_mutex_unlock(&movie_lock);

	return frame;
}

int host_movie_finished(int movie)
{
	int finished;

	if (!((movie) > 0 && (movie) < _movie_count))
		return 1;

	pthread_mutex_lock(&movie_lock);
	finished = movies[movie].finished;
	pthread_mutex_unlock(&movie_lock);

	return finished;
}

void host_movie_close(int movie)
{
	if (!((movie) > 0 && (movie) < _movie_count))
		return;

	pthread_mutex_lock(&movie_lock);
	movies[movie].requested = 0;
	movies[movie].playing = 0;
	pthread_mutex_unlock(&movie_lock);
}

/* ---------- what the player (MoviePlayer.java) calls */

JNIEXPORT jint JNICALL Java_com_halo_decomp_MoviePlayer_nativePoll(JNIEnv *env, jclass cls)
{
	int movie;

	(void)env;
	(void)cls;

	for (movie = 1; movie < _movie_count; movie++)
	{
		int wanted;

		pthread_mutex_lock(&movie_lock);
		wanted = movies[movie].requested && !movies[movie].playing;
		pthread_mutex_unlock(&movie_lock);

		if (wanted)
			return movie;
	}

	/* (a movie the guest opened: the player starts it) */
	for (movie = 1; movie < _movie_count; movie++)
	{
		int wanted;

		pthread_mutex_lock(&movie_lock);
		wanted = movies[movie].requested;
		pthread_mutex_unlock(&movie_lock);

		if (wanted)
			return movie;
	}

	return _movie_none;
}

JNIEXPORT void JNICALL Java_com_halo_decomp_MoviePlayer_nativeReady(JNIEnv *env, jclass cls, jboolean ready)
{
	(void)env;
	(void)cls;

	movie_player_ready = ready ? 1 : 0;
}

JNIEXPORT void JNICALL Java_com_halo_decomp_MoviePlayer_nativeStarted(JNIEnv *env, jclass cls, jint movie)
{
	(void)env;
	(void)cls;

	if (!((movie) > 0 && (movie) < _movie_count))
		return;

	pthread_mutex_lock(&movie_lock);
	movies[movie].playing = 1;
	pthread_mutex_unlock(&movie_lock);
}

JNIEXPORT void JNICALL Java_com_halo_decomp_MoviePlayer_nativeProgress(JNIEnv *env, jclass cls,
	jint movie, jint position_ms, jint duration_ms)
{
	(void)env;
	(void)cls;

	if (!((movie) > 0 && (movie) < _movie_count))
		return;

	pthread_mutex_lock(&movie_lock);
	movies[movie].position_ms = (unsigned long)position_ms;
	if (duration_ms > 0)
		movies[movie].duration_ms = (unsigned long)duration_ms;
	pthread_mutex_unlock(&movie_lock);
}

JNIEXPORT void JNICALL Java_com_halo_decomp_MoviePlayer_nativeFinished(JNIEnv *env, jclass cls, jint movie)
{
	(void)env;
	(void)cls;

	if (!((movie) > 0 && (movie) < _movie_count))
		return;

	pthread_mutex_lock(&movie_lock);
	movies[movie].finished = 1;
	movies[movie].playing = 0;
	movies[movie].requested = 0;
	pthread_mutex_unlock(&movie_lock);
}
