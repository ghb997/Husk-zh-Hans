package android.opengl;

public class EGLConfig extends EGLObjectHandle {
    EGLConfig(long handle) { super(handle); }
    @Override public String toString() { return "EGLConfig 0x" + Long.toHexString(getNativeHandle()); }
}
