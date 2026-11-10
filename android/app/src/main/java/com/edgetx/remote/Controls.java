package com.edgetx.remote;

import android.app.AlertDialog;
import android.content.Context;
import android.os.SystemClock;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.Switch;
import android.widget.TextView;

import org.json.JSONObject;

import java.util.List;

/** Komponen pengaturan yang tersambung ke remot: geser, saklar, pilihan, tombol. */
final class Controls {
    interface Getter { int get(JSONObject d); }
    interface Fmt { String fmt(int v); }
    /** Dipanggil saat nilai kontrol berubah (digeser pengguna atau disegarkan dari remot). */
    interface Change { void on(int v); }

    /** Satu kontrol yang bisa disegarkan dari data remot. */
    abstract static class Ctl {
        long userAt;
        boolean touching;
        boolean busy() { return touching || SystemClock.uptimeMillis() - userAt < 1500; }
        abstract void bind(JSONObject d);
    }

    private final Host host;
    private final Context c;
    private final List<Ctl> bag;

    Controls(Host host, List<Ctl> bag) {
        this.host = host;
        this.c = host.ctx();
        this.bag = bag;
    }

    static int gi(JSONObject d, String k) { return d.optInt(k, 0); }

    static int ga(JSONObject d, String k, int i) {
        org.json.JSONArray a = d.optJSONArray(k);
        return a == null ? 0 : a.optInt(i, 0);
    }

    private LinearLayout.LayoutParams gap() { return Ui.lpm(c, -1, -2, 0, 6, 0, 6); }

    // ------------------------------------------------------------ geser
    void slider(LinearLayout parent, String label, final String key, final int min, final int max,
                final Fmt fmt, final Getter g) {
        slider(parent, label, key, min, max, fmt, g, null);
    }

