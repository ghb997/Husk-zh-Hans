package husk;

/** Decoders behind android.media.MediaCodec (husk-tl-dvm-media.c): VideoToolbox for H.264/HEVC, AudioToolbox for AAC/MP3. */
public final class MediaCodecNative {
    private MediaCodecNative() {}
    public static native boolean supported(String mime);
    public static native long create(String mime);
    public static native boolean configure(long h, int width, int height, int rate, int channels, byte[] csd0, byte[] csd1, byte[] csd2);
    public static native int decode(long h, byte[] data, int len, long ptsUs, int flags);
    /** The next decoded frame in display order, 0 when none; info = { ptsUs, width, height }. */
    public static native long nextFrame(long h, long[] info);
    /** The next decoded 16-bit PCM, null when none; info = { ptsUs, sampleRate, channels }. */
    public static native byte[] nextPcm(long h, long[] info);
    public static native boolean renderFrame(long frame, android.graphics.Bitmap dst);
    public static native void freeFrame(long frame);
    public static native void flush(long h);
    public static native void destroy(long h);
    public static native int size(long h);
}
