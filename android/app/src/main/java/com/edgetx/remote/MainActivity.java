package com.edgetx.remote;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.res.Configuration;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.Gravity;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import org.json.JSONObject;

import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

/**
 * Aplikasi ESP32 TX: kendali, pantauan, pengaturan model dan remot, dan update firmware.
 * Semua lewat Bluetooth (Nordic UART). Hanya update firmware yang memakai WiFi sementara.
 */
public class MainActivity extends Activity implements BleLink.Listener, Host {
    private static final int REQ_BT = 11, REQ_FW = 21, REQ_RESTORE = 22, REQ_SAVE = 23;
    private static final int SEND_MS = 25;
    private static final int POLL_MS = 700;
    private static final String[] TAB_NAMES = {"Kendali", "Monitor", "Model", "Sinyal", "Remot", "Update"};
    private static final int TAB_SIGNAL = 3;

    private final Handler main = new Handler(Looper.getMainLooper());
    private BleLink link;
    private JSONObject data;
    private int linkState = BleLink.IDLE;
    private Frame.Status status;
    private long statusAt;
    private long dataAt;

    private final Page[] pages = new Page[6];
    private PageControl pControl;
    private PageSettings pSettings;
    private PageUpdate pUpdate;
    private PageSignal pSignal;
    private int tab = 0;

    private FrameLayout content;
    private LinearLayout header, tabBar;
    private View dot;
    private TextView tvTitle, tvSub, tvBatt;
    private Ui.SignalView sig;
    private Button btnConnect;
    private final TextView[] tabViews = new TextView[6];
    private String pendingBackup;

    // ---------------------------------------------------------------- Host
    @Override public android.content.Context ctx() { return this; }
    @Override public boolean ready() { return link != null && link.isReady(); }
    @Override public JSONObject data() { return data; }
    @Override public void toast(String m) { Toast.makeText(this, m, Toast.LENGTH_SHORT).show(); }
    @Override public void cmd(String line) { if (ready()) link.sendLine(line); }
    @Override public void recreateUi() { recreate(); }

    @Override public void set(String key, int value) {
        if (!ready()) { toast("Belum tersambung ke remot"); return; }
        link.sendLine("S," + key + "," + value);
        main.removeCallbacks(pollNow);
        main.postDelayed(pollNow, 250);
    }

    private final Runnable pollNow = new Runnable() {
        @Override public void run() { if (ready() && tab != 0 && !bulkMode) link.sendLine("G"); }
    };

    BleLink link() { return link; }

    /** Perilaku throttle stik HP saat dilepas: 0 bertahan, 1 kembali ke tengah, 2 kembali ke bawah. Disimpan di HP. */
    int throttleMode() {
        try { return getSharedPreferences("edgetx", MODE_PRIVATE).getInt("thr", 0); } catch (Exception e) { return 0; }
    }

    void setThrottleMode(int m) {
        try { getSharedPreferences("edgetx", MODE_PRIVATE).edit().putInt("thr", m).apply(); } catch (Exception ignored) { }
        pControl.applyThrottleMode(m);
    }

    private boolean bulkMode;

    /** Update firmware dimulai: tidak ada paket kendali atau polling selama transfer. */
    void prepareBulk() { bulkMode = true; pControl.resetAll(); }
    void endBulk() { bulkMode = false; }

    /** Sambungkan Bluetooth lagi setelah jeda (setelah remot restart karena update). */
    void reconnectAfter(long ms) {
        main.postDelayed(new Runnable() {
            @Override public void run() { if (linkState == BleLink.IDLE) onConnectClicked(); }
        }, ms);
    }

