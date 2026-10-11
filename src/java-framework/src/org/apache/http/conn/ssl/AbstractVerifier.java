package org.apache.http.conn.ssl;

import java.io.IOException;
import java.security.cert.Certificate;
import java.security.cert.CertificateParsingException;
import java.security.cert.X509Certificate;
import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import java.util.Locale;
import javax.net.ssl.SSLException;
import javax.net.ssl.SSLSession;
import javax.net.ssl.SSLSocket;

/** Apache's host name checks: the name against the certificate's DNS subject alternative names, else its common name. */
@Deprecated
public abstract class AbstractVerifier implements X509HostnameVerifier {
    public AbstractVerifier() {}

    public final void verify(String host, SSLSocket ssl) throws IOException {
        if (host == null) throw new NullPointerException("host to verify is null");
        SSLSession session = ssl.getSession();
        Certificate[] certs = session.getPeerCertificates();
        verify(host, (X509Certificate) certs[0]);
    }

    public final boolean verify(String host, SSLSession session) {
        try {
            Certificate[] certs = session.getPeerCertificates();
            verify(host, (X509Certificate) certs[0]);
            return true;
        } catch (SSLException e) { return false; }
        catch (Exception e) { return false; }
    }

    public final void verify(String host, X509Certificate cert) throws SSLException {
        String cn = getCN(cert);
        verify(host, cn == null ? null : new String[] { cn }, getDNSSubjectAlts(cert));
    }

    public final void verify(String host, String[] cns, String[] subjectAlts, boolean strictWithSubDomains) throws SSLException {
        List<String> names = new ArrayList<>();
        if (cns != null && cns.length > 0 && cns[0] != null) names.add(cns[0]);
        if (subjectAlts != null) for (String s : subjectAlts) if (s != null) names.add(s);
        if (names.isEmpty()) throw new SSLException("Certificate for <" + host + "> doesn't contain CN or DNS subjectAlt");
        String h = host.trim().toLowerCase(Locale.ENGLISH);
        for (String n : names) {
            String c = n.toLowerCase(Locale.ENGLISH);
            boolean wildcard = c.startsWith("*.") && c.lastIndexOf('.') > 1 && acceptableCountryWildcard(c) && !isIPAddress(host);
            boolean match;
            if (wildcard) {
                match = h.endsWith(c.substring(1));
                if (match && strictWithSubDomains) match = countDots(h) == countDots(c);
                else if (match) match = h.length() > c.length() - 1;
            } else match = h.equals(c);
            if (match) return;
        }
        throw new SSLException("hostname in certificate didn't match: <" + host + "> != " + names);
    }

    public static boolean acceptableCountryWildcard(String cn) {
        int dots = countDots(cn);
        return dots >= 2;
    }

    public static String[] getCNs(X509Certificate cert) {
        String cn = getCN(cert);
        return cn == null ? null : new String[] { cn };
    }

    private static String getCN(X509Certificate cert) {
        String dn = cert.getSubjectX500Principal().getName();
        for (String part : dn.split(",")) {
            String p = part.trim();
            if (p.regionMatches(true, 0, "CN=", 0, 3)) return p.substring(3);
        }
        return null;
    }

    public static String[] getDNSSubjectAlts(X509Certificate cert) {
        List<String> out = new ArrayList<>();
        Collection<List<?>> c = null;
        try { c = cert.getSubjectAlternativeNames(); } catch (CertificateParsingException e) { }
        if (c != null) for (List<?> e : c) if (e.size() >= 2 && Integer.valueOf(2).equals(e.get(0))) out.add((String) e.get(1));
        return out.isEmpty() ? null : out.toArray(new String[0]);
    }

    public static int countDots(String s) { int n = 0; for (int i = 0; i < s.length(); i++) if (s.charAt(i) == '.') n++; return n; }

    private static boolean isIPAddress(String h) { return h != null && (h.matches("[0-9.]+") || h.indexOf(':') >= 0); }
}
