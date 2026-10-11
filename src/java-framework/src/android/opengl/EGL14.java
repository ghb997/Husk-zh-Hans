package android.opengl;

import java.util.IdentityHashMap;
import javax.microedition.khronos.egl.EGL10;

/**
 * EGL 1.4 for Java: the same EGL as EGL10's (javax.microedition.khronos.egl, Husk's contexts and surfaces from husk.EGLNative),
 * with android.opengl handles. Each EGL10 object gets one handle object for as long as it lives.
 */
public class EGL14 {
    public static final int EGL_ALPHA_MASK_SIZE = 12350;
    public static final int EGL_ALPHA_SIZE = 12321;
    public static final int EGL_BACK_BUFFER = 12420;
    public static final int EGL_BAD_ACCESS = 12290;
    public static final int EGL_BAD_ALLOC = 12291;
    public static final int EGL_BAD_ATTRIBUTE = 12292;
    public static final int EGL_BAD_CONFIG = 12293;
    public static final int EGL_BAD_CONTEXT = 12294;
    public static final int EGL_BAD_CURRENT_SURFACE = 12295;
    public static final int EGL_BAD_DISPLAY = 12296;
    public static final int EGL_BAD_MATCH = 12297;
    public static final int EGL_BAD_NATIVE_PIXMAP = 12298;
    public static final int EGL_BAD_NATIVE_WINDOW = 12299;
    public static final int EGL_BAD_PARAMETER = 12300;
    public static final int EGL_BAD_SURFACE = 12301;
    public static final int EGL_BIND_TO_TEXTURE_RGB = 12345;
    public static final int EGL_BIND_TO_TEXTURE_RGBA = 12346;
    public static final int EGL_BLUE_SIZE = 12322;
    public static final int EGL_BUFFER_DESTROYED = 12437;
    public static final int EGL_BUFFER_PRESERVED = 12436;
    public static final int EGL_BUFFER_SIZE = 12320;
    public static final int EGL_CLIENT_APIS = 12429;
    public static final int EGL_COLOR_BUFFER_TYPE = 12351;
    public static final int EGL_CONFIG_CAVEAT = 12327;
    public static final int EGL_CONFIG_ID = 12328;
    public static final int EGL_CONFORMANT = 12354;
    public static final int EGL_CONTEXT_CLIENT_TYPE = 12439;
    public static final int EGL_CONTEXT_CLIENT_VERSION = 12440;
    public static final int EGL_CONTEXT_LOST = 12302;
    public static final int EGL_CORE_NATIVE_ENGINE = 12379;
    public static final int EGL_DEFAULT_DISPLAY = 0;
    public static final int EGL_DEPTH_SIZE = 12325;
    public static final int EGL_DISPLAY_SCALING = 10000;
    public static final int EGL_DRAW = 12377;
    public static final int EGL_EXTENSIONS = 12373;
    public static final int EGL_FALSE = 0;
    public static final int EGL_GREEN_SIZE = 12323;
    public static final int EGL_HEIGHT = 12374;
    public static final int EGL_HORIZONTAL_RESOLUTION = 12432;
    public static final int EGL_LARGEST_PBUFFER = 12376;
    public static final int EGL_LEVEL = 12329;
    public static final int EGL_LUMINANCE_BUFFER = 12431;
    public static final int EGL_LUMINANCE_SIZE = 12349;
    public static final int EGL_MATCH_NATIVE_PIXMAP = 12353;
    public static final int EGL_MAX_PBUFFER_HEIGHT = 12330;
    public static final int EGL_MAX_PBUFFER_PIXELS = 12331;
    public static final int EGL_MAX_PBUFFER_WIDTH = 12332;
    public static final int EGL_MAX_SWAP_INTERVAL = 12348;
    public static final int EGL_MIN_SWAP_INTERVAL = 12347;
    public static final int EGL_MIPMAP_LEVEL = 12419;
    public static final int EGL_MIPMAP_TEXTURE = 12418;
    public static final int EGL_MULTISAMPLE_RESOLVE = 12441;
    public static final int EGL_MULTISAMPLE_RESOLVE_BOX = 12443;
    public static final int EGL_MULTISAMPLE_RESOLVE_BOX_BIT = 512;
    public static final int EGL_MULTISAMPLE_RESOLVE_DEFAULT = 12442;
    public static final int EGL_NATIVE_RENDERABLE = 12333;
    public static final int EGL_NATIVE_VISUAL_ID = 12334;
    public static final int EGL_NATIVE_VISUAL_TYPE = 12335;
    public static final int EGL_NONE = 12344;
    public static final int EGL_NON_CONFORMANT_CONFIG = 12369;
    public static final int EGL_NOT_INITIALIZED = 12289;
    public static final int EGL_NO_TEXTURE = 12380;
    public static final int EGL_OPENGL_API = 12450;
    public static final int EGL_OPENGL_BIT = 8;
    public static final int EGL_OPENGL_ES2_BIT = 4;
    public static final int EGL_OPENGL_ES_API = 12448;
    public static final int EGL_OPENGL_ES_BIT = 1;
    public static final int EGL_OPENVG_API = 12449;
    public static final int EGL_OPENVG_BIT = 2;
    public static final int EGL_OPENVG_IMAGE = 12438;
    public static final int EGL_PBUFFER_BIT = 1;
    public static final int EGL_PIXEL_ASPECT_RATIO = 12434;
    public static final int EGL_PIXMAP_BIT = 2;
    public static final int EGL_READ = 12378;
    public static final int EGL_RED_SIZE = 12324;
    public static final int EGL_RENDERABLE_TYPE = 12352;
    public static final int EGL_RENDER_BUFFER = 12422;
    public static final int EGL_RGB_BUFFER = 12430;
    public static final int EGL_SAMPLES = 12337;
    public static final int EGL_SAMPLE_BUFFERS = 12338;
    public static final int EGL_SINGLE_BUFFER = 12421;
    public static final int EGL_SLOW_CONFIG = 12368;
    public static final int EGL_STENCIL_SIZE = 12326;
    public static final int EGL_SUCCESS = 12288;
    public static final int EGL_SURFACE_TYPE = 12339;
    public static final int EGL_SWAP_BEHAVIOR = 12435;
    public static final int EGL_SWAP_BEHAVIOR_PRESERVED_BIT = 1024;
    public static final int EGL_TEXTURE_2D = 12383;
    public static final int EGL_TEXTURE_FORMAT = 12416;
    public static final int EGL_TEXTURE_RGB = 12381;
    public static final int EGL_TEXTURE_RGBA = 12382;
    public static final int EGL_TEXTURE_TARGET = 12417;
    public static final int EGL_TRANSPARENT_BLUE_VALUE = 12341;
    public static final int EGL_TRANSPARENT_GREEN_VALUE = 12342;
    public static final int EGL_TRANSPARENT_RED_VALUE = 12343;
    public static final int EGL_TRANSPARENT_RGB = 12370;
    public static final int EGL_TRANSPARENT_TYPE = 12340;
    public static final int EGL_TRUE = 1;
    public static final int EGL_VENDOR = 12371;
    public static final int EGL_VERSION = 12372;
    public static final int EGL_VERTICAL_RESOLUTION = 12433;
    public static final int EGL_VG_ALPHA_FORMAT = 12424;
    public static final int EGL_VG_ALPHA_FORMAT_NONPRE = 12427;
    public static final int EGL_VG_ALPHA_FORMAT_PRE = 12428;
    public static final int EGL_VG_ALPHA_FORMAT_PRE_BIT = 64;
    public static final int EGL_VG_COLORSPACE = 12423;
    public static final int EGL_VG_COLORSPACE_LINEAR = 12426;
    public static final int EGL_VG_COLORSPACE_LINEAR_BIT = 32;
    public static final int EGL_VG_COLORSPACE_sRGB = 12425;
    public static final int EGL_WIDTH = 12375;
    public static final int EGL_WINDOW_BIT = 4;

