package husk;

import android.content.Intent;
import android.os.Binder;
import android.os.Bundle;
import android.os.IBinder;
import android.os.Parcel;
import android.os.RemoteException;

/**
 * Services the Play Store app answers on a phone, bound by apps through Play's client libraries. Each is a binder speaking that
 * library's AIDL, answered in this process.
 */
final class PlayStore {
    private PlayStore() {}

    /** The binder for a bind intent aimed at the Play Store, or null. */
    static IBinder bind(Intent i) {
        if (i == null || !GooglePackages.VENDING.equals(i.getPackage())) return null;
        if ("com.google.android.finsky.ageverification.BIND".equals(i.getAction())) return AGE_SIGNALS;
        return null;
    }

    /**
     * Play Age Signals (com.google.android.play:age-signals): checkAgeRange(package, options, callback). The answer is a result
     * without a user status -- what Play gives where no age law applies -- so games that ask go on as they would there.
     */
    private static final IBinder AGE_SIGNALS = new Binder() {
        private static final String SERVICE = "com.google.android.play.agesignals.protocol.IAgeSignalsService";
        private static final String CALLBACK = "com.google.android.play.agesignals.protocol.IAgeSignalsServiceCallback";
        @Override protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) throws RemoteException {
            if (code != 1) return super.onTransact(code, data, reply, flags);
            data.enforceInterface(SERVICE);
            data.readString();
            if (data.readInt() != 0) Bundle.CREATOR.createFromParcel(data);
            IBinder callback = data.readStrongBinder();
            if (callback == null) return true;
            android.util.Log.d("Husk", "Play Age Signals asked: answering that no age signal applies");
            Parcel p = Parcel.obtain();
            p.writeInterfaceToken(CALLBACK);
            p.writeInt(1);
            new Bundle().writeToParcel(p, 0);
            callback.transact(1, p, null, IBinder.FLAG_ONEWAY);
            return true;
        }
    };
}
