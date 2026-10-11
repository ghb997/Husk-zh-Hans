package android.app;

import android.content.Intent;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.os.Looper;
import android.os.Message;

/** Handles each start intent on one worker thread, in order, and stops itself when the last one is done (as Android's). */
public abstract class IntentService extends Service {
    private final String mName;
    private volatile Looper mLooper;
    private volatile Handler mHandler;
    private boolean mRedelivery;
    public IntentService(String name) { mName = name; }
    IntentService() { this(null); }
    public void setIntentRedelivery(boolean enabled) { mRedelivery = enabled; }
    @Override public void onCreate() {
        super.onCreate();
        HandlerThread t = new HandlerThread("IntentService[" + mName + "]");
        t.start();
        mLooper = t.getLooper();
        mHandler = new Handler(mLooper) {
            @Override public void handleMessage(Message m) { onHandleIntent((Intent) m.obj); stopSelf(m.arg1); }
        };
    }
    @Override public void onStart(Intent intent, int startId) { Message m = mHandler.obtainMessage(); m.arg1 = startId; m.obj = intent; mHandler.sendMessage(m); }
    @Override public int onStartCommand(Intent intent, int flags, int startId) { onStart(intent, startId); return mRedelivery ? START_REDELIVER_INTENT : START_NOT_STICKY; }
    @Override public void onDestroy() { if (mLooper != null) mLooper.quit(); }
    @Override public IBinder onBind(Intent intent) { return null; }
    protected abstract void onHandleIntent(Intent intent);
}
