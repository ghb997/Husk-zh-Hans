package org.apache.http.conn.scheme;

@Deprecated
public interface HostNameResolver {
    java.net.InetAddress resolve(String hostname) throws java.io.IOException;
}
