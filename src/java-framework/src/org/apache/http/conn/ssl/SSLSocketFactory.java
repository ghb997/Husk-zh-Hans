package org.apache.http.conn.ssl;

import java.io.IOException;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.security.KeyStore;
import javax.net.ssl.HttpsURLConnection;
import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLSocket;
import org.apache.http.conn.ConnectTimeoutException;
import org.apache.http.conn.scheme.HostNameResolver;
import org.apache.http.conn.scheme.LayeredSocketFactory;
import org.apache.http.params.HttpConnectionParams;
import org.apache.http.params.HttpParams;

/** Apache's TLS socket factory over the platform's (javax.net.ssl) one, checking host names with the chosen verifier. */
@Deprecated
public class SSLSocketFactory implements LayeredSocketFactory {
    public static final String TLS = "TLS", SSL = "SSL", SSLV2 = "SSLv2";
    public static final X509HostnameVerifier ALLOW_ALL_HOSTNAME_VERIFIER = new AllowAllHostnameVerifier();
    public static final X509HostnameVerifier BROWSER_COMPATIBLE_HOSTNAME_VERIFIER = new BrowserCompatHostnameVerifier();
    public static final X509HostnameVerifier STRICT_HOSTNAME_VERIFIER = new StrictHostnameVerifier();

    private static SSLSocketFactory sDefault;
    private final javax.net.ssl.SSLSocketFactory socketfactory;
    private final HostNameResolver nameResolver;
    private X509HostnameVerifier hostnameVerifier = BROWSER_COMPATIBLE_HOSTNAME_VERIFIER;

    public static synchronized SSLSocketFactory getSocketFactory() {
        if (sDefault == null) sDefault = new SSLSocketFactory(HttpsURLConnection.getDefaultSSLSocketFactory());
        return sDefault;
    }

    public SSLSocketFactory(String algorithm, KeyStore keystore, String keystorePassword, KeyStore truststore, java.security.SecureRandom random,
                            HostNameResolver nameResolver) throws java.security.GeneralSecurityException {
        SSLContext ctx = SSLContext.getInstance(algorithm == null ? TLS : algorithm);
        javax.net.ssl.KeyManagerFactory kmf = null;
        if (keystore != null) {
            kmf = javax.net.ssl.KeyManagerFactory.getInstance(javax.net.ssl.KeyManagerFactory.getDefaultAlgorithm());
            kmf.init(keystore, keystorePassword != null ? keystorePassword.toCharArray() : null);
        }
        javax.net.ssl.TrustManagerFactory tmf = null;
        if (truststore != null) {
            tmf = javax.net.ssl.TrustManagerFactory.getInstance(javax.net.ssl.TrustManagerFactory.getDefaultAlgorithm());
            tmf.init(truststore);
        }
        ctx.init(kmf != null ? kmf.getKeyManagers() : null, tmf != null ? tmf.getTrustManagers() : null, random);
        this.socketfactory = ctx.getSocketFactory();
        this.nameResolver = nameResolver;
    }
    public SSLSocketFactory(KeyStore keystore, String keystorePassword, KeyStore truststore) throws java.security.GeneralSecurityException { this(TLS, keystore, keystorePassword, truststore, null, null); }
    public SSLSocketFactory(KeyStore keystore, String keystorePassword) throws java.security.GeneralSecurityException { this(TLS, keystore, keystorePassword, null, null, null); }
    public SSLSocketFactory(KeyStore truststore) throws java.security.GeneralSecurityException { this(TLS, null, null, truststore, null, null); }
    public SSLSocketFactory(javax.net.ssl.SSLSocketFactory socketfactory) { this.socketfactory = socketfactory; this.nameResolver = null; }

    public Socket createSocket() throws IOException { return socketfactory.createSocket(); }

    public Socket connectSocket(Socket sock, String host, int port, InetAddress localAddress, int localPort, HttpParams params) throws IOException {
        if (host == null) throw new IllegalArgumentException("Target host may not be null.");
        if (params == null) throw new IllegalArgumentException("Parameters may not be null.");
        SSLSocket ssl = (SSLSocket) (sock != null ? sock : createSocket());
        if (localAddress != null || localPort > 0) ssl.bind(new InetSocketAddress(localAddress, localPort < 0 ? 0 : localPort));
        int connTimeout = HttpConnectionParams.getConnectionTimeout(params), soTimeout = HttpConnectionParams.getSoTimeout(params);
        InetSocketAddress remote = nameResolver != null ? new InetSocketAddress(nameResolver.resolve(host), port) : new InetSocketAddress(host, port);
        try { ssl.connect(remote, connTimeout); }
        catch (SocketTimeoutException e) { throw new ConnectTimeoutException("Connect to " + remote + " timed out"); }
        ssl.setSoTimeout(soTimeout);
        try { hostnameVerifier.verify(host, ssl); }
        catch (IOException e) { try { ssl.close(); } catch (Exception x) { } throw e; }
        return ssl;
    }

    public boolean isSecure(Socket sock) {
        if (sock == null) throw new IllegalArgumentException("Socket may not be null.");
        if (!(sock instanceof SSLSocket)) throw new IllegalArgumentException("Socket not created by this factory.");
        if (sock.isClosed()) throw new IllegalArgumentException("Socket is closed.");
        return true;
    }

    public Socket createSocket(Socket socket, String host, int port, boolean autoClose) throws IOException {
        SSLSocket ssl = (SSLSocket) socketfactory.createSocket(socket, host, port, autoClose);
        hostnameVerifier.verify(host, ssl);
        return ssl;
    }

    public void setHostnameVerifier(X509HostnameVerifier v) { if (v == null) throw new IllegalArgumentException("Hostname verifier may not be null"); hostnameVerifier = v; }
    public X509HostnameVerifier getHostnameVerifier() { return hostnameVerifier; }
}
