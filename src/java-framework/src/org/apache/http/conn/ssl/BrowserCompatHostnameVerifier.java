package org.apache.http.conn.ssl;

@Deprecated
public class BrowserCompatHostnameVerifier extends AbstractVerifier {
    public BrowserCompatHostnameVerifier() {}
    public final void verify(String host, String[] cns, String[] subjectAlts) throws javax.net.ssl.SSLException { verify(host, cns, subjectAlts, false); }
    public final String toString() { return "BROWSER_COMPATIBLE"; }
}
