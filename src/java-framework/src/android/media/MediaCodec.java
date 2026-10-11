package android.media;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.os.Handler;
import android.os.Looper;
import android.view.Surface;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayDeque;

/**
 * A decoder (husk.MediaCodecNative). Input is decoded as it is queued, on the queuing thread; outputs wait in order for
 * dequeueOutputBuffer, or go to the Callback on its handler (the async mode ExoPlayer uses). A video frame released with render
 * is drawn into the output Surface (a SurfaceView's software buffer), scaled to it. No encoders, no DRM.
 */
@SuppressWarnings({"unchecked", "rawtypes", "deprecation"})
public final class MediaCodec {
    public static final int BUFFER_FLAG_CODEC_CONFIG = 2;
    public static final int BUFFER_FLAG_DECODE_ONLY = 32;
    public static final int BUFFER_FLAG_END_OF_STREAM = 4;
    public static final int BUFFER_FLAG_KEY_FRAME = 1;
    public static final int BUFFER_FLAG_MUXER_DATA = 16;
    public static final int BUFFER_FLAG_PARTIAL_FRAME = 8;
    public static final int BUFFER_FLAG_SYNC_FRAME = 1;
    public static final int CONFIGURE_FLAG_DETACHED_SURFACE = 8;
    public static final int CONFIGURE_FLAG_ENCODE = 1;
    public static final int CONFIGURE_FLAG_USE_BLOCK_MODEL = 2;
    public static final int CONFIGURE_FLAG_USE_CRYPTO_ASYNC = 4;
    public static final int CRYPTO_MODE_AES_CBC = 2;
    public static final int CRYPTO_MODE_AES_CTR = 1;
    public static final int CRYPTO_MODE_UNENCRYPTED = 0;
    public static final int INFO_OUTPUT_BUFFERS_CHANGED = -3;
    public static final int INFO_OUTPUT_FORMAT_CHANGED = -2;
    public static final int INFO_TRY_AGAIN_LATER = -1;
    public static final java.lang.String PARAMETER_KEY_HDR10_PLUS_INFO = "hdr10-plus-info";
    public static final java.lang.String PARAMETER_KEY_LOW_LATENCY = "low-latency";
    public static final java.lang.String PARAMETER_KEY_OFFSET_TIME = "time-offset-us";
    public static final java.lang.String PARAMETER_KEY_QP_OFFSET_MAP = "qp-offset-map";
    public static final java.lang.String PARAMETER_KEY_QP_OFFSET_RECTS = "qp-offset-rects";
    public static final java.lang.String PARAMETER_KEY_REQUEST_SYNC_FRAME = "request-sync";
    public static final java.lang.String PARAMETER_KEY_SUSPEND = "drop-input-frames";
    public static final java.lang.String PARAMETER_KEY_SUSPEND_TIME = "drop-start-time-us";
    public static final java.lang.String PARAMETER_KEY_TUNNEL_PEEK = "tunnel-peek";
    public static final java.lang.String PARAMETER_KEY_VIDEO_BITRATE = "video-bitrate";
    public static final int VIDEO_SCALING_MODE_SCALE_TO_FIT = 1;
    public static final int VIDEO_SCALING_MODE_SCALE_TO_FIT_WITH_CROPPING = 2;
    private static final int INPUTS = 8, ST_UNINITIALIZED = 0, ST_CONFIGURED = 1, ST_RUNNING = 2, ST_RELEASED = 3;
    private static final class Out { int index = -1; long pts; int flags, size; long frame; ByteBuffer data; }
    private final String mName, mMime;
    private final boolean mVideo;
    private long mNative;
    private int mState = ST_UNINITIALIZED, mGeneration, mScaling = VIDEO_SCALING_MODE_SCALE_TO_FIT;
    private final Object mLock = new Object();
    private Surface mSurface;
    private MediaFormat mInputFormat, mOutputFormat;
    private ByteBuffer[] mInputs;
    private final ArrayDeque<Integer> mFreeInputs = new ArrayDeque<>();
    private final ArrayDeque<Out> mQueue = new ArrayDeque<>();
    private Out[] mSlots = new Out[16];
    private boolean mFormatPending, mStartPending;
    private int mOutW, mOutH, mRate, mChannels;
    private Callback mCallback; private Handler mCallbackHandler;
    private OnFrameRenderedListener mRendered; private Handler mRenderedHandler;
    private Bitmap mFrame;
    private final Paint mPaint = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final long[] mInfo = new long[3];