    public static final EGLContext EGL_NO_CONTEXT = new EGLContext(0);
    public static final EGLDisplay EGL_NO_DISPLAY = new EGLDisplay(0);
    public static final EGLSurface EGL_NO_SURFACE = new EGLSurface(0);
    private static final EGL10 E = (EGL10) javax.microedition.khronos.egl.EGLContext.getEGL();
    private static final IdentityHashMap<Object, EGLObjectHandle> sHandles = new IdentityHashMap<>();
    private static long sNext = 0x1000;
    public EGL14() {}
    private static synchronized <T extends EGLObjectHandle> T wrap(Object o, Class<T> kind) {
        if (o == null || o == EGL10.EGL_NO_CONTEXT || o == EGL10.EGL_NO_SURFACE || o == EGL10.EGL_NO_DISPLAY) {
            return kind.cast(kind == EGLContext.class ? EGL_NO_CONTEXT : kind == EGLSurface.class ? EGL_NO_SURFACE : kind == EGLDisplay.class ? EGL_NO_DISPLAY : null);
        }
        EGLObjectHandle h = sHandles.get(o);
        if (h == null) {
            long id = sNext; sNext += 8;
            h = kind == EGLContext.class ? new EGLContext(id) : kind == EGLSurface.class ? new EGLSurface(id) : kind == EGLDisplay.class ? new EGLDisplay(id) : new EGLConfig(id);
            h.husk = o; sHandles.put(o, h);
        }
        return kind.cast(h);
    }
    private static synchronized void forget(EGLObjectHandle h) { if (h != null && h.husk != null) sHandles.remove(h.husk); }
    private static javax.microedition.khronos.egl.EGLDisplay d(EGLDisplay x) { return x != null && x.husk != null ? (javax.microedition.khronos.egl.EGLDisplay) x.husk : E.eglGetDisplay(EGL10.EGL_DEFAULT_DISPLAY); }
    private static javax.microedition.khronos.egl.EGLConfig c(EGLConfig x) { return x != null ? (javax.microedition.khronos.egl.EGLConfig) x.husk : null; }
    private static javax.microedition.khronos.egl.EGLContext ctx(EGLContext x) { return x != null && x.husk != null ? (javax.microedition.khronos.egl.EGLContext) x.husk : EGL10.EGL_NO_CONTEXT; }
    private static javax.microedition.khronos.egl.EGLSurface s(EGLSurface x) { return x != null && x.husk != null ? (javax.microedition.khronos.egl.EGLSurface) x.husk : EGL10.EGL_NO_SURFACE; }
    private static int[] tail(int[] a, int off) { if (a == null) return null; int[] r = new int[a.length - off]; System.arraycopy(a, off, r, 0, r.length); return r; }

