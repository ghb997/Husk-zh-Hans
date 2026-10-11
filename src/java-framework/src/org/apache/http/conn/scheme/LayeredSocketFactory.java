package org.apache.http.conn.scheme;

@Deprecated
public interface LayeredSocketFactory extends SocketFactory {
    java.net.Socket createSocket(java.net.Socket socket, String host, int port, boolean autoClose) throws java.io.IOException;
}
