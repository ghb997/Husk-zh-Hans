package org.apache.http.params;

@Deprecated
public final class HttpConnectionParams implements CoreConnectionPNames {
    private HttpConnectionParams() {}
    private static HttpParams check(HttpParams p) { if (p == null) throw new IllegalArgumentException("HTTP parameters may not be null"); return p; }
    public static int getSoTimeout(HttpParams p) { return check(p).getIntParameter(SO_TIMEOUT, 0); }
    public static void setSoTimeout(HttpParams p, int timeout) { check(p).setIntParameter(SO_TIMEOUT, timeout); }
    public static boolean getTcpNoDelay(HttpParams p) { return check(p).getBooleanParameter(TCP_NODELAY, true); }
    public static void setTcpNoDelay(HttpParams p, boolean value) { check(p).setBooleanParameter(TCP_NODELAY, value); }
    public static int getSocketBufferSize(HttpParams p) { return check(p).getIntParameter(SOCKET_BUFFER_SIZE, -1); }
    public static void setSocketBufferSize(HttpParams p, int size) { check(p).setIntParameter(SOCKET_BUFFER_SIZE, size); }
    public static int getLinger(HttpParams p) { return check(p).getIntParameter(SO_LINGER, -1); }
    public static void setLinger(HttpParams p, int value) { check(p).setIntParameter(SO_LINGER, value); }
    public static int getConnectionTimeout(HttpParams p) { return check(p).getIntParameter(CONNECTION_TIMEOUT, 0); }
    public static void setConnectionTimeout(HttpParams p, int timeout) { check(p).setIntParameter(CONNECTION_TIMEOUT, timeout); }
    public static boolean isStaleCheckingEnabled(HttpParams p) { return check(p).getBooleanParameter(STALE_CONNECTION_CHECK, true); }
    public static void setStaleCheckingEnabled(HttpParams p, boolean value) { check(p).setBooleanParameter(STALE_CONNECTION_CHECK, value); }
}
