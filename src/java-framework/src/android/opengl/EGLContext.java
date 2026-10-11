package android.opengl;

public class EGLContext extends EGLObjectHandle {
    EGLContext(long handle) { super(handle); }
    @Override public String toString() { return "EGLContext 0x" + Long.toHexString(getNativeHandle()); }
}
