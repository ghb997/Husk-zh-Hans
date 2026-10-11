package android.content;

import android.net.Uri;
import java.util.ArrayList;
import java.util.List;

/** Matches content URIs (authority, then path segments, with # for a number and * for any text) to codes, as the platform's. */
public class UriMatcher {
    public static final int NO_MATCH = -1;
    private static final int EXACT = 0, NUMBER = 1, TEXT = 2;
    private int mCode;
    private final int mWhich;
    private final String mText;
    private final ArrayList<UriMatcher> mChildren = new ArrayList<>();
    public UriMatcher(int code) { this(code, EXACT, null); }
    UriMatcher() { this(NO_MATCH, EXACT, null); }
    private UriMatcher(int code, int which, String text) { mCode = code; mWhich = which; mText = text; }
    public void addURI(String authority, String path, int code) {
        if (code < 0) throw new IllegalArgumentException("code " + code + " is invalid: it must be positive");
        String[] tokens = null;
        if (path != null) {
            String p = path;
            if (p.length() > 1 && p.charAt(0) == '/') p = p.substring(1);   /* a leading slash is allowed */
            tokens = p.split("/");
        }
        int n = tokens != null ? tokens.length : 0;
        UriMatcher node = this;
        for (int i = -1; i < n; i++) {
            String token = i < 0 ? authority : tokens[i];
            UriMatcher child = null;
            for (UriMatcher c : node.mChildren) if (token.equals(c.mText)) { child = c; break; }
            if (child == null) {
                child = new UriMatcher(NO_MATCH, token.equals("#") ? NUMBER : token.equals("*") ? TEXT : EXACT, token);
                node.mChildren.add(child);
            }
            node = child;
        }
        node.mCode = code;
    }
    public int match(Uri uri) {
        List<String> segs = uri.getPathSegments();
        int n = segs.size();
        UriMatcher node = this;
        if (n == 0 && uri.getAuthority() == null) return mCode;
        for (int i = -1; i < n; i++) {
            String u = i < 0 ? uri.getAuthority() : segs.get(i);
            ArrayList<UriMatcher> list = node.mChildren;
            if (list.isEmpty()) break;
            node = null;
            for (UriMatcher c : list) {
                switch (c.mWhich) {
                case EXACT: if (c.mText.equals(u)) node = c; break;
                case NUMBER: {
                    int len = u == null ? 0 : u.length(); boolean ok = len > 0;
                    for (int k = 0; k < len; k++) { char ch = u.charAt(k); if (ch < '0' || ch > '9') { ok = false; break; } }
                    if (ok) node = c;
                    break;
                }
                case TEXT: node = c; break;
                }
                if (node != null) break;
            }
            if (node == null) return NO_MATCH;
        }
        return node.mCode;
    }
}