    public static int eglGetError() { return E.eglGetError(); }
    public static EGLDisplay eglGetDisplay(int id) { return wrap(E.eglGetDisplay(EGL10.EGL_DEFAULT_DISPLAY), EGLDisplay.class); }
    public static EGLDisplay eglGetDisplay(long id) { return eglGetDisplay(0); }
    public static boolean eglInitialize(EGLDisplay dpy, int[] major, int majorOff, int[] minor, int minorOff) {
        int[] v = new int[2]; boolean ok = E.eglInitialize(d(dpy), v);
        if (major != null) major[majorOff] = v[0]; if (minor != null) minor[minorOff] = v[1]; return ok;
    }
    public static boolean eglTerminate(EGLDisplay dpy) { return E.eglTerminate(d(dpy)); }
    public static String eglQueryString(EGLDisplay dpy, int name) { return E.eglQueryString(d(dpy), name); }
    public static boolean eglGetConfigs(EGLDisplay dpy, EGLConfig[] configs, int off, int size, int[] num, int numOff) {
        return eglChooseConfig(dpy, null, 0, configs, off, size, num, numOff);
    }
    public static boolean eglChooseConfig(EGLDisplay dpy, int[] attribs, int attribsOff, EGLConfig[] configs, int off, int size, int[] num, int numOff) {
        javax.microedition.khronos.egl.EGLConfig[] cs = new javax.microedition.khronos.egl.EGLConfig[Math.max(1, size)];
        int[] n = new int[1];
        boolean ok = E.eglChooseConfig(d(dpy), tail(attribs, attribsOff), cs, size, n);
        if (configs != null) for (int i = 0; i < Math.min(n[0], size); i++) configs[off + i] = wrap(cs[i], EGLConfig.class);
        if (num != null) num[numOff] = n[0];
        return ok;
    }
    public static boolean eglGetConfigAttrib(EGLDisplay dpy, EGLConfig cfg, int attr, int[] value, int off) {
        int[] v = new int[1]; boolean ok = E.eglGetConfigAttrib(d(dpy), c(cfg), attr, v); value[off] = v[0]; return ok;
    }
    public static EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig cfg, Object win, int[] attribs, int off) {
        if (!(win instanceof android.view.Surface || win instanceof android.view.SurfaceView || win instanceof android.view.SurfaceHolder || win instanceof android.graphics.SurfaceTexture))
            throw new UnsupportedOperationException("eglCreateWindowSurface() can only be called with an instance of Surface, SurfaceView, SurfaceTexture or SurfaceHolder at the moment, this will be fixed later.");
        return wrap(E.eglCreateWindowSurface(d(dpy), c(cfg), win, tail(attribs, off)), EGLSurface.class);
    }
    public static EGLSurface eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig cfg, int[] attribs, int off) { return wrap(E.eglCreatePbufferSurface(d(dpy), c(cfg), tail(attribs, off)), EGLSurface.class); }
    public static EGLSurface eglCreatePixmapSurface(EGLDisplay dpy, EGLConfig cfg, int pixmap, int[] attribs, int off) { return EGL_NO_SURFACE; }
    public static EGLSurface eglCreatePbufferFromClientBuffer(EGLDisplay dpy, int type, int buffer, EGLConfig cfg, int[] attribs, int off) { return EGL_NO_SURFACE; }
    public static EGLSurface eglCreatePbufferFromClientBuffer(EGLDisplay dpy, int type, long buffer, EGLConfig cfg, int[] attribs, int off) { return EGL_NO_SURFACE; }
    public static boolean eglDestroySurface(EGLDisplay dpy, EGLSurface s) { boolean ok = E.eglDestroySurface(d(dpy), s(s)); forget(s); return ok; }
    public static boolean eglQuerySurface(EGLDisplay dpy, EGLSurface s, int attr, int[] value, int off) { int[] v = new int[1]; boolean ok = E.eglQuerySurface(d(dpy), s(s), attr, v); value[off] = v[0]; return ok; }
    public static boolean eglBindAPI(int api) { return api == EGL_OPENGL_ES_API; }
    public static int eglQueryAPI() { return EGL_OPENGL_ES_API; }
    public static boolean eglWaitClient() { return true; }
    public static boolean eglReleaseThread() { return E.eglReleaseThread(); }
    public static boolean eglSurfaceAttrib(EGLDisplay dpy, EGLSurface s, int attr, int value) { return true; }
    public static boolean eglBindTexImage(EGLDisplay dpy, EGLSurface s, int buffer) { return true; }
    public static boolean eglReleaseTexImage(EGLDisplay dpy, EGLSurface s, int buffer) { return true; }
    public static boolean eglSwapInterval(EGLDisplay dpy, int interval) { return true; }
    public static EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig cfg, EGLContext share, int[] attribs, int off) { return wrap(E.eglCreateContext(d(dpy), c(cfg), ctx(share), tail(attribs, off)), EGLContext.class); }
    public static boolean eglDestroyContext(EGLDisplay dpy, EGLContext c) { boolean ok = E.eglDestroyContext(d(dpy), ctx(c)); forget(c); return ok; }
    public static boolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext c) { return E.eglMakeCurrent(d(dpy), s(draw), s(read), ctx(c)); }
    public static EGLContext eglGetCurrentContext() { return wrap(E.eglGetCurrentContext(), EGLContext.class); }
    public static EGLSurface eglGetCurrentSurface(int which) { return wrap(E.eglGetCurrentSurface(which), EGLSurface.class); }
    public static EGLDisplay eglGetCurrentDisplay() { return wrap(E.eglGetCurrentDisplay(), EGLDisplay.class); }
    public static boolean eglQueryContext(EGLDisplay dpy, EGLContext c, int attr, int[] value, int off) { int[] v = new int[1]; boolean ok = E.eglQueryContext(d(dpy), ctx(c), attr, v); value[off] = v[0]; return ok; }
    public static boolean eglWaitGL() { return E.eglWaitGL(); }
    public static boolean eglWaitNative(int engine) { return true; }
    public static boolean eglSwapBuffers(EGLDisplay dpy, EGLSurface s) { return E.eglSwapBuffers(d(dpy), s(s)); }
    public static boolean eglCopyBuffers(EGLDisplay dpy, EGLSurface s, int target) { return true; }
}
