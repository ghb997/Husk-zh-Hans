package android.media;

import android.util.Range;
import java.util.HashSet;
import java.util.Set;

/**
 * A codec the device has. Husk decodes H.264 and HEVC video through VideoToolbox and AAC (and MP3) audio through AudioToolbox
 * (husk-tl-dvm-media.c); MediaCodecList lists those, with capabilities like a phone's hardware decoders.
 */
@SuppressWarnings({"unchecked", "rawtypes"})
public final class MediaCodecInfo {
    public static final int SECURITY_MODEL_SANDBOXED = 0, SECURITY_MODEL_MEMORY_SAFE = 1, SECURITY_MODEL_TRUSTED_CONTENT_ONLY = 2;
    private final String mName;
    private final String[] mTypes;
    private final CodecCapabilities[] mCaps;
    private final boolean mEncoder;
    MediaCodecInfo(String name, boolean encoder, CodecCapabilities... caps) {
        mName = name; mEncoder = encoder; mCaps = caps; mTypes = new String[caps.length];
        for (int i = 0; i < caps.length; i++) mTypes[i] = caps[i].mMime;
    }
    protected MediaCodecInfo() { this("", false); }
    public String getName() { return mName; }
    public String getCanonicalName() { return mName; }
    public boolean isAlias() { return false; }
    public boolean isEncoder() { return mEncoder; }
    public boolean isHardwareAccelerated() { return true; }
    public boolean isSoftwareOnly() { return false; }
    public boolean isVendor() { return true; }
    public int getSecurityModel() { return SECURITY_MODEL_SANDBOXED; }
    public String[] getSupportedTypes() { return mTypes.clone(); }
    public CodecCapabilities getCapabilitiesForType(String type) {
        for (CodecCapabilities c : mCaps) if (c.mMime.equalsIgnoreCase(type)) return c;
        throw new IllegalArgumentException("codec does not support type");
    }
    public MediaCodecInfo makeRegular() { return this; }

