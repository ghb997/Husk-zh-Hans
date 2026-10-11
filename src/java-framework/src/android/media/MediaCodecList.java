package android.media;

import java.util.ArrayList;

/** The decoders Husk has (see MediaCodecInfo): H.264 and HEVC video, AAC and MP3 audio. No encoders. */
public final class MediaCodecList {
    public static final int REGULAR_CODECS = 0, ALL_CODECS = 1;
    private static MediaCodecInfo[] sInfos;
    static synchronized MediaCodecInfo[] infos() {
        if (sInfos == null) {
            int[][] avc = {
                { MediaCodecInfo.CodecProfileLevel.AVCProfileConstrainedBaseline, MediaCodecInfo.CodecProfileLevel.AVCLevel52 },
                { MediaCodecInfo.CodecProfileLevel.AVCProfileBaseline, MediaCodecInfo.CodecProfileLevel.AVCLevel52 },
                { MediaCodecInfo.CodecProfileLevel.AVCProfileMain, MediaCodecInfo.CodecProfileLevel.AVCLevel52 },
                { MediaCodecInfo.CodecProfileLevel.AVCProfileConstrainedHigh, MediaCodecInfo.CodecProfileLevel.AVCLevel52 },
                { MediaCodecInfo.CodecProfileLevel.AVCProfileHigh, MediaCodecInfo.CodecProfileLevel.AVCLevel52 },
            };
            int[][] hevc = {
                { MediaCodecInfo.CodecProfileLevel.HEVCProfileMain, MediaCodecInfo.CodecProfileLevel.HEVCMainTierLevel51 },
                { MediaCodecInfo.CodecProfileLevel.HEVCProfileMain10, MediaCodecInfo.CodecProfileLevel.HEVCMainTierLevel51 },
            };
            ArrayList<MediaCodecInfo> l = new ArrayList<>();
            l.add(new MediaCodecInfo("c2.husk.avc.decoder", false, MediaCodecInfo.CodecCapabilities.video(MediaFormat.MIMETYPE_VIDEO_AVC, avc)));
            if (husk.MediaCodecNative.supported(MediaFormat.MIMETYPE_VIDEO_HEVC))
                l.add(new MediaCodecInfo("c2.husk.hevc.decoder", false, MediaCodecInfo.CodecCapabilities.video(MediaFormat.MIMETYPE_VIDEO_HEVC, hevc)));
            l.add(new MediaCodecInfo("c2.husk.aac.decoder", false, MediaCodecInfo.CodecCapabilities.audio(MediaFormat.MIMETYPE_AUDIO_AAC, 8,
                new int[] { MediaCodecInfo.CodecProfileLevel.AACObjectLC, MediaCodecInfo.CodecProfileLevel.AACObjectHE, MediaCodecInfo.CodecProfileLevel.AACObjectHE_PS })));
            l.add(new MediaCodecInfo("c2.husk.mp3.decoder", false, MediaCodecInfo.CodecCapabilities.audio(MediaFormat.MIMETYPE_AUDIO_MPEG, 2, new int[0])));
            sInfos = l.toArray(new MediaCodecInfo[0]);
        }
        return sInfos;
    }
    public MediaCodecList(int kind) {}
    public static int getCodecCount() { return infos().length; }
    public static MediaCodecInfo getCodecInfoAt(int i) { MediaCodecInfo[] a = infos(); if (i < 0 || i >= a.length) throw new IllegalArgumentException(); return a[i]; }
    public static MediaCodecInfo getInfoFor(String name) { for (MediaCodecInfo i : infos()) if (i.getName().equals(name)) return i; return null; }
    public MediaCodecInfo[] getCodecInfos() { return infos().clone(); }
    public String findDecoderForFormat(MediaFormat f) {
        String mime = f.getString(MediaFormat.KEY_MIME);
        if (mime == null) return null;
        for (MediaCodecInfo i : infos()) {
            for (String t : i.getSupportedTypes()) if (t.equalsIgnoreCase(mime) && i.getCapabilitiesForType(t).isFormatSupported(f)) return i.getName();
        }
        return null;
    }
    public String findEncoderForFormat(MediaFormat f) { return null; }
}
