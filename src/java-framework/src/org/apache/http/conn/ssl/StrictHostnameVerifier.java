package org.apache.http.conn.ssl;

@Deprecated
public class StrictHostnameVerifier extends AbstractVerifier {
    public StrictHostnameVerifier() {}
    public final void verify(String host, String[] cns, String[] subjectAlts) throws javax.net.ssl.SSLException { verify(host, cns, subjectAlts, true); }
    public final String toString() { return "STRICT"; }
}
