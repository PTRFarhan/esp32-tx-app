package com.edgetx.remote;

import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.json.JSONObject;

import java.io.BufferedInputStream;
import java.io.InputStream;
import java.util.Locale;

/**
 * Update firmware langsung lewat Bluetooth, tanpa pindah ke WiFi.
 * Alur: kirim "FWB,ukuran", tunggu "A,FWB,200", kirim berkas mentah dengan jendela 8 KB,
 * remot mengabari "F,byte" tiap 4 KB, lalu "F,DONE" dan restart sendiri.
 */
class PageUpdate extends Page {
    static final long MIN_SIZE = 102400;
    static final long MAX_SIZE = 1966080;       // satu slot aplikasi di min_spiffs.csv

    private static final int IDLE = 0, WAIT_ACK = 1, SENDING = 2, FINISHING = 3, DONE = 4, FAIL = 5;

    private TextView tvVer, tvFile, tvStatus, tvPct;
    private Ui.BarView bar;
    private Button btnPick, btnStart, btnCancel;
    private Uri fileUri;
    private long fileSize;
    private boolean fileOk;
    private int phase = IDLE;
    private long acked;
    private long lastProgressAt;
    private final Handler h = new Handler(Looper.getMainLooper());

    PageUpdate(Host h) { super(h); }

    boolean busy() { return phase == WAIT_ACK || phase == SENDING || phase == FINISHING; }

    @Override View build() {
        LinearLayout col = scrollColumn();

        LinearLayout v = Ui.card(c, col, "Firmware remot");
        tvVer = Ui.text(c, "Versi terpasang: (hubungkan Bluetooth)", 15, Ui.TX, true);
        v.addView(tvVer);
        k.note(v, "File yang dikirim harus firmware.bin hasil build PlatformIO, yaitu .pio/build/esp32dev/firmware.bin. "
                + "Bukan kode C++ (.ino atau .cpp) dan bukan .hex.");

        LinearLayout f = Ui.card(c, col, "1. Pilih file");
        btnPick = k.action(f, "Pilih firmware.bin", Ui.CARD2, Ui.TX, new View.OnClickListener() {
            @Override public void onClick(View x) { ((MainActivity) c).pickFirmwareFile(); }
        });
        tvFile = Ui.text(c, "Belum ada file.", 13, Ui.MUT, false);
        f.addView(tvFile);

        LinearLayout g = Ui.card(c, col, "2. Kirim lewat Bluetooth");
        btnStart = k.action(g, "Mulai update", Ui.ACC, 0xFF001018, new View.OnClickListener() {
            @Override public void onClick(View x) { start(); }
        });
        btnCancel = k.action(g, "Batalkan", Ui.CARD2, Ui.BAD, new View.OnClickListener() {
            @Override public void onClick(View x) { cancel(); }
        });
        bar = new Ui.BarView(c, false);
        g.addView(bar, Ui.lpm(c, -1, Ui.dp(c, 12), 0, 10, 0, 4));
        tvPct = Ui.text(c, "", 22, Ui.TX, true);
        g.addView(tvPct);
        tvStatus = Ui.text(c, "Siap.", 13, Ui.MUT, false);
        g.addView(tvStatus, Ui.lpm(c, -1, -2, 0, 2, 0, 0));

        LinearLayout i = Ui.card(c, col, "Cara kerja");
        TextView t = Ui.text(c,
                "• Berkas dikirim langsung lewat Bluetooth yang sedang tersambung, tanpa pindah ke WiFi. Sekitar 1 menit untuk 1,3 MB.\n"
                        + "• OLED remot menampilkan kemajuan update. Jangan matikan remot dan biarkan aplikasi tetap terbuka.\n"
                        + "• Setelah selesai remot restart sendiri. Hubungkan lagi, versi baru tampil di atas.\n"
                        + "• Kalau gagal, remot kembali normal sendiri dalam beberapa detik dan firmware lama tetap utuh.\n"
                        + "• Alternatif tanpa aplikasi: menu remot UPDATE FIRMWARE, sambung WiFi ESP32 TX (password Farhan2005), buka 192.168.4.1.\n"
                        + "• Pemasangan pertama firmware yang membawa fitur ini harus lewat kabel USB (partisi Minimal SPIFFS).",
                12, Ui.MUT, false);
        t.setLineSpacing(0f, 1.2f);
        i.addView(t);
        refreshButtons();
        return view;
    }