    public static final class CodecCapabilities {
        public static final int COLOR_Format12bitRGB444 = 3;
        public static final int COLOR_Format16bitARGB1555 = 5;
        public static final int COLOR_Format16bitARGB4444 = 4;
        public static final int COLOR_Format16bitBGR565 = 7;
        public static final int COLOR_Format16bitRGB565 = 6;
        public static final int COLOR_Format18BitBGR666 = 41;
        public static final int COLOR_Format18bitARGB1665 = 9;
        public static final int COLOR_Format18bitRGB666 = 8;
        public static final int COLOR_Format19bitARGB1666 = 10;
        public static final int COLOR_Format24BitABGR6666 = 43;
        public static final int COLOR_Format24BitARGB6666 = 42;
        public static final int COLOR_Format24bitARGB1887 = 13;
        public static final int COLOR_Format24bitBGR888 = 12;
        public static final int COLOR_Format24bitRGB888 = 11;
        public static final int COLOR_Format25bitARGB1888 = 14;
        public static final int COLOR_Format32bitABGR2101010 = 2130750114;
        public static final int COLOR_Format32bitABGR8888 = 2130747392;
        public static final int COLOR_Format32bitARGB8888 = 16;
        public static final int COLOR_Format32bitBGRA8888 = 15;
        public static final int COLOR_Format64bitABGRFloat = 2130710294;
        public static final int COLOR_Format8bitRGB332 = 2;
        public static final int COLOR_FormatCbYCrY = 27;
        public static final int COLOR_FormatCrYCbY = 28;
        public static final int COLOR_FormatL16 = 36;
        public static final int COLOR_FormatL2 = 33;
        public static final int COLOR_FormatL24 = 37;
        public static final int COLOR_FormatL32 = 38;
        public static final int COLOR_FormatL4 = 34;
        public static final int COLOR_FormatL8 = 35;
        public static final int COLOR_FormatMonochrome = 1;
        public static final int COLOR_FormatRGBAFlexible = 2134288520;
        public static final int COLOR_FormatRGBFlexible = 2134292616;
        public static final int COLOR_FormatRawBayer10bit = 31;
        public static final int COLOR_FormatRawBayer8bit = 30;
        public static final int COLOR_FormatRawBayer8bitcompressed = 32;
        public static final int COLOR_FormatSurface = 2130708361;
        public static final int COLOR_FormatYCbYCr = 25;
        public static final int COLOR_FormatYCrYCb = 26;
        public static final int COLOR_FormatYUV411PackedPlanar = 18;
        public static final int COLOR_FormatYUV411Planar = 17;
        public static final int COLOR_FormatYUV420Flexible = 2135033992;
        public static final int COLOR_FormatYUV420PackedPlanar = 20;
        public static final int COLOR_FormatYUV420PackedSemiPlanar = 39;
        public static final int COLOR_FormatYUV420Planar = 19;
        public static final int COLOR_FormatYUV420SemiPlanar = 21;
        public static final int COLOR_FormatYUV422Flexible = 2135042184;
        public static final int COLOR_FormatYUV422PackedPlanar = 23;
        public static final int COLOR_FormatYUV422PackedSemiPlanar = 40;
        public static final int COLOR_FormatYUV422Planar = 22;
        public static final int COLOR_FormatYUV422SemiPlanar = 24;
        public static final int COLOR_FormatYUV444Flexible = 2135181448;
        public static final int COLOR_FormatYUV444Interleaved = 29;
        public static final int COLOR_FormatYUVP010 = 54;
        public static final int COLOR_FormatYUVP210 = 60;
        public static final int COLOR_QCOM_FormatYUV420SemiPlanar = 2141391872;
        public static final int COLOR_TI_FormatYUV420PackedSemiPlanar = 2130706688;
        public static final java.lang.String FEATURE_AdaptivePlayback = "adaptive-playback";
        public static final java.lang.String FEATURE_DetachedSurface = "detached-surface";
        public static final java.lang.String FEATURE_DynamicColorAspects = "dynamic-color-aspects";
        public static final java.lang.String FEATURE_DynamicTimestamp = "dynamic-timestamp";
        public static final java.lang.String FEATURE_EncodingStatistics = "encoding-statistics";
        public static final java.lang.String FEATURE_FrameParsing = "frame-parsing";
        public static final java.lang.String FEATURE_HdrEditing = "hdr-editing";
        public static final java.lang.String FEATURE_HlgEditing = "hlg-editing";
        public static final java.lang.String FEATURE_IntraRefresh = "intra-refresh";
        public static final java.lang.String FEATURE_LowLatency = "low-latency";
        public static final java.lang.String FEATURE_MultipleFrames = "multiple-frames";
        public static final java.lang.String FEATURE_PartialFrame = "partial-frame";
        public static final java.lang.String FEATURE_QpBounds = "qp-bounds";
        public static final java.lang.String FEATURE_Roi = "region-of-interest";
        public static final java.lang.String FEATURE_SecurePlayback = "secure-playback";
        public static final java.lang.String FEATURE_TunneledPlayback = "tunneled-playback";
        public int[] colorFormats = new int[0];
        public CodecProfileLevel[] profileLevels = new CodecProfileLevel[0];
        String mMime = "";
        VideoCapabilities mVideo; AudioCapabilities mAudio;
        final Set<String> mFeatures = new HashSet<>();
        public CodecCapabilities() {}
        static CodecCapabilities video(String mime, int[][] pl) {
            CodecCapabilities c = new CodecCapabilities(); c.mMime = mime;
            c.colorFormats = new int[] { COLOR_FormatSurface, COLOR_FormatYUV420Flexible, COLOR_FormatYUV420SemiPlanar };
            c.profileLevels = new CodecProfileLevel[pl.length];
            for (int i = 0; i < pl.length; i++) { c.profileLevels[i] = new CodecProfileLevel(); c.profileLevels[i].profile = pl[i][0]; c.profileLevels[i].level = pl[i][1]; }
            c.mVideo = new VideoCapabilities(); c.mFeatures.add(FEATURE_AdaptivePlayback);
            return c;
        }
        static CodecCapabilities audio(String mime, int maxChannels, int[] profiles) {
            CodecCapabilities c = new CodecCapabilities(); c.mMime = mime;
            c.profileLevels = new CodecProfileLevel[profiles.length];
            for (int i = 0; i < profiles.length; i++) { c.profileLevels[i] = new CodecProfileLevel(); c.profileLevels[i].profile = profiles[i]; }
            c.mAudio = new AudioCapabilities(maxChannels);
            return c;
        }
        public static CodecCapabilities createFromProfileLevel(String mime, int profile, int level) {
            CodecCapabilities c = mime.startsWith("video/") ? video(mime, new int[][] { { profile, level } }) : audio(mime, 8, new int[] { profile }); return c;
        }
        public CodecCapabilities dup() { return this; }
        public String getMimeType() { return mMime; }
        public VideoCapabilities getVideoCapabilities() { return mVideo; }
        public AudioCapabilities getAudioCapabilities() { return mAudio; }
        public EncoderCapabilities getEncoderCapabilities() { return null; }
        public int getMaxSupportedInstances() { return 16; }
        public MediaFormat getDefaultFormat() { MediaFormat f = new MediaFormat(); f.setString(MediaFormat.KEY_MIME, mMime); return f; }
        public boolean isFeatureSupported(String name) { return mFeatures.contains(name); }
        public boolean isFeatureRequired(String name) { return false; }
        public String[] validFeatures() { return mVideo != null ? new String[] { FEATURE_AdaptivePlayback, FEATURE_SecurePlayback, FEATURE_TunneledPlayback, FEATURE_PartialFrame } : new String[0]; }
        public boolean isRegular() { return true; }
        public boolean isFormatSupported(MediaFormat f) {
            if (f == null) return false;
            String m = f.getString(MediaFormat.KEY_MIME);
            if (m != null && !m.equalsIgnoreCase(mMime)) return false;
            for (String feat : new String[] { FEATURE_SecurePlayback, FEATURE_TunneledPlayback }) if (f.containsKey("feature-" + feat) && f.getInteger("feature-" + feat) != 0) return false;
            if (mVideo != null) return mVideo.supportsFormat(f);
            if (mAudio != null) return mAudio.supportsFormat(f);
            return true;
        }
        public interface CodecCapsIntf {
            CodecCapsIntf dup(); AudioCapabilities getAudioCapabilities(); int[] getColorFormats(); MediaFormat getDefaultFormat();
            EncoderCapabilities getEncoderCapabilities(); int getMaxSupportedInstances(); String getMimeType(); CodecProfileLevel[] getProfileLevels();
            VideoCapabilities getVideoCapabilities(); boolean isFeatureRequired(String n); boolean isFeatureSupported(String n);
            boolean isFormatSupported(MediaFormat f); boolean isRegular(); String[] validFeatures();
        }
    }