    // ---------------------------------------------------------------- siklus hidup
    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        Ui.loadTheme(this);
        link = new BleLink(this, this);
        setContentView(buildUi());
        applyConfig();
        showTab(0);
        renderLink();
    }

    private View buildUi() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Ui.BG);

        // ---- header
        header = Ui.row(this);
        header.setBackground(Ui.round(this, Ui.CARD, 0, 0));
        header.setPadding(Ui.dp(this, 14), 0, Ui.dp(this, 10), 0);
        dot = Ui.dot(this, Ui.MUT);
        header.addView(dot, Ui.lpm(this, Ui.dp(this, 10), Ui.dp(this, 10), 0, 0, 10, 0));
        LinearLayout names = new LinearLayout(this);
        names.setOrientation(LinearLayout.VERTICAL);
        tvTitle = Ui.text(this, "ESP32 TX", 16, Ui.TX, true);
        tvSub = Ui.text(this, "Belum tersambung", 11, Ui.MUT, false);
        tvSub.setSingleLine(true);
        tvSub.setEllipsize(android.text.TextUtils.TruncateAt.END);
        names.addView(tvTitle);
        names.addView(tvSub);
        header.addView(names, new LinearLayout.LayoutParams(0, -2, 1f));
        tvBatt = Ui.text(this, "", 13, Ui.TX, true);
        header.addView(tvBatt, Ui.lpm(this, -2, -2, 6, 0, 8, 0));
        sig = new Ui.SignalView(this);
        header.addView(sig, Ui.lpm(this, Ui.dp(this, 26), Ui.dp(this, 18), 0, 0, 10, 0));
        btnConnect = Ui.button(this, "Hubungkan", Ui.ACC, 0xFF001018);
        btnConnect.setMinHeight(0);
        btnConnect.setMinimumHeight(0);
        btnConnect.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { onConnectClicked(); }
        });
        header.addView(btnConnect, Ui.lp(-2, Ui.dp(this, 36)));
        root.addView(header, Ui.lp(-1, Ui.dp(this, 54)));

        // ---- isi
        content = new FrameLayout(this);
        pControl = new PageControl(this);
        pSettings = new PageSettings(this);
        pUpdate = new PageUpdate(this);
        pSignal = new PageSignal(this);
        pages[0] = pControl;
        pages[1] = new PageMonitor(this);
        pages[2] = new PageModel(this);
        pages[3] = pSignal;
        pages[4] = pSettings;
        pages[5] = pUpdate;
        for (Page p : pages) {
            View v = p.build();
            v.setVisibility(View.GONE);
            content.addView(v, new FrameLayout.LayoutParams(-1, -1));
        }
        pControl.applyThrottleMode(throttleMode());
        root.addView(content, new LinearLayout.LayoutParams(-1, 0, 1f));

        // ---- tab bawah
        tabBar = Ui.row(this);
        tabBar.setBackground(Ui.round(this, Ui.CARD, 0, 0));
        for (int i = 0; i < TAB_NAMES.length; i++) {
            final int idx = i;
            TextView t = Ui.text(this, TAB_NAMES[i], 12, Ui.MUT, true);
            t.setGravity(Gravity.CENTER);
            t.setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View v) { showTab(idx); }
            });
            tabViews[i] = t;
            tabBar.addView(t, new LinearLayout.LayoutParams(0, -1, 1f));
        }
        root.addView(tabBar, Ui.lp(-1, Ui.dp(this, 52)));
        return root;
    }

    private boolean landscape() {
        return getResources().getConfiguration().orientation == Configuration.ORIENTATION_LANDSCAPE;
    }

    /** Header dan tab diperkecil di landscape supaya area kendali lebih luas. */
    private void applyConfig() {
        boolean land = landscape();
        header.getLayoutParams().height = Ui.dp(this, land ? 42 : 54);
        tabBar.getLayoutParams().height = Ui.dp(this, land ? 40 : 52);
        header.requestLayout();
        tabBar.requestLayout();
        for (Page p : pages) p.onConfig(land);
        WindowInsetsController c = getWindow().getInsetsController();
        if (c != null) {
            if (land) {
                c.hide(WindowInsets.Type.statusBars());
                c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            } else {
                c.show(WindowInsets.Type.statusBars());
            }
        }
    }

    @Override
    public void onConfigurationChanged(Configuration cfg) {
        super.onConfigurationChanged(cfg);
        applyConfig();
    }

    private void showTab(int i) {
        if (i != tab) {
            pages[tab].onHide();
            pages[tab].view.setVisibility(View.GONE);
        }
        tab = i;
        pages[i].view.setVisibility(View.VISIBLE);
        for (int k = 0; k < tabViews.length; k++) {
            boolean on = k == i;
            tabViews[k].setTextColor(on ? Ui.onAccent() : Ui.MUT);
            tabViews[k].setBackground(on ? Ui.round(this, Ui.ACC, 10, 0) : null);
        }
        pages[i].onShow();
        if (data != null) pages[i].onData(data);
        if (ready() && i != 0) link.sendLine("G");
    }

    // ---------------------------------------------------------------- koneksi
    private void onConnectClicked() {
        if (linkState != BleLink.IDLE) {
            link.stop();
            return;
        }
        if (checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN) != PackageManager.PERMISSION_GRANTED
                || checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{
                    Manifest.permission.BLUETOOTH_SCAN,
                    Manifest.permission.BLUETOOTH_CONNECT}, REQ_BT);
            return;
        }
        link.start();
    }

    @Override
    public void onRequestPermissionsResult(int code, String[] perms, int[] results) {
        super.onRequestPermissionsResult(code, perms, results);
        if (code != REQ_BT) return;
        boolean ok = results.length > 0;
        for (int r : results) if (r != PackageManager.PERMISSION_GRANTED) ok = false;
        if (ok) link.start(); else tvSub.setText("Izin Bluetooth ditolak");
    }

    @Override
    public void onLinkState(int state, String message) {
        linkState = state;
        tvSub.setText(message);
        if (state == BleLink.IDLE) {
            status = null;
            data = null;
            pControl.resetAll();
            tvBatt.setText("");
            sig.set(0);
        } else if (state == BleLink.READY) {
            link.sendLine("G");
        }
        for (Page p : pages) p.onLink(state == BleLink.READY);
        renderLink();
    }

    @Override public void onMtu(int mtu) { }

    private void renderLink() {
        dot.setBackground(Ui.round(this, linkState == BleLink.READY ? Ui.OK
                : (linkState == BleLink.IDLE ? Ui.MUT : Ui.WARN), 8, 0));
        switch (linkState) {
            case BleLink.READY:
                btnConnect.setText("Putuskan");
                Ui.paint(this, btnConnect, Ui.BAD, 0xFFFFFFFF);
                break;
            case BleLink.SCANNING:
            case BleLink.CONNECTING:
                btnConnect.setText("Batal");
                Ui.paint(this, btnConnect, Ui.WARN, 0xFF000000);
                break;
            default:
                btnConnect.setText("Hubungkan");
                Ui.paint(this, btnConnect, Ui.ACC, 0xFF001018);
                break;
        }
    }

    // ---------------------------------------------------------------- data dari remot
    @Override
    public void onStatusLine(String line) {
        if (line.startsWith("J")) {
            try {
                data = new JSONObject(line.substring(1));
                dataAt = SystemClock.uptimeMillis();
                pages[tab].onData(data);
                headerFromData();
            } catch (Exception ignored) { }
        } else if (line.startsWith("A,")) {
            String[] p = line.split(",");
            if (p.length >= 3) {
                int code = 0;
                try { code = Integer.parseInt(p[2].trim()); } catch (NumberFormatException ignored) { }
                String name = p[1];
                for (Page pg : pages) pg.onAck(name, code);
                if (name.startsWith("S:") && code != 200) {
                    toast(code == 409 ? ackReason(name.substring(2)) : "Ditolak oleh remot");
                    if (ready()) link.sendLine("G");   // kembalikan tampilan ke nilai sebenarnya
                }
            }
        } else if (line.startsWith("F,")) {
            pUpdate.onFw(line.substring(2));
        } else if (line.startsWith("BK:")) {
            for (Page pg : pages) pg.onBackup(line.substring(3));
        } else {
            Frame.Status s = Frame.parseStatus(line);
            if (s != null) {
                status = s;
                statusAt = SystemClock.uptimeMillis();
                if (data == null) tvBatt.setText(String.format(Locale.US, "%.2fV", s.volt));
                sig.set(s.link);
            }
        }
    }

    private String ackReason(String key) {
        if (key.equals("src")) return "Mode HP butuh Bluetooth tersambung";
        if (key.equals("wifi")) return "Matikan Bluetooth dulu";
        if (key.equals("bt")) return "Matikan WiFi dulu";
        return "Ditolak oleh remot";
    }

    private void headerFromData() {
        if (data == null) return;
        tvBatt.setText(String.format(Locale.US, "%.2fV", data.optDouble("v", 0)));
        tvBatt.setTextColor(data.optInt("lb") == 1 ? Ui.BAD : Ui.TX);
        sig.set(data.optInt("link", 0));
        tvTitle.setText("ESP32 TX  •  " + PageModel.nameOf(data, data.optInt("mdl", 0)));
    }

    // ---------------------------------------------------------------- loop
    private final Runnable sendTick = new Runnable() {
        @Override public void run() {
            pControl.sample();
            if (link.isReady() && !bulkMode) link.sendFrame(Frame.build(pControl.ch));
            main.postDelayed(this, SEND_MS);
        }
    };

    private final Runnable uiTick = new Runnable() {
        @Override public void run() {
            if (tab == 0) {
                pControl.refreshReadout();
                pControl.setHint(controlHint());
                // di tab Kendali input dibaca dari status singkat
                if (status != null && SystemClock.uptimeMillis() - statusAt < 2500) syncSrc();
            }
            main.postDelayed(this, 150);
        }
    };

    private final Runnable pollTick = new Runnable() {
        @Override public void run() {
            if (bulkMode) { /* tidak mengganggu transfer firmware */ }
            else if (ready() && tab != 0) link.sendLine("G");
            else if (ready() && tab == 0 && SystemClock.uptimeMillis() - dataAt > 3000) link.sendLine("G");
            main.postDelayed(this, tab == 0 ? 3000 : POLL_MS);
        }
    };

    /** Menyelaraskan tombol Input dengan status singkat di tab Kendali. */
    private void syncSrc() {
        try {
            JSONObject d = new JSONObject();
            d.put("src", status.hp);
            for (Controls.Ctl x : pControl.ctls) x.bind(d);
        } catch (Exception ignored) { }
    }

    private String controlHint() {
        if (linkState != BleLink.READY) return "Tekan Hubungkan. Bluetooth remot harus ON.";
        if (status == null || SystemClock.uptimeMillis() - statusAt > 2500) return "Menunggu status dari remot...";
        String a = String.format(Locale.US, "Baterai %.2f V   Link NRF %d/5\n", status.volt, status.link);
        if (status.hp != 1) return a + "Remot memakai stik sendiri. Pilih Input: HP untuk mengendalikan dari sini.";
        if (status.ok == 1) return a + "Mode HP aktif, data diterima.";
        return a + "Mode HP, remot FAILSAFE (data belum diterima).";
    }

    @Override protected void onResume() {
        super.onResume();
        main.post(sendTick);
        main.post(uiTick);
        main.post(pollTick);
        if (tab == TAB_SIGNAL) pages[tab].onShow();
    }

    @Override protected void onPause() {
        super.onPause();
        main.removeCallbacks(sendTick);
        main.removeCallbacks(uiTick);
        main.removeCallbacks(pollTick);
        pControl.resetAll();     // aplikasi ditinggal: throttle kembali 0, remot masuk failsafe sendiri
        if (tab == TAB_SIGNAL) pages[tab].onHide();     // jangan biarkan analisis sinyal tetap aktif di latar belakang
    }

    @Override protected void onDestroy() {
        link.stop();
        super.onDestroy();
    }

    // ---------------------------------------------------------------- berkas
    void pickFirmwareFile() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        startActivityForResult(i, REQ_FW);
    }

    void pickRestoreFile() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        startActivityForResult(i, REQ_RESTORE);
    }

    void saveBackup(String text) {
        pendingBackup = "EDGETX1:" + text.substring(8);
        Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("text/plain");
        i.putExtra(Intent.EXTRA_TITLE, "edgetx-backup.txt");
        startActivityForResult(i, REQ_SAVE);
    }

    @Override
    protected void onActivityResult(int req, int res, Intent intent) {
        super.onActivityResult(req, res, intent);
        if (res != RESULT_OK || intent == null || intent.getData() == null) return;
        final Uri uri = intent.getData();
        if (req == REQ_FW) {
            pUpdate.onFilePicked(uri);
        } else if (req == REQ_SAVE && pendingBackup != null) {
            try (OutputStream out = getContentResolver().openOutputStream(uri)) {
                out.write(pendingBackup.getBytes(StandardCharsets.US_ASCII));
                toast("Cadangan tersimpan");
            } catch (Exception e) {
                toast("Gagal menyimpan: " + e.getMessage());
            }
            pendingBackup = null;
        } else if (req == REQ_RESTORE) {
            final String text;
            try (InputStream in = getContentResolver().openInputStream(uri)) {
                byte[] buf = new byte[2048];
                int n = in.read(buf);
                text = n > 0 ? new String(buf, 0, n, StandardCharsets.US_ASCII) : "";
            } catch (Exception e) {
                toast("Gagal membaca berkas");
                return;
            }
            if (!ready()) { toast("Hubungkan ke remot dulu"); return; }
            new android.app.AlertDialog.Builder(this, android.R.style.Theme_Material_Dialog_Alert)
                    .setMessage("Semua pengaturan dan model di remot diganti dengan isi file. Remot restart setelahnya. Lanjut?")
                    .setNegativeButton("Batal", null)
                    .setPositiveButton("Pulihkan", new android.content.DialogInterface.OnClickListener() {
                        @Override public void onClick(android.content.DialogInterface d, int w) { pSettings.startRestore(text); }
                    }).show();
        }
    }
}
