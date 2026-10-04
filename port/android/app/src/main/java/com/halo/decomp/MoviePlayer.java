package com.halo.decomp;

import android.content.Context;
import android.content.res.AssetFileDescriptor;
import android.media.MediaPlayer;
import android.os.Handler;
import android.os.Looper;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewGroup;

import java.io.IOException;

/**
 * The movies the game asks for. The port has no Bink decoder
 * (port/linux/src/bink_null.c hands them to this side), so the device's own
 * player plays the port's transcoded file over the game's surface, and the
 * native side (port/android/host/host_movie.c) is told which frame it is on:
 * the game's playback loop ends the movie when it reaches the last one, and
 * stops it when a button skips it (the game closes the movie, the poll below
 * finds nothing asked for and the player is stopped).
 */
final class MoviePlayer implements SurfaceHolder.Callback, MediaPlayer.OnCompletionListener, Runnable {
    private static native int nativePoll();
    private static native void nativeReady(boolean ready);
    private static native void nativeStarted(int movie);
    private static native void nativeProgress(int movie, int positionMs, int durationMs);
    private static native void nativeFinished(int movie);

    /* the ids the native side uses (host_movie.c) */
    private static final String[] FILES = {
        "", "intro_es.mp4", "credits_es.mp4", "attract1_es.mp4", "attract2_es.mp4", "attract3_es.mp4"
    };

    private final Context context;
    private final SurfaceView view;
    private final View overlay;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private MediaPlayer player;
    private int movie;
    private boolean surfaceReady;
    private boolean starting;

    MoviePlayer(Context context, ViewGroup layout, View overlay) {
        this.context = context;
        this.overlay = overlay;
        view = new SurfaceView(context);
        /* over the game's own surface (SDL's) */
        view.setZOrderMediaOverlay(true);
        view.getHolder().addCallback(this);
        view.setVisibility(View.GONE);
        layout.addView(view, new ViewGroup.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        handler.postDelayed(this, 100);
    }

    /* SurfaceHolder.Callback */
    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        surfaceReady = true;
        nativeReady(true);
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        surfaceReady = false;
        nativeReady(false);
    }

    @Override
    public void onCompletion(MediaPlayer finished) {
        int done = movie;
        stopMovie();
        if (done != 0) nativeFinished(done);
    }

    /** what the game asks for, and how far the player is, every 100 ms */
    @Override
    public void run() {
        int wanted = nativePoll();

        if (wanted == 0) {
            /* the game dropped the movie (it ended, or a button skipped it) */
            if (player != null) stopMovie();
        } else if (wanted != movie && !starting) {
            startMovie(wanted);
        }

        if (player != null) {
            try {
                nativeProgress(movie, player.getCurrentPosition(), player.getDuration());
            } catch (IllegalStateException ignored) {
            }
        }

        handler.postDelayed(this, 100);
    }

    private void startMovie(int wanted) {
        if (wanted <= 0 || wanted >= FILES.length || !surfaceReady) return;

        AssetFileDescriptor file = null;
        starting = true;
        try {
            file = context.getAssets().openFd("cinematics/" + FILES[wanted]);
            MediaPlayer started = new MediaPlayer();
            started.setDataSource(file.getFileDescriptor(), file.getStartOffset(), file.getLength());
            started.setSurface(view.getHolder().getSurface());
            started.setOnCompletionListener(this);
            started.prepare();
            started.start();
            player = started;
            movie = wanted;
            view.setVisibility(View.VISIBLE);
            if (overlay != null) overlay.setVisibility(View.INVISIBLE);
            nativeStarted(wanted);
        } catch (IOException | IllegalArgumentException | IllegalStateException e) {
            stopMovie();
        } finally {
            starting = false;
            if (file != null) {
                try {
                    file.close();
                } catch (IOException ignored) {
                }
            }
        }
    }

    private void stopMovie() {
        if (player != null) {
            try {
                player.stop();
            } catch (IllegalStateException ignored) {
            }
            player.release();
            player = null;
        }
        movie = 0;
        view.setVisibility(View.GONE);
        if (overlay != null) overlay.setVisibility(View.VISIBLE);
    }
}
