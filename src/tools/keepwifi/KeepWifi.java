import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.os.Looper;

/**
 * Holds a standing request for a Wi-Fi network so Android never tears Wi-Fi down.
 * Once the phone is paired, Wear's Bluetooth companion proxy outscores Wi-Fi
 * (100 vs 60), Wi-Fi satisfies no request, and ConnectivityService disconnects it
 * after a ~20 s linger. The watch's telnet shell lives on Wi-Fi, so keep it up.
 *
 * Run as root: CLASSPATH=/oem/r800dev/keepwifi.dex app_process / KeepWifi
 */
public class KeepWifi {
    public static void main(String[] args) throws Exception {
        Looper.prepareMainLooper();
        Object at = Class.forName("android.app.ActivityThread").getMethod("systemMain").invoke(null);
        Context ctx = (Context) at.getClass().getMethod("getSystemContext").invoke(at);
        ConnectivityManager cm = (ConnectivityManager) ctx.getSystemService(Context.CONNECTIVITY_SERVICE);
        NetworkRequest req = new NetworkRequest.Builder()
                .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
                .addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
                .build();
        cm.requestNetwork(req, new ConnectivityManager.NetworkCallback() {
            @Override public void onAvailable(Network n) { System.out.println("keepwifi: wifi available " + n); }
            @Override public void onLost(Network n) { System.out.println("keepwifi: wifi lost " + n); }
        });
        System.out.println("keepwifi: request filed");
        Looper.loop();
    }
}
