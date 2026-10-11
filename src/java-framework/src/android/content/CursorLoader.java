package android.content;

import android.database.Cursor;
import android.net.Uri;

/** Queries a ContentResolver in the background and re-queries when the cursor's content changes. */
public class CursorLoader extends AsyncTaskLoader<Cursor> {
    private final ForceLoadContentObserver mObserver = new ForceLoadContentObserver();
    private Uri mUri; private String[] mProjection, mSelectionArgs; private String mSelection, mSortOrder;
    private Cursor mCursor;
    public CursorLoader(Context c) { super(c); }
    public CursorLoader(Context c, Uri uri, String[] projection, String selection, String[] selectionArgs, String sortOrder) {
        super(c); mUri = uri; mProjection = projection; mSelection = selection; mSelectionArgs = selectionArgs; mSortOrder = sortOrder;
    }
    @Override public Cursor loadInBackground() {
        Cursor c = getContext().getContentResolver().query(mUri, mProjection, mSelection, mSelectionArgs, mSortOrder);
        if (c != null) { try { c.getCount(); c.registerContentObserver(mObserver); } catch (RuntimeException e) { c.close(); throw e; } }
        return c;
    }
    @Override public void deliverResult(Cursor c) {
        if (isReset()) { if (c != null) c.close(); return; }
        Cursor old = mCursor; mCursor = c;
        if (isStarted()) super.deliverResult(c);
        if (old != null && old != c && !old.isClosed()) old.close();
    }
    @Override protected void onStartLoading() { if (mCursor != null) deliverResult(mCursor); if (takeContentChanged() || mCursor == null) forceLoad(); }
    @Override protected void onStopLoading() { cancelLoad(); }
    @Override public void onCanceled(Cursor c) { if (c != null && !c.isClosed()) c.close(); }
    @Override protected void onReset() { super.onReset(); onStopLoading(); if (mCursor != null && !mCursor.isClosed()) mCursor.close(); mCursor = null; }
    public Uri getUri() { return mUri; } public void setUri(Uri u) { mUri = u; }
    public String[] getProjection() { return mProjection; } public void setProjection(String[] p) { mProjection = p; }
    public String getSelection() { return mSelection; } public void setSelection(String s) { mSelection = s; }
    public String[] getSelectionArgs() { return mSelectionArgs; } public void setSelectionArgs(String[] a) { mSelectionArgs = a; }
    public String getSortOrder() { return mSortOrder; } public void setSortOrder(String s) { mSortOrder = s; }
}
