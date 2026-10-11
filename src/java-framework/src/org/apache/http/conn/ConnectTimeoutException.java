package org.apache.http.conn;

@Deprecated
public class ConnectTimeoutException extends java.io.InterruptedIOException {
    public ConnectTimeoutException() { super(); }
    public ConnectTimeoutException(String message) { super(message); }
}