    public static final class CodecProfileLevel {
        public static final int AACObjectELD = 39;
        public static final int AACObjectERLC = 17;
        public static final int AACObjectERScalable = 20;
        public static final int AACObjectHE = 5;
        public static final int AACObjectHE_PS = 29;
        public static final int AACObjectLC = 2;
        public static final int AACObjectLD = 23;
        public static final int AACObjectLTP = 4;
        public static final int AACObjectMain = 1;
        public static final int AACObjectSSR = 3;
        public static final int AACObjectScalable = 6;
        public static final int AACObjectXHE = 42;
        public static final int AC4Level0 = 1;
        public static final int AC4Level1 = 2;
        public static final int AC4Level2 = 4;
        public static final int AC4Level3 = 8;
        public static final int AC4Level4 = 16;
        public static final int AC4Profile00 = 257;
        public static final int AC4Profile10 = 513;
        public static final int AC4Profile11 = 514;
        public static final int AC4Profile21 = 1026;
        public static final int AC4Profile22 = 1028;
        public static final int APVLevel11Band0 = 513;
        public static final int APVLevel11Band1 = 514;
        public static final int APVLevel11Band2 = 516;
        public static final int APVLevel11Band3 = 520;
        public static final int APVLevel1Band0 = 257;
        public static final int APVLevel1Band1 = 258;
        public static final int APVLevel1Band2 = 260;
        public static final int APVLevel1Band3 = 264;
        public static final int APVLevel21Band0 = 2049;
        public static final int APVLevel21Band1 = 2050;
        public static final int APVLevel21Band2 = 2052;
        public static final int APVLevel21Band3 = 2056;
        public static final int APVLevel2Band0 = 1025;
        public static final int APVLevel2Band1 = 1026;
        public static final int APVLevel2Band2 = 1028;
        public static final int APVLevel2Band3 = 1032;
        public static final int APVLevel31Band0 = 8193;
        public static final int APVLevel31Band1 = 8194;
        public static final int APVLevel31Band2 = 8196;
        public static final int APVLevel31Band3 = 8200;
        public static final int APVLevel3Band0 = 4097;
        public static final int APVLevel3Band1 = 4098;
        public static final int APVLevel3Band2 = 4100;
        public static final int APVLevel3Band3 = 4104;
        public static final int APVLevel41Band0 = 32769;
        public static final int APVLevel41Band1 = 32770;
        public static final int APVLevel41Band2 = 32772;
        public static final int APVLevel41Band3 = 32776;
        public static final int APVLevel4Band0 = 16385;
        public static final int APVLevel4Band1 = 16386;
        public static final int APVLevel4Band2 = 16388;
        public static final int APVLevel4Band3 = 16392;
        public static final int APVLevel51Band0 = 131073;
        public static final int APVLevel51Band1 = 131074;
        public static final int APVLevel51Band2 = 131076;
        public static final int APVLevel51Band3 = 131080;
        public static final int APVLevel5Band0 = 65537;
        public static final int APVLevel5Band1 = 65538;
        public static final int APVLevel5Band2 = 65540;
        public static final int APVLevel5Band3 = 65544;
        public static final int APVLevel61Band0 = 524289;
        public static final int APVLevel61Band1 = 524290;
        public static final int APVLevel61Band2 = 524292;
        public static final int APVLevel61Band3 = 524296;
        public static final int APVLevel6Band0 = 262145;
        public static final int APVLevel6Band1 = 262146;
        public static final int APVLevel6Band2 = 262148;
        public static final int APVLevel6Band3 = 262152;
        public static final int APVLevel71Band0 = 2097153;
        public static final int APVLevel71Band1 = 2097154;
        public static final int APVLevel71Band2 = 2097156;
        public static final int APVLevel71Band3 = 2097160;
        public static final int APVLevel7Band0 = 1048577;
        public static final int APVLevel7Band1 = 1048578;
        public static final int APVLevel7Band2 = 1048580;
        public static final int APVLevel7Band3 = 1048584;
        public static final int APVProfile422_10 = 1;
        public static final int APVProfile422_10HDR10 = 4096;
        public static final int APVProfile422_10HDR10Plus = 8192;
        public static final int AV1Level2 = 1;
        public static final int AV1Level21 = 2;
        public static final int AV1Level22 = 4;
        public static final int AV1Level23 = 8;
        public static final int AV1Level3 = 16;
        public static final int AV1Level31 = 32;
        public static final int AV1Level32 = 64;
        public static final int AV1Level33 = 128;
        public static final int AV1Level4 = 256;
        public static final int AV1Level41 = 512;
        public static final int AV1Level42 = 1024;
        public static final int AV1Level43 = 2048;
        public static final int AV1Level5 = 4096;
        public static final int AV1Level51 = 8192;
        public static final int AV1Level52 = 16384;
        public static final int AV1Level53 = 32768;
        public static final int AV1Level6 = 65536;
        public static final int AV1Level61 = 131072;
        public static final int AV1Level62 = 262144;
        public static final int AV1Level63 = 524288;
        public static final int AV1Level7 = 1048576;
        public static final int AV1Level71 = 2097152;
        public static final int AV1Level72 = 4194304;
        public static final int AV1Level73 = 8388608;
        public static final int AV1ProfileMain10 = 2;
        public static final int AV1ProfileMain10HDR10 = 4096;
        public static final int AV1ProfileMain10HDR10Plus = 8192;
        public static final int AV1ProfileMain8 = 1;
        public static final int AVCLevel1 = 1;
        public static final int AVCLevel11 = 4;
        public static final int AVCLevel12 = 8;
        public static final int AVCLevel13 = 16;
        public static final int AVCLevel1b = 2;
        public static final int AVCLevel2 = 32;
        public static final int AVCLevel21 = 64;
        public static final int AVCLevel22 = 128;
        public static final int AVCLevel3 = 256;
        public static final int AVCLevel31 = 512;
        public static final int AVCLevel32 = 1024;
        public static final int AVCLevel4 = 2048;
        public static final int AVCLevel41 = 4096;
        public static final int AVCLevel42 = 8192;
        public static final int AVCLevel5 = 16384;
        public static final int AVCLevel51 = 32768;
        public static final int AVCLevel52 = 65536;
        public static final int AVCLevel6 = 131072;
        public static final int AVCLevel61 = 262144;
        public static final int AVCLevel62 = 524288;
        public static final int AVCProfileBaseline = 1;
        public static final int AVCProfileConstrainedBaseline = 65536;
        public static final int AVCProfileConstrainedHigh = 524288;
        public static final int AVCProfileExtended = 4;
        public static final int AVCProfileHigh = 8;
        public static final int AVCProfileHigh10 = 16;
        public static final int AVCProfileHigh422 = 32;
        public static final int AVCProfileHigh444 = 64;
        public static final int AVCProfileMain = 2;
        public static final int DTS_HDProfileHRA = 1;
        public static final int DTS_HDProfileLBR = 2;
        public static final int DTS_HDProfileMA = 4;
        public static final int DTS_UHDProfileP1 = 1;
        public static final int DTS_UHDProfileP2 = 2;
        public static final int DolbyVisionLevel8k30 = 1024;
        public static final int DolbyVisionLevel8k60 = 2048;
        public static final int DolbyVisionLevelFhd24 = 4;
        public static final int DolbyVisionLevelFhd30 = 8;
        public static final int DolbyVisionLevelFhd60 = 16;
        public static final int DolbyVisionLevelHd24 = 1;
        public static final int DolbyVisionLevelHd30 = 2;
        public static final int DolbyVisionLevelUhd120 = 512;
        public static final int DolbyVisionLevelUhd24 = 32;
        public static final int DolbyVisionLevelUhd30 = 64;
        public static final int DolbyVisionLevelUhd48 = 128;
        public static final int DolbyVisionLevelUhd60 = 256;
        public static final int DolbyVisionProfileDvav110 = 1024;
        public static final int DolbyVisionProfileDvavPen = 2;
        public static final int DolbyVisionProfileDvavPer = 1;
        public static final int DolbyVisionProfileDvavSe = 512;
        public static final int DolbyVisionProfileDvheDen = 8;
        public static final int DolbyVisionProfileDvheDer = 4;
        public static final int DolbyVisionProfileDvheDtb = 128;
        public static final int DolbyVisionProfileDvheDth = 64;
        public static final int DolbyVisionProfileDvheDtr = 16;
        public static final int DolbyVisionProfileDvheSt = 256;
        public static final int DolbyVisionProfileDvheStn = 32;
        public static final int H263Level10 = 1;
        public static final int H263Level20 = 2;
        public static final int H263Level30 = 4;
        public static final int H263Level40 = 8;
        public static final int H263Level45 = 16;
        public static final int H263Level50 = 32;
        public static final int H263Level60 = 64;
        public static final int H263Level70 = 128;
        public static final int H263ProfileBackwardCompatible = 4;
        public static final int H263ProfileBaseline = 1;
        public static final int H263ProfileH320Coding = 2;
        public static final int H263ProfileHighCompression = 32;
        public static final int H263ProfileHighLatency = 256;
        public static final int H263ProfileISWV2 = 8;
        public static final int H263ProfileISWV3 = 16;
        public static final int H263ProfileInterlace = 128;
        public static final int H263ProfileInternet = 64;
        public static final int HEVCHighTierLevel1 = 2;
        public static final int HEVCHighTierLevel2 = 8;
        public static final int HEVCHighTierLevel21 = 32;
        public static final int HEVCHighTierLevel3 = 128;
        public static final int HEVCHighTierLevel31 = 512;
        public static final int HEVCHighTierLevel4 = 2048;
        public static final int HEVCHighTierLevel41 = 8192;
        public static final int HEVCHighTierLevel5 = 32768;
        public static final int HEVCHighTierLevel51 = 131072;
        public static final int HEVCHighTierLevel52 = 524288;
        public static final int HEVCHighTierLevel6 = 2097152;
        public static final int HEVCHighTierLevel61 = 8388608;
        public static final int HEVCHighTierLevel62 = 33554432;
        public static final int HEVCMainTierLevel1 = 1;
        public static final int HEVCMainTierLevel2 = 4;
        public static final int HEVCMainTierLevel21 = 16;
        public static final int HEVCMainTierLevel3 = 64;
        public static final int HEVCMainTierLevel31 = 256;
        public static final int HEVCMainTierLevel4 = 1024;
        public static final int HEVCMainTierLevel41 = 4096;
        public static final int HEVCMainTierLevel5 = 16384;
        public static final int HEVCMainTierLevel51 = 65536;
        public static final int HEVCMainTierLevel52 = 262144;
        public static final int HEVCMainTierLevel6 = 1048576;
        public static final int HEVCMainTierLevel61 = 4194304;
        public static final int HEVCMainTierLevel62 = 16777216;
        public static final int HEVCProfileMain = 1;
        public static final int HEVCProfileMain10 = 2;
        public static final int HEVCProfileMain10HDR10 = 4096;
        public static final int HEVCProfileMain10HDR10Plus = 8192;
        public static final int HEVCProfileMainStill = 4;
        public static final int IAMFProfileBaseAac = 16908290;
        public static final int IAMFProfileBaseEnhancedAac = 17039362;
        public static final int IAMFProfileBaseEnhancedFlac = 17039364;
        public static final int IAMFProfileBaseEnhancedOpus = 17039361;
        public static final int IAMFProfileBaseEnhancedPcm = 17039368;
        public static final int IAMFProfileBaseFlac = 16908292;
        public static final int IAMFProfileBaseOpus = 16908289;
        public static final int IAMFProfileBasePcm = 16908296;
        public static final int IAMFProfileSimpleAac = 16842754;
        public static final int IAMFProfileSimpleFlac = 16842756;
        public static final int IAMFProfileSimpleOpus = 16842753;
        public static final int IAMFProfileSimplePcm = 16842760;
        public static final int MPEG2LevelH14 = 2;
        public static final int MPEG2LevelHL = 3;
        public static final int MPEG2LevelHP = 4;
        public static final int MPEG2LevelLL = 0;
        public static final int MPEG2LevelML = 1;
        public static final int MPEG2Profile422 = 2;
        public static final int MPEG2ProfileHigh = 5;
        public static final int MPEG2ProfileMain = 1;
        public static final int MPEG2ProfileSNR = 3;
        public static final int MPEG2ProfileSimple = 0;
        public static final int MPEG2ProfileSpatial = 4;
        public static final int MPEG4Level0 = 1;
        public static final int MPEG4Level0b = 2;
        public static final int MPEG4Level1 = 4;
        public static final int MPEG4Level2 = 8;
        public static final int MPEG4Level3 = 16;
        public static final int MPEG4Level3b = 24;
        public static final int MPEG4Level4 = 32;
        public static final int MPEG4Level4a = 64;
        public static final int MPEG4Level5 = 128;
        public static final int MPEG4Level6 = 256;
        public static final int MPEG4ProfileAdvancedCoding = 4096;
        public static final int MPEG4ProfileAdvancedCore = 8192;
        public static final int MPEG4ProfileAdvancedRealTime = 1024;
        public static final int MPEG4ProfileAdvancedScalable = 16384;
        public static final int MPEG4ProfileAdvancedSimple = 32768;
        public static final int MPEG4ProfileBasicAnimated = 256;
        public static final int MPEG4ProfileCore = 4;
        public static final int MPEG4ProfileCoreScalable = 2048;
        public static final int MPEG4ProfileHybrid = 512;
        public static final int MPEG4ProfileMain = 8;
        public static final int MPEG4ProfileNbit = 16;
        public static final int MPEG4ProfileScalableTexture = 32;
        public static final int MPEG4ProfileSimple = 1;
        public static final int MPEG4ProfileSimpleFBA = 128;
        public static final int MPEG4ProfileSimpleFace = 64;
        public static final int MPEG4ProfileSimpleScalable = 2;
        public static final int VP8Level_Version0 = 1;
        public static final int VP8Level_Version1 = 2;
        public static final int VP8Level_Version2 = 4;
        public static final int VP8Level_Version3 = 8;
        public static final int VP8ProfileMain = 1;
        public static final int VP9Level1 = 1;
        public static final int VP9Level11 = 2;
        public static final int VP9Level2 = 4;
        public static final int VP9Level21 = 8;
        public static final int VP9Level3 = 16;
        public static final int VP9Level31 = 32;
        public static final int VP9Level4 = 64;
        public static final int VP9Level41 = 128;
        public static final int VP9Level5 = 256;
        public static final int VP9Level51 = 512;
        public static final int VP9Level52 = 1024;
        public static final int VP9Level6 = 2048;
        public static final int VP9Level61 = 4096;
        public static final int VP9Level62 = 8192;
        public static final int VP9Profile0 = 1;
        public static final int VP9Profile1 = 2;
        public static final int VP9Profile2 = 4;
        public static final int VP9Profile2HDR = 4096;
        public static final int VP9Profile2HDR10Plus = 16384;
        public static final int VP9Profile3 = 8;
        public static final int VP9Profile3HDR = 8192;
        public static final int VP9Profile3HDR10Plus = 32768;
        public int profile, level;
        public CodecProfileLevel() {}
        @Override public boolean equals(Object o) { return o instanceof CodecProfileLevel && ((CodecProfileLevel) o).profile == profile && ((CodecProfileLevel) o).level == level; }
        @Override public int hashCode() { return profile * 31 + level; }
    }

