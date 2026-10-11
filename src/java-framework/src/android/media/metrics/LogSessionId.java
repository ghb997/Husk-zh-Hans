package android.media.metrics;

/** A media metrics session id; Husk has no metrics service, so sessions are only ever NONE or what the app made. */
public final class LogSessionId {
    public static final LogSessionId LOG_SESSION_ID_NONE = new LogSessionId("");
    private final String mId;
    public LogSessionId(String id) { mId = id == null ? "" : id; }
    public String getStringId() { return mId; }
    @Override public boolean equals(Object o) { return o instanceof LogSessionId && ((LogSessionId) o).mId.equals(mId); }
    @Override public int hashCode() { return mId.hashCode(); }
    @Override public String toString() { return mId; }
}
