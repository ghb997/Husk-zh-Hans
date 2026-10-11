package android.opengl;

public class EGLSurface extends EGLObjectHandle {
    EGLSurface(long handle) { super(handle); }
    @Override public String toString() { return "EGLSurface 0x" + Long.toHexString(getNativeHandle()); }
}
