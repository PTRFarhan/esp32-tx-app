package com.edgetx.remote;

import android.view.Gravity;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.json.JSONObject;

import java.util.Locale;

/** Pantauan langsung: baterai, link, channel, stik, timer, sistem. */
class PageMonitor extends Page {
    private Ui.GaugeView gauge;
    private Ui.SignalView signal;
    private TextView tvRx, tvLink, tvChips, tvMode;
    private final Ui.BarView[] chBar = new Ui.BarView[8];
    private final TextView[] chVal = new TextView[8];
    private final Ui.BarView[] rawBar = new Ui.BarView[4];
    private final TextView[] rawVal = new TextView[4];
    private TextView tvSw, tvTimer, tvSys, tvTimerInfo;

    PageMonitor(Host h) { super(h); }

    @Override View build() {
        LinearLayout col = scrollColumn();

        // ---- Baterai dan link
        LinearLayout hero = Ui.card(c, col, "Remot");
        LinearLayout hr = Ui.row(c);
        gauge = new Ui.GaugeView(c);
        hr.addView(gauge, Ui.lp(Ui.dp(c, 130), Ui.dp(c, 130)));
        LinearLayout side = new LinearLayout(c);
        side.setOrientation(LinearLayout.VERTICAL);
        side.setPadding(Ui.dp(c, 14), 0, 0, 0);
        tvMode = Ui.text(c, "--", 16, Ui.TX, true);
        side.addView(tvMode);
        LinearLayout sr = Ui.row(c);
        signal = new Ui.SignalView(c);
        sr.addView(signal, Ui.lp(Ui.dp(c, 34), Ui.dp(c, 24)));
        tvLink = Ui.text(c, "  Link NRF", 13, Ui.MUT, false);
        sr.addView(tvLink);
        side.addView(sr, Ui.lpm(c, -2, -2, 0, 8, 0, 4));
        tvRx = Ui.text(c, "Baterai RX: --", 13, Ui.TX, false);
        side.addView(tvRx);
        tvChips = Ui.text(c, "", 12, Ui.MUT, false);
        side.addView(tvChips, Ui.lpm(c, -2, -2, 0, 6, 0, 0));
        hr.addView(side, new LinearLayout.LayoutParams(0, -2, 1f));
        hero.addView(hr);

        // ---- Channel
        LinearLayout cc = Ui.card(c, col, "Channel keluaran");
        for (int i = 0; i < 8; i++) {
            LinearLayout r = Ui.row(c);
            r.addView(Ui.text(c, "CH" + (i + 1), 12, Ui.MUT, true), Ui.lp(Ui.dp(c, 38), -2));
            chBar[i] = new Ui.BarView(c, true);
            r.addView(chBar[i], new LinearLayout.LayoutParams(0, Ui.dp(c, 10), 1f));
            chVal[i] = Ui.text(c, "0", 12, Ui.TX, false);
            chVal[i].setGravity(Gravity.END);
            chVal[i].setTypeface(android.graphics.Typeface.MONOSPACE);
            r.addView(chVal[i], Ui.lp(Ui.dp(c, 48), -2));
            cc.addView(r, Ui.lpm(c, -1, -2, 0, 4, 0, 4));
        }

        // ---- Stik mentah
        LinearLayout sc = Ui.card(c, col, "Stik dan saklar remot");
        String[] rn = {"X kiri", "Y kiri", "X kanan", "Y kanan"};
        for (int i = 0; i < 4; i++) {
            LinearLayout r = Ui.row(c);
            r.addView(Ui.text(c, rn[i], 12, Ui.MUT, false), Ui.lp(Ui.dp(c, 60), -2));
            rawBar[i] = new Ui.BarView(c, false);
            r.addView(rawBar[i], new LinearLayout.LayoutParams(0, Ui.dp(c, 8), 1f));
            rawVal[i] = Ui.text(c, "0", 12, Ui.TX, false);
            rawVal[i].setGravity(Gravity.END);
            rawVal[i].setTypeface(android.graphics.Typeface.MONOSPACE);
            r.addView(rawVal[i], Ui.lp(Ui.dp(c, 48), -2));
            sc.addView(r, Ui.lpm(c, -1, -2, 0, 4, 0, 4));
        }
        tvSw = Ui.text(c, "SW1 --   SW2 --", 13, Ui.TX, false);
        sc.addView(tvSw, Ui.lpm(c, -2, -2, 0, 6, 0, 0));

        // ---- Timer
        LinearLayout tc = Ui.card(c, col, "Timer");
        tvTimer = Ui.text(c, "00:00", 34, Ui.TX, true);
        tvTimer.setTypeface(android.graphics.Typeface.MONOSPACE, android.graphics.Typeface.BOLD);
        tc.addView(tvTimer);
        tvTimerInfo = Ui.text(c, "", 12, Ui.MUT, false);
        tc.addView(tvTimerInfo);
        LinearLayout tb = Ui.row(c);
        final android.widget.Button run = Ui.button(c, "Jalan / Berhenti", Ui.ACC, 0xFF001018);
        run.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                JSONObject d = host.data();
                host.set("trun", d != null && d.optInt("trun") == 1 ? 0 : 1);
            }
        });
        android.widget.Button rst = Ui.button(c, "Reset", Ui.CARD2, Ui.TX);
        rst.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { host.set("treset", 1); }
        });
        tb.addView(run, Ui.lpm(c, 0, Ui.dp(c, 44), 0, 6, 4, 0));
        ((LinearLayout.LayoutParams) run.getLayoutParams()).weight = 1f;
        tb.addView(rst, Ui.lpm(c, 0, Ui.dp(c, 44), 4, 6, 0, 0));
        ((LinearLayout.LayoutParams) rst.getLayoutParams()).weight = 1f;
        tc.addView(tb);

        // ---- Sistem
        LinearLayout sy = Ui.card(c, col, "Sistem");
        tvSys = Ui.text(c, "--", 13, Ui.TX, false);
        tvSys.setLineSpacing(0f, 1.25f);
        sy.addView(tvSys);
        return view;
    }

    @Override void onData(JSONObject d) {
        super.onData(d);
        float v = (float) d.optDouble("v", 0);
        gauge.set(d.optInt("vp", 0), String.format(Locale.US, "%.2fV", v), d.optInt("vp", 0) + "%");
        tvMode.setText(d.optString("mode", "--"));
        int bars = d.optInt("link", 0);
        signal.set(bars);
        tvLink.setText("  Link NRF " + bars + "/5" + (d.optInt("lost") == 1 ? "  (putus)" : ""));
        float rxv = (float) d.optDouble("rxv", -1);
        tvRx.setText(rxv > 0 ? String.format(Locale.US, "Baterai RX: %.2f V", rxv) : "Baterai RX: belum ada data");
        StringBuilder chips = new StringBuilder();
        chips.append(d.optInt("src") == 1 ? "Input HP" : "Input REMOT");
        chips.append(d.optInt("hpok") == 1 ? "  •  Data HP OK" : (d.optInt("src") == 1 ? "  •  FAILSAFE" : ""));
        if (d.optInt("lb") == 1) chips.append("\n⚠ Baterai remot rendah");
        tvChips.setText(chips.toString());

        for (int i = 0; i < 8; i++) {
            int val = Controls.ga(d, "ch", i);
            chBar[i].set(val / 4095f, Ui.ACC);
            chVal[i].setText(String.valueOf(val));
        }
        for (int i = 0; i < 4; i++) {
            int val = Controls.ga(d, "raw", i);
            rawBar[i].set(val / 4095f, Ui.OK);
            rawVal[i].setText(String.valueOf(val));
        }
        tvSw.setText("SW1 " + (Controls.ga(d, "sw", 0) == 1 ? "ON" : "off") + "     SW2 " + (Controls.ga(d, "sw", 1) == 1 ? "ON" : "off"));

        int sec = d.optInt("tel", 0);
        tvTimer.setText(String.format(Locale.US, "%02d:%02d", sec / 60, sec % 60));
        tvTimer.setTextColor(d.optInt("trun") == 1 ? Ui.OK : Ui.TX);
        int tmin = d.optInt("tmin", 0);
        int tr = d.optInt("ttrig", 0);
        tvTimerInfo.setText((tmin == 0 ? "Stopwatch" : "Hitung mundur " + tmin + " menit")
                + "  •  Pemicu " + (tr == 0 ? "manual" : "CH" + tr));

        int up = d.optInt("up", 0);
        tvSys.setText(String.format(Locale.US,
                "Firmware %s\nNyala %02d:%02d:%02d\nPaket NRF %d/dtk, ACK %d/dtk\nMemori bebas %d KB\nModel aktif %d",
                d.optString("fw", "?"), up / 3600, (up / 60) % 60, up % 60,
                d.optInt("txr"), d.optInt("ackr"), d.optInt("heap") / 1024, d.optInt("mdl") + 1));
    }
}