    private MediaCodec(String name, String mime) throws IOException {
        mName = name; mMime = mime; mVideo = mime.startsWith("video/");
        mNative = husk.MediaCodecNative.create(mime);
        if (mNative == 0) throw new IOException("Failed to initialize " + name + ", error 0xfffffff4");
    }
    protected MediaCodec() { mName = ""; mMime = ""; mVideo = false; }
    public static MediaCodec createByCodecName(String name) throws IOException {
        MediaCodecInfo i = MediaCodecList.getInfoFor(name);
        if (i == null) throw new IllegalArgumentException("Failed to initialize " + name + ", error 0xfffffffe");
        if (i.isEncoder()) throw new IOException("Husk has no encoder " + name);
        return new MediaCodec(name, i.getSupportedTypes()[0]);
    }
    public static MediaCodec createByCodecNameForClient(String name, int pid, int uid) throws IOException { return createByCodecName(name); }
    public static MediaCodec createDecoderByType(String type) throws IOException {
        if (type == null) throw new NullPointerException();
        for (MediaCodecInfo i : MediaCodecList.infos()) for (String t : i.getSupportedTypes()) if (t.equalsIgnoreCase(type)) return new MediaCodec(i.getName(), t);
        throw new IOException("Failed to create a decoder for " + type + " (Husk decodes H.264, HEVC, AAC and MP3)");
    }
    public static MediaCodec createEncoderByType(String type) throws IOException { throw new IOException("Husk has no " + type + " encoder"); }
    public static Surface createPersistentInputSurface() { throw new IllegalArgumentException("Husk has no encoders"); }
    public static java.util.List getGloballyAvailableResources() { return new java.util.ArrayList(); }
    public static android.media.Image mapHardwareBuffer(android.hardware.HardwareBuffer b) { return null; }

