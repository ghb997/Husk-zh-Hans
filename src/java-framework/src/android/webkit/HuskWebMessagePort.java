package android.webkit;

import android.os.Handler;
import java.util.ArrayList;
import java.util.HashMap;

/**
 * One end of a WebView message channel (createWebMessageChannel). Both ends start in Java; an end handed to the page through
 * postWebMessage becomes the page's MessagePort -- the port2 of a page-side MessageChannel whose port1 stays in
 * window.__huskPorts[id] -- so what the app posts on the other end arrives there, and what the page posts arrives at the app's
 * callback.
 */
final class HuskWebMessagePort extends WebMessagePort {
    private static int sNextId = 1;
    private static final boolean TRACE = System.getenv("TL_WEB_CONSOLE") != null;     /* the traffic in the log too */
    private static final HashMap<Integer, HuskWebMessagePort> sInPage = new HashMap<>();

    private final WebView mView;
    private HuskWebMessagePort mPeer;
    private WebMessageCallback mCallback;
    private Handler mCallbackHandler;
    private int mPageId;                                        // nonzero once this end is the page's
    private boolean mClosed;
    private final ArrayList<WebMessage> mPending = new ArrayList<>();   // arrived before a callback was set

    private HuskWebMessagePort(WebView v) { mView = v; }

    static WebMessagePort[] channel(WebView v) {
        HuskWebMessagePort a = new HuskWebMessagePort(v), b = new HuskWebMessagePort(v);
        a.mPeer = b; b.mPeer = a;
        return new WebMessagePort[] { a, b };
    }

    /** This end, handed to the page: its id there. */
    int transfer() {
        synchronized (sInPage) {
            if (mPageId == 0) { mPageId = sNextId++; sInPage.put(mPageId, this); }
            return mPageId;
        }
    }

    /** A message the page posted on its port of channel id: to the app's end. */
    static void fromPage(int id, String data) {
        HuskWebMessagePort p;
        synchronized (sInPage) { p = sInPage.get(id); }
        if (TRACE) android.util.Log.d("WebView", "port " + id + " page -> app: " + (data != null && data.length() > 300 ? data.substring(0, 300) + "..." : data));
        if (p != null && p.mPeer != null) p.mPeer.receive(new WebMessage(data));
    }

    @Override public void postMessage(WebMessage message) {
        if (mClosed) throw new IllegalStateException("Port is already closed");
        HuskWebMessagePort peer = mPeer;
        if (peer == null || message == null) return;
        if (TRACE) { String d = message.getData(); android.util.Log.d("WebView", "port " + peer.mPageId + " app -> " + (peer.mPageId != 0 ? "page" : "app") + ": " + (d != null && d.length() > 300 ? d.substring(0, 300) + "..." : d)); }
        if (peer.mPageId != 0) mView.huskPortToPage(peer.mPageId, message.getData());
        else peer.receive(message);
    }

    private void receive(final WebMessage m) {
        final WebMessageCallback cb;
        Handler h;
        synchronized (this) {
            if (mClosed) return;
            if (mCallback == null) { mPending.add(m); return; }
            cb = mCallback; h = mCallbackHandler;
        }
        (h != null ? h : mView.huskHandler()).post(() -> cb.onMessage(this, m));
    }

    @Override public void setWebMessageCallback(WebMessageCallback callback) { setWebMessageCallback(callback, null); }
    @Override public void setWebMessageCallback(WebMessageCallback callback, Handler handler) {
        ArrayList<WebMessage> pending;
        synchronized (this) {
            mCallback = callback; mCallbackHandler = handler;
            pending = new ArrayList<>(mPending); mPending.clear();
        }
        for (WebMessage m : pending) receive(m);
    }

    @Override public void close() {
        synchronized (this) { if (mClosed) return; mClosed = true; mPending.clear(); }
        if (mPageId != 0) { synchronized (sInPage) { sInPage.remove(mPageId); } mView.huskPortClose(mPageId); }
    }
}
