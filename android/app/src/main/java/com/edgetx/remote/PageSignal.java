package com.edgetx.remote;

import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.Locale;

/**
 * Tab Sinyal: meneruskan analisis sinyal 2,4 GHz dari remot (NRF24 dipakai sebagai pemindai).
 * Mengaktifkan dengan mengirim kunci "ana" dan dijaga tetap aktif selama tab ini terlihat.
 * Saat aktif, remot memutus sementara pengiriman ke RX (lihat catatan di kartu).
 */
class PageSignal extends Page {
    private final Handler h = new Handler(Looper.getMainLooper());
    private Graphs.SpectrumView spec;
    private Graphs.WifiBarsView bars;
    private TextView tvTxStat, tvClean, tvBestWorst, tvState;
    private Button btnToggle;
    private boolean wantOn = true;
    private boolean visible = false;
    private int[] lastSp;

    PageSignal(Host h) { super(h); }

    @Override View build() {
        LinearLayout col = scrollColumn();

        LinearLayout card = Ui.card(c, col, "Analisis sinyal 2,4 GHz");

        LinearLayout stateRow = Ui.row(c);
        tvState = Ui.text(c, "Nonaktif", 12, Ui.MUT, true);
        stateRow.addView(tvState, new LinearLayout.LayoutParams(0, -2, 1f));
        btnToggle = Ui.button(c, "Aktifkan", Ui.ACC, Ui.onAccent());
        btnToggle.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { setWanted(!wantOn); }
        });
        stateRow.addView(btnToggle, Ui.lpm(c, Ui.dp(c, 110), Ui.dp(c, 38), 8, 0, 0, 0));
        card.addView(stateRow, Ui.lpm(c, -1, -2, 0, 0, 0, 10));

        spec = new Graphs.SpectrumView(c);
        card.addView(spec, Ui.lpm(c, -1, -2, 0, 0, 0, 8));

        LinearLayout chipRow = Ui.row(c);
        tvTxStat = chip(chipRow, "Kanal TX: --");
        tvClean = chip(chipRow, "Paling bersih: --");
        card.addView(chipRow, Ui.lpm(c, -1, -2, 0, 8, 0, 10));

        TextView lbl = Ui.text(c, "KEPADATAN 13 KANAL WIFI", 11, Ui.ACC, true);
        lbl.setLetterSpacing(0.1f);
        card.addView(lbl, Ui.lpm(c, -1, -2, 0, 0, 0, 6));
        bars = new Graphs.WifiBarsView(c);
        card.addView(bars, Ui.lpm(c, -1, -2, 0, 0, 0, 6));
        tvBestWorst = Ui.text(c, "", 12, Ui.MUT, false);
        card.addView(tvBestWorst, Ui.lpm(c, -1, -2, 0, 4, 0, 0));

        k.note(card, "Selama analisis aktif, NRF24 di remot dipakai penuh sebagai pemindai 126 kanal dan pengiriman ke RX diputus sementara (RX masuk failsafe). Jangan aktifkan saat model sedang terbang atau berjalan. Mati otomatis kalau tab ini ditutup.");

        return view;
    }

    private TextView chip(LinearLayout row, String txt) {
        TextView t = Ui.text(c, txt, 12, Ui.TX, true);
        t.setBackground(Ui.round(c, Ui.CARD2, 10, Ui.STROKE));
        t.setPadding(Ui.dp(c, 10), Ui.dp(c, 6), Ui.dp(c, 10), Ui.dp(c, 6));
        row.addView(t, Ui.lpm(c, -2, -2, 0, 0, 8, 0));
        return t;
    }

    private void setWanted(boolean on) {
        wantOn = on;
        btnToggle.setText(on ? "Matikan" : "Aktifkan");
        Ui.paint(c, btnToggle, on ? Ui.BAD : Ui.ACC, on ? 0xFFFFFFFF : Ui.onAccent());
        tvState.setText(on ? "Mengaktifkan..." : "Nonaktif");
        if (on && visible && host.ready()) host.set("ana", 1);
        if (!on && host.ready()) host.set("ana", 0);
        if (!on) { spec.set(null); bars.set(null); tvTxStat.setText("Kanal TX: --"); tvClean.setText("Paling bersih: --"); tvBestWorst.setText(""); }
    }

    private final Runnable keepAlive = new Runnable() {
        @Override public void run() {
            if (visible && wantOn && host.ready()) host.set("ana", 1);
            h.postDelayed(this, 900);     // dikirim ulang supaya remot tidak menganggap halaman ditutup (batas 2 detik di remot)
        }
    };

    @Override void onShow() {
        visible = true;
        if (wantOn && host.ready()) host.set("ana", 1);
        h.removeCallbacks(keepAlive);
        h.postDelayed(keepAlive, 900);
    }

    @Override void onHide() {
        visible = false;
        h.removeCallbacks(keepAlive);
        if (host.ready()) host.set("ana", 0);
        tvState.setText("Nonaktif");
    }

    @Override void onLink(boolean ready) {
        if (!ready) { spec.set(null); bars.set(null); tvState.setText("Terputus"); }
        else if (visible && wantOn) host.set("ana", 1);
    }

    @Override void onData(JSONObject d) {
        super.onData(d);
        JSONArray sp = d.optJSONArray("sp");
        if (sp == null) {
            if (visible) tvState.setText(wantOn ? "Menunggu data..." : "Nonaktif");
            return;
        }
        int[] v = new int[Math.min(126, sp.length())];
        for (int i = 0; i < v.length; i++) v[i] = sp.optInt(i, 0);
        lastSp = v;
        spec.set(v);
        bars.set(v);
        tvState.setText("Aktif");

        int sum = 0, cnt = 0;
        for (int i = 106; i <= 114 && i < v.length; i++) { sum += v[i]; cnt++; }
        int txLvl = cnt > 0 ? sum / cnt : 0;
        String txWord = txLvl < 8 ? "bersih" : (txLvl < 30 ? "agak ramai" : "RAMAI");
        tvTxStat.setText("Kanal TX (110): " + txWord);
        tvTxStat.setTextColor(txLvl < 8 ? Ui.OK : (txLvl < 30 ? Ui.WARN : Ui.BAD));

        int bestCh = 3, bestLv = 999;
        for (int c2 = 3; c2 < 123 && c2 < v.length; c2++) {
            int a = Math.max(0, c2 - 2), b = Math.min(v.length - 1, c2 + 2), s2 = 0, n2 = 0;
            for (int i = a; i <= b; i++) { s2 += v[i]; n2++; }
            int lv = n2 > 0 ? s2 / n2 : 0;
            if (lv < bestLv) { bestLv = lv; bestCh = c2; }
        }
        tvClean.setText("Paling bersih: " + bestCh);
        tvClean.setTextColor(Ui.OK);

        tvBestWorst.setText(String.format(Locale.US, "Kanal WiFi terbaik %d, paling ramai %d", bars.bestChannel(), bars.worstChannel()));
    }
}
