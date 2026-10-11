package org.apache.http.conn.ssl;

import java.io.IOException;
import java.security.cert.X509Certificate;
import javax.net.ssl.SSLException;
import javax.net.ssl.SSLSocket;

@Deprecated
public interface X509HostnameVerifier extends javax.net.ssl.HostnameVerifier {
    void verify(String host, SSLSocket ssl) throws IOException;
    void verify(String host, X509Certificate cert) throws SSLException;
    void verify(String host, String[] cns, String[] subjectAlts) throws SSLException;
}