    public static final class VideoCapabilities {
        private static final int MAX_W = 4096, MAX_H = 4096;
        private static final long MAX_PIXEL_RATE = 3840L * 2160 * 60;
        VideoCapabilities() {}
        public Range<Integer> getBitrateRange() { return new Range<>(1, 240000000); }
        public Range<Integer> getSupportedWidths() { return new Range<>(2, MAX_W); }
        public Range<Integer> getSupportedHeights() { return new Range<>(2, MAX_H); }
        public int getWidthAlignment() { return 2; }
        public int getHeightAlignment() { return 2; }
        public int getSmallerDimensionUpperLimit() { return 2304; }
        public Range<Integer> getSupportedFrameRates() { return new Range<>(0, 960); }
        public boolean isSizeSupported(int w, int h) { return w >= 2 && h >= 2 && w <= MAX_W && h <= MAX_H && Math.min(w, h) <= 2304 && w % 2 == 0 && h % 2 == 0; }
        public Range<Integer> getSupportedWidthsFor(int h) { if (h < 2 || h > MAX_H) throw new IllegalArgumentException("unsupported height"); return new Range<>(2, MAX_W); }
        public Range<Integer> getSupportedHeightsFor(int w) { if (w < 2 || w > MAX_W) throw new IllegalArgumentException("unsupported width"); return new Range<>(2, MAX_H); }
        public Range<Double> getSupportedFrameRatesFor(int w, int h) {
            if (!isSizeSupported(w, h)) throw new IllegalArgumentException("unsupported size");
            return new Range<>(0.0, Math.min(960.0, (double) MAX_PIXEL_RATE / ((long) w * h)));
        }
        public Range<Double> getAchievableFrameRatesFor(int w, int h) { return getSupportedFrameRatesFor(w, h); }
        public boolean areSizeAndRateSupported(int w, int h, double rate) { return isSizeSupported(w, h) && rate <= (double) MAX_PIXEL_RATE / ((long) w * h); }
        public java.util.List<PerformancePoint> getSupportedPerformancePoints() {
            return java.util.Arrays.asList(new PerformancePoint(3840, 2160, 60), new PerformancePoint(1920, 1080, 240));
        }
        public boolean supportsFormat(MediaFormat f) {
            int w = f.containsKey(MediaFormat.KEY_WIDTH) ? f.getInteger(MediaFormat.KEY_WIDTH) : 0, h = f.containsKey(MediaFormat.KEY_HEIGHT) ? f.getInteger(MediaFormat.KEY_HEIGHT) : 0;
            if (w == 0 || h == 0) return true;
            double r = 0; try { if (f.containsKey(MediaFormat.KEY_FRAME_RATE)) r = f.getNumber(MediaFormat.KEY_FRAME_RATE).doubleValue(); } catch (Exception e) {}
            return r > 0 ? areSizeAndRateSupported(w, h, r) : isSizeSupported(w, h);
        }
        public static final class PerformancePoint {
            private final int mW, mH, mRate;
        public static final PerformancePoint FHD_100 = new PerformancePoint(1920, 1080, 100);
        public static final PerformancePoint FHD_120 = new PerformancePoint(1920, 1080, 120);
        public static final PerformancePoint FHD_200 = new PerformancePoint(1920, 1080, 200);
        public static final PerformancePoint FHD_24 = new PerformancePoint(1920, 1080, 24);
        public static final PerformancePoint FHD_240 = new PerformancePoint(1920, 1080, 240);
        public static final PerformancePoint FHD_25 = new PerformancePoint(1920, 1080, 25);
        public static final PerformancePoint FHD_30 = new PerformancePoint(1920, 1080, 30);
        public static final PerformancePoint FHD_50 = new PerformancePoint(1920, 1080, 50);
        public static final PerformancePoint FHD_60 = new PerformancePoint(1920, 1080, 60);
        public static final PerformancePoint HD_100 = new PerformancePoint(1280, 720, 100);
        public static final PerformancePoint HD_120 = new PerformancePoint(1280, 720, 120);
        public static final PerformancePoint HD_200 = new PerformancePoint(1280, 720, 200);
        public static final PerformancePoint HD_24 = new PerformancePoint(1280, 720, 24);
        public static final PerformancePoint HD_240 = new PerformancePoint(1280, 720, 240);
        public static final PerformancePoint HD_25 = new PerformancePoint(1280, 720, 25);
        public static final PerformancePoint HD_30 = new PerformancePoint(1280, 720, 30);
        public static final PerformancePoint HD_50 = new PerformancePoint(1280, 720, 50);
        public static final PerformancePoint HD_60 = new PerformancePoint(1280, 720, 60);
        public static final PerformancePoint SD_24 = new PerformancePoint(720, 480, 24);
        public static final PerformancePoint SD_25 = new PerformancePoint(720, 480, 25);
        public static final PerformancePoint SD_30 = new PerformancePoint(720, 480, 30);
        public static final PerformancePoint SD_48 = new PerformancePoint(720, 480, 48);
        public static final PerformancePoint SD_50 = new PerformancePoint(720, 480, 50);
        public static final PerformancePoint SD_60 = new PerformancePoint(720, 480, 60);
        public static final PerformancePoint UHD_100 = new PerformancePoint(3840, 2160, 100);
        public static final PerformancePoint UHD_120 = new PerformancePoint(3840, 2160, 120);
        public static final PerformancePoint UHD_200 = new PerformancePoint(3840, 2160, 200);
        public static final PerformancePoint UHD_24 = new PerformancePoint(3840, 2160, 24);
        public static final PerformancePoint UHD_240 = new PerformancePoint(3840, 2160, 240);
        public static final PerformancePoint UHD_25 = new PerformancePoint(3840, 2160, 25);
        public static final PerformancePoint UHD_30 = new PerformancePoint(3840, 2160, 30);
        public static final PerformancePoint UHD_50 = new PerformancePoint(3840, 2160, 50);
        public static final PerformancePoint UHD_60 = new PerformancePoint(3840, 2160, 60);
            public PerformancePoint(int w, int h, int rate) { mW = w; mH = h; mRate = rate; }
            public PerformancePoint(int w, int h, int rate, int maxRate, android.util.Size blockSize) { this(w, h, maxRate); }
            public PerformancePoint(PerformancePoint p, android.util.Size blockSize) { this(p.mW, p.mH, p.mRate); }
            public int getMaxFrameRate() { return mRate; }
            public int getMaxMacroBlocks() { return ((mW + 15) / 16) * ((mH + 15) / 16); }
            public long getMaxMacroBlockRate() { return (long) getMaxMacroBlocks() * mRate; }
            public boolean covers(PerformancePoint o) { return o.getMaxMacroBlocks() <= getMaxMacroBlocks() && o.getMaxMacroBlockRate() <= getMaxMacroBlockRate() && o.mRate <= Math.max(mRate, 960); }
            public boolean covers(MediaFormat f) {
                int w = f.containsKey(MediaFormat.KEY_WIDTH) ? f.getInteger(MediaFormat.KEY_WIDTH) : 0, h = f.containsKey(MediaFormat.KEY_HEIGHT) ? f.getInteger(MediaFormat.KEY_HEIGHT) : 0;
                int r = 30; try { if (f.containsKey(MediaFormat.KEY_FRAME_RATE)) r = (int) Math.ceil(f.getNumber(MediaFormat.KEY_FRAME_RATE).doubleValue()); } catch (Exception e) {}
                return covers(new PerformancePoint(w, h, r));
            }
            @Override public boolean equals(Object o) { return o instanceof PerformancePoint && ((PerformancePoint) o).mW == mW && ((PerformancePoint) o).mH == mH && ((PerformancePoint) o).mRate == mRate; }
            @Override public int hashCode() { return (mW * 31 + mH) * 31 + mRate; }
            @Override public String toString() { return "PerformancePoint(" + mW + "x" + mH + "@" + mRate + ")"; }
        }
    }