    private static byte[] bytes(ByteBuffer b) {
        if (b == null) return null;
        ByteBuffer d = b.duplicate(); d.position(0); d.limit(b.limit());
        byte[] r = new byte[d.remaining()]; d.get(r); return r;
    }
    public void configure(MediaFormat format, Surface surface, MediaCrypto crypto, int flags) {
        synchronized (mLock) {
            if (mState != ST_UNINITIALIZED) throw new IllegalStateException("configure in state " + mState);
            if ((flags & CONFIGURE_FLAG_ENCODE) != 0) throw new IllegalStateException("Husk has no encoders");
            MediaFormat f = format != null ? new MediaFormat(format) : new MediaFormat();
            mInputFormat = f; mSurface = surface;
            int w = f.getInteger(MediaFormat.KEY_WIDTH, 0), h = f.getInteger(MediaFormat.KEY_HEIGHT, 0);
            int rate = f.getInteger(MediaFormat.KEY_SAMPLE_RATE, 0), ch = f.getInteger(MediaFormat.KEY_CHANNEL_COUNT, 0);
            if (!husk.MediaCodecNative.configure(mNative, w, h, rate, ch, bytes(f.getByteBuffer("csd-0")), bytes(f.getByteBuffer("csd-1")), bytes(f.getByteBuffer("csd-2"))))
                throw new CodecException(0x80001000, "the codec refused the format " + format);
            int max = f.getInteger(MediaFormat.KEY_MAX_INPUT_SIZE, 0);
            if (max <= 0) max = mVideo ? Math.max(1 << 20, w * h * 3 / 2) : 1 << 16;
            mInputs = new ByteBuffer[INPUTS];
            for (int i = 0; i < INPUTS; i++) mInputs[i] = ByteBuffer.allocateDirect(max).order(ByteOrder.nativeOrder());
            if (mVideo) {
                int s = husk.MediaCodecNative.size(mNative);
                mOutW = s > 0 ? s >>> 16 : w; mOutH = s > 0 ? s & 0xffff : h;
                mOutputFormat = videoFormat(mOutW, mOutH);
            } else {
                mRate = rate > 0 ? rate : 44100; mChannels = ch > 0 ? ch : 2;
                mOutputFormat = audioFormat(mRate, mChannels);
            }
            mState = ST_CONFIGURED;
        }
    }
    public void configure(MediaFormat format, Surface surface, int flags, MediaDescrambler d) { configure(format, surface, (MediaCrypto) null, flags); }
    private static MediaFormat videoFormat(int w, int h) {
        MediaFormat f = MediaFormat.createVideoFormat("video/raw", w, h);
        f.setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface);
        f.setInteger("crop-left", 0); f.setInteger("crop-top", 0); f.setInteger("crop-right", w - 1); f.setInteger("crop-bottom", h - 1);
        f.setInteger("stride", w); f.setInteger("slice-height", h);
        return f;
    }
    private static MediaFormat audioFormat(int rate, int ch) {
        MediaFormat f = MediaFormat.createAudioFormat("audio/raw", rate, ch);
        f.setInteger("pcm-encoding", AudioFormat.ENCODING_PCM_16BIT);
        return f;
    }
    public void start() {
        int gen; Integer[] inputs;
        synchronized (mLock) {
            if (mState == ST_UNINITIALIZED || mState == ST_RELEASED) throw new IllegalStateException("start in state " + mState);
            boolean fresh = mState == ST_CONFIGURED;
            mState = ST_RUNNING;
            if (fresh) { mFreeInputs.clear(); for (int i = 0; i < INPUTS; i++) mFreeInputs.add(i); }
            else if (!mStartPending) return;
            mStartPending = false;
            if (mCallback == null) return;
            gen = mGeneration; inputs = mFreeInputs.toArray(new Integer[0]);
        }
        for (Integer i : inputs) postInput(gen, i);
    }
    private void requireRunning() { if (mState != ST_RUNNING) throw new IllegalStateException("not executing (state " + mState + ")"); }
    public int dequeueInputBuffer(long timeoutUs) {
        synchronized (mLock) {
            requireRunning();
            if (mCallback != null) throw new IllegalStateException("dequeueInputBuffer in async mode");
            long end = System.nanoTime() + timeoutUs * 1000;
            while (mFreeInputs.isEmpty()) {
                if (timeoutUs == 0) return INFO_TRY_AGAIN_LATER;
                long left = timeoutUs < 0 ? 100 : (end - System.nanoTime()) / 1000000;
                if (left <= 0) return INFO_TRY_AGAIN_LATER;
                try { mLock.wait(left); } catch (InterruptedException e) { return INFO_TRY_AGAIN_LATER; }
                requireRunning();
            }
            return mFreeInputs.poll();
        }
    }
    public ByteBuffer getInputBuffer(int i) { if (mInputs == null || i < 0 || i >= mInputs.length) return null; ByteBuffer b = mInputs[i]; b.clear(); return b; }
    public ByteBuffer[] getInputBuffers() { if (mInputs != null) for (ByteBuffer b : mInputs) b.clear(); return mInputs; }
    public android.media.Image getInputImage(int i) { return null; }
    public void queueInputBuffer(int index, int offset, int size, long ptsUs, int flags) {
        if (index < 0 || index >= INPUTS) throw new IllegalArgumentException("bad input index " + index);
        synchronized (mLock) { requireRunning(); }
        byte[] data = new byte[Math.max(0, size)];
        if (size > 0) { ByteBuffer d = mInputs[index].duplicate(); d.clear(); d.position(offset); d.get(data, 0, size); }
        decode(data, size, ptsUs, flags);
        int gen;
        synchronized (mLock) { mFreeInputs.add(index); mLock.notifyAll(); gen = mGeneration; }
        if (mCallback != null) postInput(gen, index);
    }
    public void queueSecureInputBuffer(int index, int offset, CryptoInfo info, long ptsUs, int flags) {
        if (info != null && info.mode != CRYPTO_MODE_UNENCRYPTED) throw new CryptoException(CryptoException.ERROR_UNSUPPORTED_OPERATION, "Husk cannot decrypt protected media");
        int size = 0;
        if (info != null && info.numBytesOfClearData != null) for (int i = 0; i < info.numSubSamples; i++) size += info.numBytesOfClearData[i] + (info.numBytesOfEncryptedData != null ? info.numBytesOfEncryptedData[i] : 0);
        queueInputBuffer(index, offset, size, ptsUs, flags);
    }
    public void queueInputBuffers(int index, ArrayDeque infos) {
        for (Object o : infos) { BufferInfo b = (BufferInfo) o; queueInputBuffer(index, b.offset, b.size, b.presentationTimeUs, b.flags); }
    }
    public void queueSecureInputBuffers(int index, ArrayDeque infos, ArrayDeque cryptos) { throw new CryptoException(CryptoException.ERROR_UNSUPPORTED_OPERATION, "Husk cannot decrypt protected media"); }
    private void decode(byte[] data, int size, long pts, int flags) {
        husk.MediaCodecNative.decode(mNative, data, size, pts, flags);
        collect();
        if ((flags & BUFFER_FLAG_END_OF_STREAM) != 0) { Out e = new Out(); e.pts = pts; e.flags = BUFFER_FLAG_END_OF_STREAM; emit(e, false); }
    }
    private void collect() {
        for (;;) {
            Out o = new Out();
            boolean changed = false;
            if (mVideo) {
                long f = husk.MediaCodecNative.nextFrame(mNative, mInfo);
                if (f == 0) return;
                o.frame = f; o.pts = mInfo[0];
                int w = (int) mInfo[1], h = (int) mInfo[2];
                o.size = Math.max(1, w * h * 3 / 2);
                if (w != mOutW || h != mOutH || mFormatPendingFirst) { mOutW = w; mOutH = h; mOutputFormat = videoFormat(w, h); changed = true; }
            } else {
                byte[] p = husk.MediaCodecNative.nextPcm(mNative, mInfo);
                if (p == null) return;
                o.data = ByteBuffer.allocateDirect(p.length).order(ByteOrder.nativeOrder()); o.data.put(p); o.data.flip();
                o.size = p.length; o.pts = mInfo[0];
                if ((int) mInfo[1] != mRate || (int) mInfo[2] != mChannels || mFormatPendingFirst) { mRate = (int) mInfo[1]; mChannels = (int) mInfo[2]; mOutputFormat = audioFormat(mRate, mChannels); changed = true; }
            }
            mFormatPendingFirst = false;
            emit(o, changed);
        }
    }
    /* Android reports the output format once before the first output */
    private boolean mFormatPendingFirst = true;
    private void emit(Out o, boolean formatChanged) {
        int gen; MediaFormat fmt;
        synchronized (mLock) {
            int i = 0; while (i < mSlots.length && mSlots[i] != null) i++;
            if (i == mSlots.length) mSlots = java.util.Arrays.copyOf(mSlots, mSlots.length * 2);
            o.index = i; mSlots[i] = o;
            if (formatChanged && mCallback == null) mFormatPending = true;
            if (mCallback == null) { mQueue.add(o); mLock.notifyAll(); return; }
            gen = mGeneration; fmt = mOutputFormat;
        }
        final MediaFormat f = fmt; final Out out = o; final int g = gen;
        if (formatChanged) postCallback(g, () -> mCallback.onOutputFormatChanged(this, f));
        postCallback(g, () -> { BufferInfo bi = new BufferInfo(); bi.set(0, out.size, out.pts, out.flags); mCallback.onOutputBufferAvailable(this, out.index, bi); });
    }
    private void postInput(int gen, int index) { postCallback(gen, () -> mCallback.onInputBufferAvailable(this, index)); }
    private void postCallback(int gen, Runnable r) {
        Handler h = mCallbackHandler;
        if (h == null) return;
        h.post(() -> { synchronized (mLock) { if (gen != mGeneration || mState != ST_RUNNING || mCallback == null) return; } r.run(); });
    }
    public int dequeueOutputBuffer(BufferInfo info, long timeoutUs) {
        synchronized (mLock) {
            requireRunning();
            if (mCallback != null) throw new IllegalStateException("dequeueOutputBuffer in async mode");
            long end = System.nanoTime() + timeoutUs * 1000;
            for (;;) {
                if (mFormatPending) { mFormatPending = false; return INFO_OUTPUT_FORMAT_CHANGED; }
                Out o = mQueue.poll();
                if (o != null) { info.set(0, o.size, o.pts, o.flags); return o.index; }
                if (timeoutUs == 0) return INFO_TRY_AGAIN_LATER;
                long left = timeoutUs < 0 ? 100 : (end - System.nanoTime()) / 1000000;
                if (left <= 0) return INFO_TRY_AGAIN_LATER;
                try { mLock.wait(left); } catch (InterruptedException e) { return INFO_TRY_AGAIN_LATER; }
                requireRunning();
            }
        }
    }
    private Out slot(int i) { synchronized (mLock) { return i >= 0 && i < mSlots.length ? mSlots[i] : null; } }
    public ByteBuffer getOutputBuffer(int i) {
        Out o = slot(i);
        if (o == null || o.data == null) return null;
        ByteBuffer b = o.data.duplicate().order(ByteOrder.nativeOrder()); b.position(0); b.limit(o.size); return b;
    }
    public ByteBuffer[] getOutputBuffers() {
        synchronized (mLock) { ByteBuffer[] r = new ByteBuffer[mSlots.length]; for (int i = 0; i < r.length; i++) r[i] = mSlots[i] != null && mSlots[i].data != null ? mSlots[i].data : ByteBuffer.allocateDirect(0); return r; }
    }
    public android.media.Image getOutputImage(int i) { return null; }
    public MediaFormat getOutputFormat() { synchronized (mLock) { if (mState == ST_UNINITIALIZED) throw new IllegalStateException(); return new MediaFormat(mOutputFormat); } }
    public MediaFormat getOutputFormat(int i) { return getOutputFormat(); }
    public MediaFormat getInputFormat() { synchronized (mLock) { if (mState == ST_UNINITIALIZED) throw new IllegalStateException(); return new MediaFormat(mInputFormat); } }
    public void releaseOutputBuffer(int index, boolean render) { release(index, render, 0); }
    public void releaseOutputBuffer(int index, long renderTimestampNs) { release(index, true, renderTimestampNs); }
    private void release(int index, boolean render, long whenNs) {
        Out o;
        synchronized (mLock) {
            if (mState != ST_RUNNING) throw new IllegalStateException("releaseOutputBuffer when not executing");
            if (index < 0 || index >= mSlots.length || mSlots[index] == null) throw new IllegalStateException("output buffer " + index + " is not dequeued");
            o = mSlots[index]; mSlots[index] = null;
        }
        if (o.frame == 0) return;
        if (TRACE && !render) mSkipped++;
        long t0 = TRACE ? System.nanoTime() : 0;
        try { if (render) draw(o); } finally { husk.MediaCodecNative.freeFrame(o.frame); }
        if (TRACE && render) mDrawNs += System.nanoTime() - t0;
    }
    private static final boolean TRACE = System.getenv("TL_MEDIA_TRACE") != null;
    private int mDrawn, mSkipped;
    private long mDrawNs;
    private void draw(Out o) {
        Surface s = mSurface;
        if (TRACE && mDrawn++ % 30 == 0) android.util.Log.d("MediaCodec", mName + ": frame " + mDrawn + " at " + o.pts + " (" + mSkipped + " skipped, " + (mDrawn > 1 ? mDrawNs / 1000000 / (mDrawn - 1) : 0) + " ms a draw) " + mOutW + "x" + mOutH + " to " + (s != null ? s.huskView : null));
        if (s == null || !s.isValid() || mOutW <= 0 || mOutH <= 0) return;
        if (mFrame == null || mFrame.getWidth() != mOutW || mFrame.getHeight() != mOutH) mFrame = Bitmap.createBitmap(mOutW, mOutH, Bitmap.Config.ARGB_8888);
        if (!husk.MediaCodecNative.renderFrame(o.frame, mFrame)) return;
        Canvas c;
        try { c = s.lockCanvas(null); } catch (RuntimeException e) { return; }
        if (c == null) return;
        try {
            int cw = c.getWidth(), ch = c.getHeight();
            Rect dst = new Rect(0, 0, cw, ch);
            if (mScaling == VIDEO_SCALING_MODE_SCALE_TO_FIT_WITH_CROPPING) {
                float sc = Math.max((float) cw / mOutW, (float) ch / mOutH);
                int w = Math.round(mOutW * sc), h = Math.round(mOutH * sc);
                dst.set((cw - w) / 2, (ch - h) / 2, (cw + w) / 2, (ch + h) / 2);
            }
            c.drawColor(0xFF000000);
            c.drawBitmap(mFrame, null, dst, mPaint);
        } finally { s.unlockCanvasAndPost(c); }
        OnFrameRenderedListener l = mRendered;
        if (l != null) {
            final long pts = o.pts, now = System.nanoTime();
            Handler h = mRenderedHandler;
            if (h != null) h.post(() -> l.onFrameRendered(this, pts, now)); else l.onFrameRendered(this, pts, now);
        }
    }
    private void dropOutputs() {
        java.util.ArrayList<Out> drop = new java.util.ArrayList<>();
        synchronized (mLock) {
            for (int i = 0; i < mSlots.length; i++) if (mSlots[i] != null) { drop.add(mSlots[i]); mSlots[i] = null; }
            mQueue.clear();
        }
        for (Out o : drop) if (o.frame != 0) husk.MediaCodecNative.freeFrame(o.frame);
    }
    public void flush() {
        synchronized (mLock) {
            if (mState != ST_RUNNING) throw new IllegalStateException("flush when not executing");
            mGeneration++;
        }
        husk.MediaCodecNative.flush(mNative);
        dropOutputs();
        synchronized (mLock) {
            mFreeInputs.clear(); for (int i = 0; i < INPUTS; i++) mFreeInputs.add(i);
            if (mCallback != null) mStartPending = true;
            mLock.notifyAll();
        }
    }
    public void stop() {
        synchronized (mLock) { if (mState == ST_RELEASED) throw new IllegalStateException(); mGeneration++; }
        if (mNative != 0) husk.MediaCodecNative.flush(mNative);
        dropOutputs();
        synchronized (mLock) { if (mState == ST_RUNNING) mState = ST_CONFIGURED; mStartPending = false; mLock.notifyAll(); }
    }
    public void reset() {
        stop();
        synchronized (mLock) {
            if (mNative != 0) husk.MediaCodecNative.destroy(mNative);
            mNative = husk.MediaCodecNative.create(mMime);
            mState = ST_UNINITIALIZED; mCallback = null; mCallbackHandler = null; mFormatPending = false; mFormatPendingFirst = true;
        }
    }
    public void release() {
        synchronized (mLock) { if (mState == ST_RELEASED) return; mGeneration++; mState = ST_RELEASED; mLock.notifyAll(); }
        dropOutputs();
        long n; synchronized (mLock) { n = mNative; mNative = 0; }
        if (n != 0) husk.MediaCodecNative.destroy(n);
        mFrame = null;
    }
    protected void finalize() { release(); }
    public void setCallback(Callback cb, Handler h) {
        synchronized (mLock) {
            if (mState != ST_UNINITIALIZED && mState != ST_CONFIGURED) throw new IllegalStateException("setCallback after start");
            mCallback = cb;
            mCallbackHandler = cb == null ? null : h != null ? h : new Handler(Looper.myLooper() != null ? Looper.myLooper() : Looper.getMainLooper());
        }
    }
    public void setCallback(Callback cb) { setCallback(cb, null); }
    public void setOnFrameRenderedListener(OnFrameRenderedListener l, Handler h) { mRendered = l; mRenderedHandler = h; }
    public void setOnFirstTunnelFrameReadyListener(Handler h, OnFirstTunnelFrameReadyListener l) {}
    public void setOutputSurface(Surface s) { if (!mVideo) throw new IllegalStateException("not a video codec"); mSurface = s; }
    public void detachOutputSurface() { mSurface = null; }
    public void setVideoScalingMode(int mode) { mScaling = mode; }
    public void setParameters(android.os.Bundle b) {}
    public void setAudioPresentation(AudioPresentation p) {}
    public void setInputSurface(Surface s) { throw new IllegalStateException("Husk has no encoders"); }
    public Surface createInputSurface() { throw new IllegalStateException("Husk has no encoders"); }
    public void signalEndOfInputStream() { throw new IllegalStateException("Husk has no encoders"); }
    public String getName() { return mName; }
    public String getCanonicalName() { return mName; }
    public MediaCodecInfo getCodecInfo() { return MediaCodecList.getInfoFor(mName); }
    public android.os.PersistableBundle getMetrics() {
        android.os.PersistableBundle b = new android.os.PersistableBundle();
        b.putString(MetricsConstants.CODEC, mName); b.putString(MetricsConstants.MIME_TYPE, mMime); b.putString(MetricsConstants.MODE, mVideo ? MetricsConstants.MODE_VIDEO : MetricsConstants.MODE_AUDIO);
        b.putInt(MetricsConstants.ENCODER, 0); b.putInt(MetricsConstants.WIDTH, mOutW); b.putInt(MetricsConstants.HEIGHT, mOutH);
        return b;
    }
    public OutputFrame getOutputFrame(int i) { return null; }
    public ParameterDescriptor getParameterDescriptor(String n) { return null; }
    public QueueRequest getQueueRequest(int i) { throw new IllegalStateException("block model is not supported"); }
    public java.util.List getRequiredResources() { return new java.util.ArrayList(); }
    public java.util.List getSupportedVendorParameters() { return new java.util.ArrayList(); }
    public void subscribeToVendorParameters(java.util.List l) {}
    public void unsubscribeFromVendorParameters(java.util.List l) {}

    public static final class BufferInfo {
        public int offset, size, flags;
        public long presentationTimeUs;
        public BufferInfo() {}
        public void set(int newOffset, int newSize, long newTimeUs, int newFlags) { offset = newOffset; size = newSize; presentationTimeUs = newTimeUs; flags = newFlags; }
        public BufferInfo dup() { BufferInfo b = new BufferInfo(); b.set(offset, size, presentationTimeUs, flags); return b; }
    }
    public static abstract class Callback {
        public Callback() {}
        public abstract void onInputBufferAvailable(MediaCodec codec, int index);
        public abstract void onOutputBufferAvailable(MediaCodec codec, int index, BufferInfo info);
        public abstract void onError(MediaCodec codec, CodecException e);
        public abstract void onOutputFormatChanged(MediaCodec codec, MediaFormat format);
        public void onCryptoError(MediaCodec codec, CryptoException e) {}
        public void onMetricsFlushed(MediaCodec codec, android.os.PersistableBundle metrics) {}
        public void onOutputBuffersAvailable(MediaCodec codec, int index, ArrayDeque infos) {}
        public void onRequiredResourcesChanged(MediaCodec codec) {}
    }
    public static final class CodecException extends IllegalStateException {
        public static final int ERROR_INSUFFICIENT_RESOURCE = 1100, ERROR_RECLAIMED = 1101;
        private final int mCode;
        CodecException(int code, String msg) { super(msg); mCode = code; }
        protected CodecException() { this(0, null); }
        public boolean isTransient() { return false; }
        public boolean isRecoverable() { return false; }
        public int getErrorCode() { return mCode; }
        public String getDiagnosticInfo() { return "android.media.MediaCodec.error_" + (mCode < 0 ? "neg_" : "") + Math.abs(mCode); }
    }
    public static final class CryptoException extends RuntimeException implements MediaDrmThrowable {
        public static final int ERROR_NO_KEY = 1, ERROR_KEY_EXPIRED = 2, ERROR_RESOURCE_BUSY = 3, ERROR_INSUFFICIENT_OUTPUT_PROTECTION = 4, ERROR_SESSION_NOT_OPENED = 5,
            ERROR_UNSUPPORTED_OPERATION = 6, ERROR_INSUFFICIENT_SECURITY = 7, ERROR_FRAME_TOO_LARGE = 8, ERROR_LOST_STATE = 9;
        private final int mCode;
        private CryptoInfo mInfo;
        public CryptoException(int code, String msg) { super(msg); mCode = code; }
        public CryptoException(String msg, int code, int vendor, int oem, int context, CryptoInfo info) { super(msg); mCode = code; mInfo = info; }
        public int getErrorCode() { return mCode; }
        public CryptoInfo getCryptoInfo() { return mInfo; }
        public int getVendorError() { return 0; }
        public int getOemError() { return 0; }
        public int getErrorContext() { return 0; }
    }
    public static final class CryptoInfo {
        public int numSubSamples;
        public int[] numBytesOfClearData, numBytesOfEncryptedData;
        public byte[] key, iv;
        public int mode;
        private Pattern mPattern = new Pattern(0, 0);
        public CryptoInfo() {}
        public void set(int subSamples, int[] clear, int[] encrypted, byte[] key, byte[] iv, int mode) {
            numSubSamples = subSamples; numBytesOfClearData = clear; numBytesOfEncryptedData = encrypted; this.key = key; this.iv = iv; this.mode = mode;
        }
        public void setPattern(Pattern p) { mPattern = p != null ? p : new Pattern(0, 0); }
        public Pattern getPattern() { return mPattern; }
        @Override public String toString() { return numSubSamples + " subsamples, mode " + mode; }
        public static final class Pattern {
            private int mEncrypt, mSkip;
            public Pattern(int encryptBlocks, int skipBlocks) { set(encryptBlocks, skipBlocks); }
            public void set(int encryptBlocks, int skipBlocks) { mEncrypt = encryptBlocks; mSkip = skipBlocks; }
            public int getEncryptBlocks() { return mEncrypt; }
            public int getSkipBlocks() { return mSkip; }
        }
    }
    public static final class GlobalResourceInfo {
        private final java.util.HashMap<String, Object> huskProps = new java.util.HashMap<>();
        public GlobalResourceInfo() {}
        public long getAvailable() { return (huskProps.get("Available") instanceof Long ? (Long) huskProps.get("Available") : 0L); }
        public long getCapacity() { return (huskProps.get("Capacity") instanceof Long ? (Long) huskProps.get("Capacity") : 0L); }
        public java.lang.String getName() { return (java.lang.String) huskProps.get("Name"); }
    }
    public static class IncompatibleWithBlockModelException extends java.lang.RuntimeException {
        private final java.util.HashMap<String, Object> huskProps = new java.util.HashMap<>();
        protected IncompatibleWithBlockModelException() { super(); }
    }
    public static final class InstanceResourceInfo {
        private final java.util.HashMap<String, Object> huskProps = new java.util.HashMap<>();
        public InstanceResourceInfo() {}
        public java.lang.String getName() { return (java.lang.String) huskProps.get("Name"); }
        public long getPerFrameCount() { return (huskProps.get("PerFrameCount") instanceof Long ? (Long) huskProps.get("PerFrameCount") : 0L); }
        public long getStaticCount() { return (huskProps.get("StaticCount") instanceof Long ? (Long) huskProps.get("StaticCount") : 0L); }
    }
    public static class InvalidBufferFlagsException extends java.lang.RuntimeException {
        private final java.util.HashMap<String, Object> huskProps = new java.util.HashMap<>();
        protected InvalidBufferFlagsException() { super(); }
    }
    public static final class LinearBlock {
        private final java.util.HashMap<String, Object> huskProps = new java.util.HashMap<>();
        public static boolean isCodecCopyFreeCompatible(java.lang.String[] p0) { return false; }
        public static android.media.MediaCodec.LinearBlock obtain(int p0, java.lang.String[] p1) { return new LinearBlock(); }
        protected void finalize() {}
        public boolean isMappable() { return (huskProps.get("Mappable") instanceof Boolean ? (Boolean) huskProps.get("Mappable") : false); }
        public java.nio.ByteBuffer map() { return null; }
        public void recycle() {}
        protected LinearBlock() {}
    }
    public static class MediaImage extends android.media.Image {
        private final java.util.HashMap<String, Object> huskProps = new java.util.HashMap<>();
        public MediaImage(java.nio.ByteBuffer p0, java.nio.ByteBuffer p1, boolean p2, long p3, int p4, int p5, android.graphics.Rect p6) { super(); }
        public MediaImage(java.nio.ByteBuffer[] p0, int[] p1, int[] p2, int p3, int p4, int p5, boolean p6, long p7, int p8, int p9, android.graphics.Rect p10, long p11) { super(); }
        public void close() {}
        public int getFormat() { return (huskProps.get("Format") instanceof Integer ? (Integer) huskProps.get("Format") : 0); }
        public int getHeight() { return (huskProps.get("Height") instanceof Integer ? (Integer) huskProps.get("Height") : 0); }
        public android.media.Image.Plane[] getPlanes() { return (android.media.Image.Plane[]) huskProps.get("Planes"); }
        public int getScalingMode() { return (huskProps.get("ScalingMode") instanceof Integer ? (Integer) huskProps.get("ScalingMode") : 0); }
        public long getTimestamp() { return (huskProps.get("Timestamp") instanceof Long ? (Long) huskProps.get("Timestamp") : 0L); }
        public int getTransform() { return (huskProps.get("Transform") instanceof Integer ? (Integer) huskProps.get("Transform") : 0); }
        public int getWidth() { return (huskProps.get("Width") instanceof Integer ? (Integer) huskProps.get("Width") : 0); }
        public void setCropRect(android.graphics.Rect p0) { huskProps.put("CropRect", p0); }
        MediaImage() { this((java.nio.ByteBuffer) null, (java.nio.ByteBuffer) null, (boolean) false, (long) 0L, (int) 0, (int) 0, (android.graphics.Rect) null); }
    }
    public static final class MetricsConstants {
        private final java.util.HashMap<String, Object> huskProps = new java.util.HashMap<>();
        public static final java.lang.String CODEC = "android.media.mediacodec.codec";
        public static final java.lang.String ENCODER = "android.media.mediacodec.encoder";
        public static final java.lang.String HEIGHT = "android.media.mediacodec.height";
        public static final java.lang.String MIME_TYPE = "android.media.mediacodec.mime";
        public static final java.lang.String MODE = "android.media.mediacodec.mode";
        public static final java.lang.String MODE_AUDIO = "audio";
        public static final java.lang.String MODE_VIDEO = "video";
        public static final java.lang.String ROTATION = "android.media.mediacodec.rotation";
        public static final java.lang.String SECURE = "android.media.mediacodec.secure";
        public static final java.lang.String WIDTH = "android.media.mediacodec.width";
        protected MetricsConstants() {}
    }
    public interface OnFirstTunnelFrameReadyListener {
        void onFirstTunnelFrameReady(android.media.MediaCodec p0);
    }
    public interface OnFrameRenderedListener {
        void onFrameRendered(android.media.MediaCodec p0, long p1, long p2);
    }
    public static final class OutputFrame {
        private final java.util.HashMap<String, Object> huskProps = new java.util.HashMap<>();
        public java.util.ArrayDeque getBufferInfos() { return (java.util.ArrayDeque) huskProps.get("BufferInfos"); }
        public java.util.Set getChangedKeys() { return (huskProps.get("ChangedKeys") != null ? (java.util.Set) huskProps.get("ChangedKeys") : new java.util.HashSet()); }
        public int getFlags() { return (huskProps.get("Flags") instanceof Integer ? (Integer) huskProps.get("Flags") : 0); }
        public android.media.MediaFormat getFormat() { return (android.media.MediaFormat) huskProps.get("Format"); }
        public android.hardware.HardwareBuffer getHardwareBuffer() { return (android.hardware.HardwareBuffer) huskProps.get("HardwareBuffer"); }
        public android.media.MediaCodec.LinearBlock getLinearBlock() { return (android.media.MediaCodec.LinearBlock) huskProps.get("LinearBlock"); }
        public long getPresentationTimeUs() { return (huskProps.get("PresentationTimeUs") instanceof Long ? (Long) huskProps.get("PresentationTimeUs") : 0L); }
        protected OutputFrame() {}
    }
    public static class ParameterDescriptor {
        private final java.util.HashMap<String, Object> huskProps = new java.util.HashMap<>();
        public java.lang.String getName() { return (java.lang.String) huskProps.get("Name"); }
        public int getType() { return (huskProps.get("Type") instanceof Integer ? (Integer) huskProps.get("Type") : 0); }
        protected ParameterDescriptor() {}
    }
    public static final class QueueRequest {
        private final java.util.HashMap<String, Object> huskProps = new java.util.HashMap<>();
        public void queue() {}
        public android.media.MediaCodec.QueueRequest setByteBufferParameter(java.lang.String p0, java.nio.ByteBuffer p1) { return this; }
        public android.media.MediaCodec.QueueRequest setEncryptedLinearBlock(android.media.MediaCodec.LinearBlock p0, int p1, int p2, android.media.MediaCodec.CryptoInfo p3) { return this; }
        public android.media.MediaCodec.QueueRequest setFlags(int p0) { huskProps.put("Flags", Integer.valueOf(p0)); return this; }
        public android.media.MediaCodec.QueueRequest setFloatParameter(java.lang.String p0, float p1) { return this; }
        public android.media.MediaCodec.QueueRequest setHardwareBuffer(android.hardware.HardwareBuffer p0) { huskProps.put("HardwareBuffer", p0); return this; }
        public android.media.MediaCodec.QueueRequest setIntegerParameter(java.lang.String p0, int p1) { return this; }
        public android.media.MediaCodec.QueueRequest setLinearBlock(android.media.MediaCodec.LinearBlock p0, int p1, int p2) { return this; }
        public android.media.MediaCodec.QueueRequest setLongParameter(java.lang.String p0, long p1) { return this; }
        public android.media.MediaCodec.QueueRequest setMultiFrameEncryptedLinearBlock(android.media.MediaCodec.LinearBlock p0, java.util.ArrayDeque p1, java.util.ArrayDeque p2) { return this; }
        public android.media.MediaCodec.QueueRequest setMultiFrameLinearBlock(android.media.MediaCodec.LinearBlock p0, java.util.ArrayDeque p1) { return this; }
        public android.media.MediaCodec.QueueRequest setPresentationTimeUs(long p0) { huskProps.put("PresentationTimeUs", Long.valueOf(p0)); return this; }
        public android.media.MediaCodec.QueueRequest setStringParameter(java.lang.String p0, java.lang.String p1) { return this; }
        protected QueueRequest() {}
    }
}