    void slider(LinearLayout parent, String label, final String key, final int min, final int max,
                final Fmt fmt, final Getter g, final Change chg) {
        LinearLayout box = new LinearLayout(c);
        box.setOrientation(LinearLayout.VERTICAL);
        LinearLayout top = Ui.row(c);
        TextView l = Ui.text(c, label, 14, Ui.TX, false);
        final TextView val = Ui.text(c, "--", 14, Ui.ACC, true);
        top.addView(l, new LinearLayout.LayoutParams(0, -2, 1f));
        top.addView(val);
        final SeekBar sb = new SeekBar(c);
        sb.setMax(max - min);
        sb.setProgressTintList(Ui.tint(Ui.ACC, Ui.ACC));
        sb.setThumbTintList(Ui.tint(Ui.ACC, Ui.ACC));
        box.addView(top);
        box.addView(sb, Ui.lp(-1, Ui.dp(c, 34)));
        parent.addView(box, gap());

        final Ctl ctl = new Ctl() {
            @Override void bind(JSONObject d) {
                if (busy()) return;
                int v = g.get(d);
                if (v < min) v = min;
                if (v > max) v = max;
                sb.setProgress(v - min);
                val.setText(fmt.fmt(v));
                if (chg != null) chg.on(v);
            }
        };
        sb.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            long lastSend;
            @Override public void onProgressChanged(SeekBar s, int p, boolean user) {
                val.setText(fmt.fmt(p + min));
                if (chg != null && user) chg.on(p + min);
                if (!user) return;
                ctl.userAt = SystemClock.uptimeMillis();
                long now = ctl.userAt;
                if (now - lastSend > 140) {
                    lastSend = now;
                    host.set(key, p + min);
                }
            }
            @Override public void onStartTrackingTouch(SeekBar s) { ctl.touching = true; }
            @Override public void onStopTrackingTouch(SeekBar s) {
                ctl.touching = false;
                ctl.userAt = SystemClock.uptimeMillis();
                host.set(key, s.getProgress() + min);
            }
        });
        bag.add(ctl);
    }

    static final Fmt PCT = new Fmt() { @Override public String fmt(int v) { return v + "%"; } };
    static final Fmt NUM = new Fmt() { @Override public String fmt(int v) { return String.valueOf(v); } };

    // ------------------------------------------------------------ saklar
    void toggle(LinearLayout parent, String label, final String key, final Getter g) {
        toggle(parent, label, key, g, null);
    }

    void toggle(LinearLayout parent, String label, final String key, final Getter g, final Change chg) {
        LinearLayout row = Ui.row(c);
        TextView l = Ui.text(c, label, 14, Ui.TX, false);
        final Switch sw = new Switch(c);
        sw.setThumbTintList(Ui.tint(Ui.ACC, 0xFF8793A1));
        sw.setTrackTintList(Ui.tint(Ui.alpha(Ui.ACC, 110), 0xFF2A3644));
        row.addView(l, new LinearLayout.LayoutParams(0, -2, 1f));
        row.addView(sw);
        parent.addView(row, gap());
        final Ctl ctl = new Ctl() {
            @Override void bind(JSONObject d) {
                if (busy()) return;
                sw.setChecked(g.get(d) == 1);
                if (chg != null) chg.on(sw.isChecked() ? 1 : 0);
            }
        };
        sw.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                ctl.userAt = SystemClock.uptimeMillis();
                host.set(key, sw.isChecked() ? 1 : 0);
                if (chg != null) chg.on(sw.isChecked() ? 1 : 0);
            }
        });
        bag.add(ctl);
    }

    // ------------------------------------------------------------ pilihan
    /** Pilihan segmen (maks 5) atau menu pilih (lebih banyak). values[i] dikirim saat opsi i dipilih. */
    void choice(LinearLayout parent, String label, final String key, final String[] names,
                final int[] values, final Getter g) {
        choice(parent, label, key, names, values, g, null);
    }

    void choice(LinearLayout parent, String label, final String key, final String[] names,
                final int[] values, final Getter g, final Change chg) {
        if (label != null) parent.addView(Ui.text(c, label, 14, Ui.TX, false), Ui.lpm(c, -1, -2, 0, 6, 0, 2));
        if (names.length <= 5) {
            LinearLayout seg = Ui.row(c);
            seg.setBackground(Ui.round(c, Ui.CARD2, 12, 0));
            seg.setPadding(Ui.dp(c, 3), Ui.dp(c, 3), Ui.dp(c, 3), Ui.dp(c, 3));
            final Button[] bs = new Button[names.length];
            final Ctl ctl = new Ctl() {
                @Override void bind(JSONObject d) {
                    if (busy()) return;
                    int cur = g.get(d);
                    for (int i = 0; i < bs.length; i++) mark(bs[i], values[i] == cur);
                    if (chg != null) chg.on(cur);
                }
            };
            for (int i = 0; i < names.length; i++) {
                final int idx = i;
                Button b = new Button(c);
                b.setText(names[i]);
                b.setTextSize(12);
                b.setAllCaps(false);
                b.setMinHeight(0);
                b.setMinimumHeight(Ui.dp(c, 36));
                b.setMinWidth(0);
                b.setMinimumWidth(0);
                b.setTextSize(names.length > 4 ? 11 : 12);
                b.setPadding(Ui.dp(c, 2), 0, Ui.dp(c, 2), 0);
                mark(b, false);
                b.setOnClickListener(new View.OnClickListener() {
                    @Override public void onClick(View v) {
                        ctl.userAt = SystemClock.uptimeMillis();
                        for (int k = 0; k < bs.length; k++) mark(bs[k], k == idx);
                        host.set(key, values[idx]);
                        if (chg != null) chg.on(values[idx]);
                    }
                });
                bs[i] = b;
                seg.addView(b, new LinearLayout.LayoutParams(0, -2, 1f));
            }
            parent.addView(seg, gap());
            bag.add(ctl);
        } else {
            final Button b = Ui.button(c, "--", Ui.CARD2, Ui.TX);
            b.setGravity(Gravity.CENTER_VERTICAL | Gravity.START);
            b.setPadding(Ui.dp(c, 14), 0, Ui.dp(c, 14), 0);
            parent.addView(b, Ui.lpm(c, -1, Ui.dp(c, 44), 0, 4, 0, 6));
            final Ctl ctl = new Ctl() {
                @Override void bind(JSONObject d) {
                    if (busy()) return;
                    int cur = g.get(d);
                    for (int i = 0; i < values.length; i++) if (values[i] == cur) b.setText(names[i] + "   ▾");
                    if (chg != null) chg.on(cur);
                }
            };
            b.setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View v) {
                    new AlertDialog.Builder(c, android.R.style.Theme_Material_Dialog_Alert)
                            .setItems(names, new android.content.DialogInterface.OnClickListener() {
                                @Override public void onClick(android.content.DialogInterface dlg, int which) {
                                    ctl.userAt = SystemClock.uptimeMillis();
                                    b.setText(names[which] + "   ▾");
                                    host.set(key, values[which]);
                                    if (chg != null) chg.on(values[which]);
                                }
                            }).show();
                }
            });
            bag.add(ctl);
        }
    }

    private void mark(Button b, boolean on) {
        Ui.paint(c, b, on ? Ui.ACC : 0x00000000, on ? 0xFF001018 : Ui.MUT);
    }

    static int[] seq(int from, int count) {
        int[] a = new int[count];
        for (int i = 0; i < count; i++) a[i] = from + i;
        return a;
    }

    // ------------------------------------------------------------ tombol
    Button action(LinearLayout parent, String label, int bg, int fg, View.OnClickListener l) {
        Button b = Ui.button(c, label, bg, fg);
        b.setOnClickListener(l);
        parent.addView(b, Ui.lpm(c, -1, Ui.dp(c, 44), 0, 4, 0, 4));
        return b;
    }

    TextView note(LinearLayout parent, String s) {
        TextView t = Ui.text(c, s, 12, Ui.MUT, false);
        parent.addView(t, Ui.lpm(c, -1, -2, 0, 4, 0, 4));
        return t;
    }
}