    public static final class AudioCapabilities {
        private static final int[] RATES = { 7350, 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000, 64000, 88200, 96000 };
        private final int mMaxChannels;
        AudioCapabilities(int maxChannels) { mMaxChannels = maxChannels; }
        protected AudioCapabilities() { this(8); }
        public Range<Integer> getBitrateRange() { return new Range<>(8000, 510000); }
        public int[] getSupportedSampleRates() { return RATES.clone(); }
        public Range<Integer>[] getSupportedSampleRateRanges() { Range<Integer>[] r = new Range[RATES.length]; for (int i = 0; i < RATES.length; i++) r[i] = new Range<>(RATES[i], RATES[i]); return r; }
        public boolean isSampleRateSupported(int rate) { for (int r : RATES) if (r == rate) return true; return false; }
        public int getMaxInputChannelCount() { return mMaxChannels; }
        public int getMinInputChannelCount() { return 1; }
        public Range<Integer>[] getInputChannelCountRanges() { return new Range[] { new Range<>(1, mMaxChannels) }; }
        public void getDefaultFormat(MediaFormat f) {}
        public boolean supportsFormat(MediaFormat f) {
            if (f.containsKey(MediaFormat.KEY_SAMPLE_RATE) && !isSampleRateSupported(f.getInteger(MediaFormat.KEY_SAMPLE_RATE))) return false;
            return !f.containsKey(MediaFormat.KEY_CHANNEL_COUNT) || (f.getInteger(MediaFormat.KEY_CHANNEL_COUNT) >= 1 && f.getInteger(MediaFormat.KEY_CHANNEL_COUNT) <= mMaxChannels);
        }
    }

    public static final class EncoderCapabilities {
        public static final int BITRATE_MODE_CQ = 0, BITRATE_MODE_VBR = 1, BITRATE_MODE_CBR = 2, BITRATE_MODE_CBR_FD = 3;
        protected EncoderCapabilities() {}
        public Range<Integer> getComplexityRange() { return new Range<>(0, 0); }
        public Range<Integer> getQualityRange() { return new Range<>(0, 0); }
        public boolean isBitrateModeSupported(int m) { return false; }
        public void getDefaultFormat(MediaFormat f) {}
        public boolean supportsFormat(MediaFormat f) { return false; }
    }
}
