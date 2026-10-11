package android.app;

import android.os.Bundle;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ListAdapter;
import android.widget.ListView;

/** An activity that is a list: its own ListView (android:id/list) unless its layout has one. */
public class ListActivity extends Activity {
    protected ListAdapter mAdapter;
    protected ListView mList;
    private boolean mFinishedStart;

    public ListActivity() {}
    @Override public void onContentChanged() {
        super.onContentChanged();
        View v = findViewById(android.R.id.list);
        if (!(v instanceof ListView)) throw new RuntimeException("Your content must have a ListView whose id attribute is 'android.R.id.list'");
        mList = (ListView) v;
        View empty = findViewById(android.R.id.empty);
        if (empty != null) mList.setEmptyView(empty);
        mList.setOnItemClickListener((AdapterView.OnItemClickListener) (parent, view, position, id) -> onListItemClick((ListView) parent, view, position, id));
        if (mFinishedStart) setListAdapter(mAdapter);
        mFinishedStart = true;
    }
    private void ensureList() {
        if (mList != null) return;
        ListView l = new ListView(this);
        l.setId(android.R.id.list);
        setContentView(l);
    }
    public void setListAdapter(ListAdapter adapter) { synchronized (this) { ensureList(); mAdapter = adapter; mList.setAdapter(adapter); } }
    public ListAdapter getListAdapter() { return mAdapter; }
    public ListView getListView() { ensureList(); return mList; }
    public void setSelection(int position) { getListView().setSelection(position); }
    public int getSelectedItemPosition() { return getListView().getSelectedItemPosition(); }
    public long getSelectedItemId() { return getListView().getSelectedItemId(); }
    protected void onListItemClick(ListView l, View v, int position, long id) {}
    @Override protected void onRestoreInstanceState(Bundle state) { ensureList(); super.onRestoreInstanceState(state); }
}
