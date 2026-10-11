package android.opengl;

/** An EGL object by its handle; equal handles are the same object, as on Android. */
public abstract class EGLObjectHandle {
    private final long mHandle;
    /** Husk: the javax.microedition.khronos.egl object behind it. */
    Object husk;
    @Deprecated protected EGLObjectHandle(int handle) { mHandle = handle; }
    protected EGLObjectHandle(long handle) { mHandle = handle; }
    @Deprecated public int getHandle() { return (int) mHandle; }
    public long getNativeHandle() { return mHandle; }
    @Override public boolean equals(Object o) { return o != null && o.getClass() == getClass() && ((EGLObjectHandle) o).mHandle == mHandle; }
    @Override public int hashCode() { return (int) (mHandle ^ (mHandle >>> 32)); }
}
