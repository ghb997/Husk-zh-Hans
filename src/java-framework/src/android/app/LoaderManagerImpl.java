package android.app;

import android.content.Loader;
import android.os.Bundle;

/** The loaders of one activity or fragment, started and stopped with it. */
final class LoaderManagerImpl extends LoaderManager {
    private final class Info implements Loader.OnLoadCompleteListener<Object> {
        final int id; Bundle args; LoaderCallbacks<Object> callbacks; Loader<Object> loader; boolean haveData; Object data;
        Info(int id, Bundle args, LoaderCallbacks<Object> cb) { this.id = id; this.args = args; this.callbacks = cb; }
        public void onLoadComplete(Loader<Object> l, Object d) {
            if (mInfos.get(id) != this) return;
            data = d; haveData = true;
            if (mStarted && callbacks != null) callbacks.onLoadFinished(l, d);
        }
        void destroy() {
            if (haveData && callbacks != null) callbacks.onLoaderReset(loader);
            if (loader != null) { loader.unregisterListener(this); loader.reset(); }
        }
    }
    private final android.util.SparseArray<Info> mInfos = new android.util.SparseArray<>();
    private boolean mStarted;
    LoaderManagerImpl(boolean started) { mStarted = started; }
    @SuppressWarnings("unchecked")
    private Info create(int id, Bundle args, LoaderCallbacks<?> cb) {
        Info i = new Info(id, args, (LoaderCallbacks<Object>) cb);
        i.loader = (Loader<Object>) i.callbacks.onCreateLoader(id, args);
        if (i.loader == null) throw new IllegalArgumentException("Object returned from onCreateLoader must not be null");
        i.loader.registerListener(id, i);
        mInfos.put(id, i);
        if (mStarted) i.loader.startLoading();
        return i;
    }
    @Override @SuppressWarnings("unchecked") public <D> Loader<D> initLoader(int id, Bundle args, LoaderCallbacks<D> cb) {
        Info i = mInfos.get(id);
        if (i == null) return (Loader<D>) create(id, args, cb).loader;
        i.callbacks = (LoaderCallbacks<Object>) cb;
        if (i.haveData && mStarted) cb.onLoadFinished((Loader<D>) (Loader<?>) i.loader, (D) i.data);
        return (Loader<D>) (Loader<?>) i.loader;
    }
    @Override @SuppressWarnings("unchecked") public <D> Loader<D> restartLoader(int id, Bundle args, LoaderCallbacks<D> cb) {
        Info old = mInfos.get(id);
        if (old != null) { mInfos.remove(id); old.destroy(); }
        return (Loader<D>) create(id, args, cb).loader;
    }
    @Override public void destroyLoader(int id) { Info i = mInfos.get(id); if (i != null) { mInfos.remove(id); i.destroy(); } }
    @Override @SuppressWarnings("unchecked") public <D> Loader<D> getLoader(int id) { Info i = mInfos.get(id); return i == null ? null : (Loader<D>) (Loader<?>) i.loader; }
    void doStart() { if (mStarted) return; mStarted = true; for (int k = 0; k < mInfos.size(); k++) { Info i = mInfos.valueAt(k); i.loader.startLoading(); } }
    void doStop() { if (!mStarted) return; mStarted = false; for (int k = 0; k < mInfos.size(); k++) mInfos.valueAt(k).loader.stopLoading(); }
    void doDestroy() { for (int k = mInfos.size() - 1; k >= 0; k--) mInfos.valueAt(k).destroy(); mInfos.clear(); }
}