    void onFilePicked(Uri uri) {
        long[] info = inspect(uri);
        fileUri = uri;
        fileOk = false;
        if (info == null || info[0] == 0) {
            tvFile.setText("Berkas tidak bisa dibaca.");
            tvFile.setTextColor(Ui.BAD);
        } else if (info[1] != 0xE9) {
            tvFile.setText("Bukan file firmware ESP32 (harus firmware.bin dari PlatformIO).");
            tvFile.setTextColor(Ui.BAD);
        } else if (info[0] > MAX_SIZE) {
            tvFile.setText(String.format(Locale.US, "Terlalu besar: %d KB (maks %d KB).", info[0] / 1024, MAX_SIZE / 1024));
            tvFile.setTextColor(Ui.BAD);
        } else if (info[0] < MIN_SIZE) {
            tvFile.setText(String.format(Locale.US, "Terlalu kecil: %d KB. Bukan firmware lengkap.", info[0] / 1024));
            tvFile.setTextColor(Ui.BAD);
        } else {
            fileSize = info[0];
            fileOk = true;
            tvFile.setText(String.format(Locale.US, "Siap: %d KB, header firmware valid.", fileSize / 1024));
            tvFile.setTextColor(Ui.OK);
        }
        refreshButtons();
    }

    /** [0] = ukuran, [1] = byte pertama. */
    private long[] inspect(Uri uri) {
        try (InputStream in = c.getContentResolver().openInputStream(uri)) {
            if (in == null) return null;
            int first = in.read();
            long size = 0;
            if (first >= 0) {
                size = 1;
                byte[] buf = new byte[16384];
                int n;
                while ((n = in.read(buf)) > 0) size += n;
            }
            return new long[]{size, first};
        } catch (Exception e) {
            return null;
        }
    }

    private void refreshButtons() {
        boolean run = busy();
        btnStart.setEnabled(fileOk && !run && host.ready());
        btnPick.setEnabled(!run);
        btnCancel.setEnabled(run);
        btnStart.setText(host.ready() ? "Mulai update" : "Mulai update (hubungkan Bluetooth dulu)");
    }

    // ---------------------------------------------------------------- alur
    private void start() {
        if (!fileOk || !host.ready() || busy()) return;
        phase = WAIT_ACK;
        acked = 0;
        lastProgressAt = SystemClock.uptimeMillis();
        show(0, "Menyiapkan remot...", Ui.TX);
        ((MainActivity) c).prepareBulk();          // hentikan paket kendali dan polling
        host.cmd("FWB," + fileSize);
        h.postDelayed(watchdog, 1000);
    }

    @Override void onAck(String name, int code) {
        if (name.equals("FWB") && phase == WAIT_ACK) {
            if (code != 200) {
                fail(code == 409 ? "Remot sedang dalam mode update." : "Remot menolak berkas (kode " + code + ").");
                return;
            }
            try {
                InputStream in = new BufferedInputStream(c.getContentResolver().openInputStream(fileUri), 32768);
                phase = SENDING;
                lastProgressAt = SystemClock.uptimeMillis();
                show(0, "Mengirim firmware...", Ui.TX);
                ((MainActivity) c).link().startBulk(in, fileSize, 8192);
            } catch (Exception e) {
                fail("Berkas tidak bisa dibuka.");
            }
        } else if (name.equals("F") && busy()) {
            // kode kegagalan dari remot
            String why = code == 415 ? "Bukan file firmware."
                    : code == 422 ? "Remot menolak: berkas tidak valid atau rusak."
                    : code == 408 ? "Remot berhenti menerima data (terlalu lama tanpa data)."
                    : "Remot gagal menulis ke flash (kode " + code + ").";
            ((MainActivity) c).link().stopBulk();
            fail(why);
        }
    }

