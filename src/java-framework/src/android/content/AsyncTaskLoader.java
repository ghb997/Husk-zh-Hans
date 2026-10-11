package android.content;

import android.os.Handler;
import android.os.Looper;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executor;
import java.util.concurrent.Executors;

/** Loads on a background thread and delivers on the main thread; a newer load replaces one still running. */
public abstract class AsyncTaskLoader<D> extends Loader<D> {
    private static final Executor POOL = Executors.newCachedThreadPool(r -> { Thread t = new Thread(r, "AsyncTaskLoader"); t.setDaemon(true); return t; });
    private final Executor mExecutor;
    private final Handler mMain = new Handler(Looper.getMainLooper());
    private final class Task implements Runnable {
        volatile boolean canceled;
        final CountDownLatch done = new CountDownLatch(1);
        public void run() {
            D data = null; boolean cancelled = false;
            try { data = onLoadInBackground(); }
            catch (android.os.OperationCanceledException e) { cancelled = true; }
            finally { done.countDown(); }
            final D d = data; final boolean c = cancelled || canceled;
            mMain.post(() -> finish(this, d, c));
        }
    }
    private volatile Task mTask;
    private Task mCancelling;
    public AsyncTaskLoader(Context c) { this(c, POOL); }
    public AsyncTaskLoader(Context c, Executor e) { super(c); mExecutor = e != null ? e : POOL; }
    public void setUpdateThrottle(long ms) {}
    @Override protected void onForceLoad() { super.onForceLoad(); cancelLoad(); mTask = new Task(); mExecutor.execute(mTask); }
    @Override protected boolean onCancelLoad() {
        Task t = mTask;
        if (t == null) return false;
        t.canceled = true; mTask = null; mCancelling = t;
        cancelLoadInBackground();
        return true;
    }
    private void finish(Task t, D data, boolean canceled) {
        if (t != mTask || canceled) {
            onCanceled(data);
            if (t == mCancelling) { mCancelling = null; rollbackContentChanged(); deliverCancellation(); }
            return;
        }
        mTask = null;
        if (isAbandoned()) { onCanceled(data); return; }
        commitContentChanged();
        deliverResult(data);
    }
    public abstract D loadInBackground();
    protected D onLoadInBackground() { return loadInBackground(); }
    public void cancelLoadInBackground() {}
    public boolean isLoadInBackgroundCanceled() { Task t = mCancelling; return t != null && t.canceled; }
    public void onCanceled(D data) {}
    public void waitForLoader() { Task t = mTask; if (t != null) try { t.done.await(); } catch (InterruptedException e) { Thread.currentThread().interrupt(); } }
}
