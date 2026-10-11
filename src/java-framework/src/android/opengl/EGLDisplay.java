package android.opengl;

public class EGLDisplay extends EGLObjectHandle {
    EGLDisplay(long handle) { super(handle); }
    @Override public String toString() { return "EGLDisplay 0x" + Long.toHexString(getNativeHandle()); }
}