    /** Baris "F,..." dari remot: kemajuan atau selesai. */
    void onFw(String arg) {
        if (!busy()) return;
        lastProgressAt = SystemClock.uptimeMillis();
        if (arg.startsWith("DONE")) {
            ((MainActivity) c).link().stopBulk();
            phase = DONE;
            show(100, "Berhasil. Remot restart sendiri, aplikasi menyambung ulang otomatis sekitar 10 detik lagi.", Ui.OK);
            ((MainActivity) c).endBulk();
            ((MainActivity) c).reconnectAfter(10000);
            refreshButtons();
            return;
        }
        try {
            acked = Long.parseLong(arg.trim());
        } catch (NumberFormatException e) {
            return;
        }
        ((MainActivity) c).link().bulkAck(acked);
        int pct = (int) Math.min(99, acked * 100 / fileSize);
        show(pct, phase == FINISHING ? "Remot memasang firmware..." : "Mengirim firmware " + pct + "%", Ui.TX);
    }

    private final Runnable watchdog = new Runnable() {
        @Override public void run() {
            if (!busy()) return;
            long idle = SystemClock.uptimeMillis() - lastProgressAt;
            BleLink l = ((MainActivity) c).link();
            if (phase == SENDING && l.bulkAllSent()) {
                phase = FINISHING;
                show(99, "Remot memasang firmware...", Ui.TX);
            }
            if (phase == WAIT_ACK && idle > 4000) {
                fail("Remot tidak menjawab. Pastikan firmware sudah versi terbaru (flash USB sekali).");
                return;
            }
            if ((phase == SENDING || phase == FINISHING) && idle > (phase == FINISHING ? 20000 : 10000)) {
                l.stopBulk();
                fail("Tidak ada balasan dari remot. Remot akan kembali normal sendiri.");
                return;
            }
            h.postDelayed(this, 500);
        }
    };

    private void cancel() {
        if (!busy()) return;
        ((MainActivity) c).link().stopBulk();
        fail("Dibatalkan. Remot kembali normal sendiri dalam beberapa detik.");
    }

    private void fail(String msg) {
        phase = FAIL;
        h.removeCallbacks(watchdog);
        show(0, msg, Ui.BAD);
        ((MainActivity) c).endBulk();
        refreshButtons();
    }

    private void show(int pct, String msg, int color) {
        bar.set(pct / 100f, color == Ui.BAD ? Ui.BAD : (color == Ui.OK ? Ui.OK : Ui.ACC));
        tvPct.setText(phase == SENDING || phase == FINISHING || phase == DONE ? pct + "%" : "");
        tvStatus.setText(msg);
        tvStatus.setTextColor(color);
        refreshButtons();
    }

    @Override void onData(JSONObject d) {
        super.onData(d);
        tvVer.setText("Versi terpasang: " + d.optString("fw", "?"));
    }

    /** Bluetooth putus. Kalau semua byte sudah terkirim, kemungkinan besar remot sedang restart. */
    @Override void onLink(boolean ready) {
        if (!ready && busy()) {
            BleLink l = ((MainActivity) c).link();
            boolean sentAll = phase == FINISHING || (l != null && l.bulkAllSent() && phase == SENDING);
            h.removeCallbacks(watchdog);
            if (sentAll) {
                phase = DONE;
                show(100, "Bluetooth putus setelah semua data terkirim. Kemungkinan remot sedang restart. Aplikasi menyambung ulang otomatis, cek versinya.", Ui.OK);
                ((MainActivity) c).reconnectAfter(10000);
            } else {
                fail("Bluetooth putus di tengah jalan. Firmware lama tetap utuh, coba lagi.");
            }
            ((MainActivity) c).endBulk();
        }
        refreshButtons();
    }

    @Override void onShow() { refreshButtons(); }
}
