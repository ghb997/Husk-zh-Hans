package android.os;

/** An error a service defines for itself, carried with its code across a binder call. */
public class ServiceSpecificException extends RuntimeException {
    public final int errorCode;
    public ServiceSpecificException(int errorCode, String message) { super(message); this.errorCode = errorCode; }
    public ServiceSpecificException(int errorCode) { this.errorCode = errorCode; }
    @Override public String toString() { return super.toString() + " (code " + errorCode + ")"; }
}
