package com.edgetx.remote;

import android.app.AlertDialog;
import android.content.DialogInterface;
import android.text.InputFilter;
import android.text.Spanned;
import android.view.View;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONObject;

/**
 * Pengaturan per model. Daftar model berasal dari remot dan bisa ditambah, diberi nama ulang, dan dihapus
 * dari sini. Perubahan langsung ikut ke menu MODEL di remot (maksimum 10 model, nama maksimum 9 karakter).
 */
class PageModel extends Page {
    static final int MAX_MODELS = 10, NAME_MAX = 9;
    private static final String[] DEFAULT_NAMES = {"PLANE", "BOAT", "ROVER", "UNIVERSAL", "MODEL 5"};

    // nilai channel yang sedang tampil di grafik (disegarkan dari remot dan dari geseran)
    private final int[] curDr = {100, 100, 100, 100}, curEx = new int[4];
    private final int[] curLo = {100, 100, 100, 100, 100, 100}, curHi = {100, 100, 100, 100, 100, 100}, curTrim = new int[6];
    private final boolean[] curRev = new boolean[6];
    private int selCh = 0;
    private Graphs.CurveView curveView;
    private TextView curveInfo;
    private final LinearLayout[] chGroup = new LinearLayout[6];
    private final Button[] chChip = new Button[6];
    private final int[] fmS = new int[4], fmD = {1, 1, 1, 1}, fmV = {50, 50, 50, 50};
    private final TextView[] fmSum = new TextView[4];

    private LinearLayout listBox;
    private String listKey = "";
    private int count;

    PageModel(Host h) { super(h); }

    /** Nama model ke idx dari data remot. Firmware lama tanpa daftar memakai nama bawaan. */
    static String nameOf(JSONObject d, int idx) {
        JSONArray a = d == null ? null : d.optJSONArray("mn");
        if (a != null) return (idx >= 0 && idx < a.length()) ? a.optString(idx, "") : "";
        return (idx >= 0 && idx < DEFAULT_NAMES.length) ? DEFAULT_NAMES[idx] : "";
    }

    /** Huruf besar, hanya huruf, angka, spasi, titik, minus, garis bawah. */
    static String cleanName(String in) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < in.length() && sb.length() < NAME_MAX; i++) {
            char ch = Character.toUpperCase(in.charAt(i));
            boolean ok = (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ' || ch == '.' || ch == '-' || ch == '_';
            if (!ok) continue;
            if (ch == ' ' && sb.length() == 0) continue;
            sb.append(ch);
        }
        return sb.toString().trim();
    }

    private static Controls.Getter gi(final String key) {
        return new Controls.Getter() { @Override public int get(JSONObject d) { return Controls.gi(d, key); } };
    }

    private static Controls.Getter ga(final String key, final int i) {
        return new Controls.Getter() { @Override public int get(JSONObject d) { return Controls.ga(d, key, i); } };
    }

    @Override View build() {
        LinearLayout col = scrollColumn();

        LinearLayout m = Ui.card(c, col, "Daftar model");
        listBox = new LinearLayout(c);
        listBox.setOrientation(LinearLayout.VERTICAL);
        m.addView(listBox);
        k.action(m, "+ Tambah model", Ui.ACC, 0xFF001018, new View.OnClickListener() {
            @Override public void onClick(View v) { askAdd(); }
        });
        k.note(m, "Ketuk nama untuk memakai model itu. Daftar ini sama dengan menu MODEL di remot (maksimum 10). Tiap model menyimpan reverse, trim, rate, expo, endpoint, failsafe, dan mixing sendiri.");

        buildChannelCard(col);
        buildFreeMix(col);

        LinearLayout x = Ui.card(c, col, "Mixing ELEVON / V-TAIL");
        k.choice(x, "Tipe", "mixt", new String[]{"OFF", "ELEVON", "V-TAIL"}, new int[]{0, 1, 2}, gi("mxt"));
        String[] chn = {"CH1", "CH2", "CH3", "CH4", "CH5", "CH6"};
        k.choice(x, "Channel A (ELEVON: aileron, V-TAIL: elevator)", "mixa", chn, Controls.seq(1, 6), gi("mxa"));
        k.choice(x, "Channel B (ELEVON: elevator, V-TAIL: rudder)", "mixb", chn, Controls.seq(1, 6), gi("mxb"));
        k.slider(x, "Bobot", "mixw", 10, 100, Controls.PCT, gi("mxw"));
        k.note(x, "Mengganti tipe mengembalikan pasangan channel ke bawaan.");

        LinearLayout f = Ui.card(c, col, "Failsafe HP");
        for (int i = 0; i < 8; i++) k.slider(f, "CH" + (i + 1), "fs" + i, 0, 100, Controls.PCT, ga("fs", i));
        k.note(f, "Nilai yang dikirim ke RX kalau data dari HP berhenti. Hanya untuk mode input HP.");

        LinearLayout tm = Ui.card(c, col, "Timer");
        k.slider(tm, "Hitung mundur (0 = stopwatch)", "tmin", 0, 99, new Controls.Fmt() {
            @Override public String fmt(int val) { return val == 0 ? "stopwatch" : val + " mnt"; }
        }, gi("tmin"));
        String[] trig = {"Manual", "CH1", "CH2", "CH3", "CH4", "CH5", "CH6", "CH7", "CH8"};
        k.choice(tm, "Pemicu", "ttrig", trig, Controls.seq(0, 9), gi("ttrig"));
        k.slider(tm, "Batas pemicu", "tthr", 5, 95, Controls.PCT, gi("tthr"));
        k.choice(tm, "Arah", "tdir", new String[]{"Di bawah batas", "Di atas batas"}, new int[]{0, 1}, gi("tdir"));
        return view;
    }

    // ---------------------------------------------------------------- kurva channel
    private void refreshCurve() {
        int i = selCh;
        int dr = i < 4 ? curDr[i] : 100, ex = i < 4 ? curEx[i] : 0;
        curveView.setCurve(dr, ex, curLo[i], curHi[i], curTrim[i], curRev[i]);
        StringBuilder sb = new StringBuilder("CH" + (i + 1) + ":  ");
        if (i < 4) sb.append("rate ").append(dr).append("%   expo ").append(ex).append("%   ");
        sb.append("endpoint ").append(curLo[i]).append("% / ").append(curHi[i]).append("%");
        if (curTrim[i] != 0) sb.append("   trim ").append(curTrim[i] > 0 ? "+" : "").append(curTrim[i]);
        if (curRev[i]) sb.append("   REVERSE");
        curveInfo.setText(sb.toString());
    }

    private void pickChannel(int i) {
        selCh = i;
        for (int g = 0; g < 6; g++) {
            chGroup[g].setVisibility(g == i ? View.VISIBLE : View.GONE);
            Ui.paint(c, chChip[g], g == i ? Ui.ACC : Ui.CARD2, g == i ? 0xFF001018 : Ui.MUT);
        }
        refreshCurve();
        JSONObject d = host.data();
        if (d != null) curveView.setLive(Controls.ga(d, "ch", selCh));
    }

    private void buildChannelCard(LinearLayout col) {
        LinearLayout card = Ui.card(c, col, "Rate, expo, dan endpoint");
        LinearLayout chips = Ui.row(c);
        for (int i = 0; i < 6; i++) {
            final int idx = i;
            Button b = Ui.button(c, "CH" + (i + 1), Ui.CARD2, Ui.MUT);
            b.setTextSize(12);
            b.setMinWidth(0);
            b.setMinimumWidth(0);
            b.setPadding(0, 0, 0, 0);
            b.setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View v) { pickChannel(idx); }
            });
            chChip[i] = b;
            LinearLayout.LayoutParams lp = Ui.lpm(c, 0, Ui.dp(c, 38), 2, 0, 2, 0);
            lp.weight = 1f;
            chips.addView(b, lp);
        }
        card.addView(chips, Ui.lpm(c, -1, -2, 0, 0, 0, 8));

        curveView = new Graphs.CurveView(c);
        card.addView(curveView, Ui.lpm(c, -1, -2, 0, 0, 0, 4));
        curveInfo = Ui.text(c, "", 12, Ui.TX, false);
        card.addView(curveInfo, Ui.lpm(c, -1, -2, 0, 2, 0, 6));
        k.note(card, "Garis biru = keluaran channel terhadap gerak stik. Garis putus-putus = lurus 100%. Titik hijau = posisi stik sekarang. Rate mengecilkan gerak, expo melembutkan di sekitar tengah, endpoint membatasi ujung gerak.");

        for (int i = 0; i < 6; i++) {
            final int ch = i;
            LinearLayout g = new LinearLayout(c);
            g.setOrientation(LinearLayout.VERTICAL);
            if (i < 4) {
                k.slider(g, "Rate (dual rate)", "dr" + i, 30, 100, Controls.PCT, ga("dr", i), new Controls.Change() {
                    @Override public void on(int v) { curDr[ch] = v; if (selCh == ch) refreshCurve(); }
                });
                k.slider(g, "Expo", "ex" + i, 0, 100, Controls.PCT, ga("ex", i), new Controls.Change() {
                    @Override public void on(int v) { curEx[ch] = v; if (selCh == ch) refreshCurve(); }
                });
            }
            k.slider(g, "Endpoint bawah", "lo" + i, 20, 120, Controls.PCT, ga("lo", i), new Controls.Change() {
                @Override public void on(int v) { curLo[ch] = v; if (selCh == ch) refreshCurve(); }
            });
            k.slider(g, "Endpoint atas", "hi" + i, 20, 120, Controls.PCT, ga("hi", i), new Controls.Change() {
                @Override public void on(int v) { curHi[ch] = v; if (selCh == ch) refreshCurve(); }
            });
            k.slider(g, "Trim", "trim" + i, -100, 100, Controls.NUM, ga("trim", i), new Controls.Change() {
                @Override public void on(int v) { curTrim[ch] = v; if (selCh == ch) refreshCurve(); }
            });
            k.toggle(g, "Reverse", "rev" + i, ga("rev", i), new Controls.Change() {
                @Override public void on(int v) { curRev[ch] = v == 1; if (selCh == ch) refreshCurve(); }
            });
            chGroup[i] = g;
            card.addView(g);
        }
        pickChannel(0);
    }

    // ---------------------------------------------------------------- mix bebas
    private static final Controls.Fmt SIGNED = new Controls.Fmt() {
        @Override public String fmt(int v) { return (v > 0 ? "+" : "") + v + "%"; }
    };

    private void refreshMix(int m) {
        if (fmSum[m] == null) return;
        if (fmS[m] == 0) {
            fmSum[m].setText("MIX " + (m + 1) + ":  mati");
            fmSum[m].setTextColor(Ui.MUT);
        } else {
            fmSum[m].setText("MIX " + (m + 1) + ":  CH" + fmS[m] + "  ->  CH" + fmD[m] + "   " + SIGNED.fmt(fmV[m]));
            fmSum[m].setTextColor(Ui.ACC);
        }
    }

    private void buildFreeMix(LinearLayout col) {
        LinearLayout card = Ui.card(c, col, "Mix bebas (semua channel)");
        k.note(card, "Empat mix manual. Tiap mix menambahkan gerak satu channel (dari) ke channel lain (ke) sebesar persen yang diatur. Nilai positif searah, negatif berlawanan arah. Berlaku untuk CH1 sampai CH8 dan tersimpan per model. Contoh: dari CH1 ke CH2 dengan -50% membuat CH2 bergerak setengah ke arah sebaliknya saat CH1 digerakkan.");
        String[] src = {"OFF", "CH1", "CH2", "CH3", "CH4", "CH5", "CH6", "CH7", "CH8"};
        String[] dst = {"CH1", "CH2", "CH3", "CH4", "CH5", "CH6", "CH7", "CH8"};
        for (int i = 0; i < 4; i++) {
            final int m = i;
            fmSum[m] = Ui.text(c, "", 14, Ui.ACC, true);
            card.addView(fmSum[m], Ui.lpm(c, -1, -2, 0, i == 0 ? 6 : 14, 0, 0));
            k.choice(card, "Dari channel", "fms" + m, src, Controls.seq(0, 9), ga("fms", m), new Controls.Change() {
                @Override public void on(int v) { fmS[m] = v; refreshMix(m); }
            });
            k.choice(card, "Ke channel", "fmd" + m, dst, Controls.seq(1, 8), ga("fmd", m), new Controls.Change() {
                @Override public void on(int v) { fmD[m] = v; refreshMix(m); }
            });
            k.slider(card, "Nilai", "fmw" + m, -100, 100, SIGNED, ga("fmw", m), new Controls.Change() {
                @Override public void on(int v) { fmV[m] = v; refreshMix(m); }
            });
            refreshMix(m);
        }
        k.note(card, "Catatan: CH7 dan CH8 normalnya hanya 3 posisi (0, tengah, 4095). Kalau dijadikan tujuan mix, nilainya ikut bergeser halus.");
    }

    // ---------------------------------------------------------------- daftar model
    @Override void onData(JSONObject d) {
        super.onData(d);
        rebuildList(d);
        if (curveView != null) curveView.setLive(Controls.ga(d, "ch", selCh));
    }

    private void rebuildList(JSONObject d) {
        if (listBox == null) return;
        JSONArray a = d.optJSONArray("mn");
        int active = d.optInt("mdl", 0);
        StringBuilder key = new StringBuilder().append(active).append('|');
        int n = a == null ? DEFAULT_NAMES.length : a.length();
        for (int i = 0; i < n; i++) key.append(nameOf(d, i)).append('|');
        if (key.toString().equals(listKey)) return;
        listKey = key.toString();
        count = n;
        listBox.removeAllViews();
        for (int i = 0; i < n; i++) {
            final int idx = i;
            final String nm = nameOf(d, i);
            boolean on = i == active;
            LinearLayout row = Ui.row(c);
            Button b = Ui.button(c, (i + 1) + ".  " + nm + (on ? "    AKTIF" : ""), on ? Ui.ACC : Ui.CARD2, on ? 0xFF001018 : Ui.TX);
            b.setGravity(android.view.Gravity.CENTER_VERTICAL | android.view.Gravity.START);
            b.setPadding(Ui.dp(c, 12), 0, Ui.dp(c, 8), 0);
            b.setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View v) { host.set("model", idx); }
            });
            row.addView(b, Ui.lpm(c, 0, Ui.dp(c, 42), 0, 0, 4, 0));
            ((LinearLayout.LayoutParams) b.getLayoutParams()).weight = 1f;
            Button rn = Ui.button(c, "Nama", Ui.CARD2, Ui.TX);
            rn.setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View v) { askRename(idx, nm); }
            });
            row.addView(rn, Ui.lpm(c, Ui.dp(c, 62), Ui.dp(c, 42), 0, 0, 4, 0));
            Button del = Ui.button(c, "Hapus", Ui.CARD2, Ui.BAD);
            del.setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View v) { askDelete(idx, nm); }
            });
            row.addView(del, Ui.lp(Ui.dp(c, 62), Ui.dp(c, 42)));
            listBox.addView(row, Ui.lpm(c, -1, -2, 0, 3, 0, 3));
        }
    }

    private EditText nameInput(String initial) {
        final EditText e = new EditText(c);
        e.setSingleLine(true);
        e.setText(initial);
        e.setSelection(e.getText().length());
        e.setHint("Nama model, maksimum " + NAME_MAX + " huruf");
        e.setFilters(new InputFilter[]{
                new InputFilter() {
                    @Override public CharSequence filter(CharSequence src, int st, int en, Spanned dst, int ds, int de) {
                        return cleanNameKeepSpace(src.subSequence(st, en).toString());
                    }
                },
                new InputFilter.LengthFilter(NAME_MAX)});
        return e;
    }

    /** Untuk ketikan: sama dengan cleanName, tetapi spasi di tengah boleh dan tanpa pemotongan. */
    private static String cleanNameKeepSpace(String in) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < in.length(); i++) {
            char ch = Character.toUpperCase(in.charAt(i));
            boolean ok = (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ' || ch == '.' || ch == '-' || ch == '_';
            if (ok) sb.append(ch);
        }
        return sb.toString();
    }

    private void askAdd() {
        if (!host.ready()) { host.toast("Hubungkan ke remot dulu"); return; }
        if (count >= MAX_MODELS) { host.toast("Daftar penuh, maksimum " + MAX_MODELS + " model"); return; }
        final EditText e = nameInput("");
        new AlertDialog.Builder(c, android.R.style.Theme_Material_Dialog_Alert)
                .setTitle("Tambah model")
                .setView(e)
                .setNegativeButton("Batal", null)
                .setPositiveButton("Tambah", new DialogInterface.OnClickListener() {
                    @Override public void onClick(DialogInterface dlg, int w) {
                        String nm = cleanName(e.getText().toString());
                        if (nm.isEmpty()) { host.toast("Nama tidak boleh kosong"); return; }
                        host.cmd("MA," + nm);
                        host.cmd("G");
                    }
                }).show();
    }

    private void askRename(final int idx, String cur) {
        if (!host.ready()) { host.toast("Hubungkan ke remot dulu"); return; }
        final EditText e = nameInput(cur);
        new AlertDialog.Builder(c, android.R.style.Theme_Material_Dialog_Alert)
                .setTitle("Ganti nama model")
                .setView(e)
                .setNegativeButton("Batal", null)
                .setPositiveButton("Simpan", new DialogInterface.OnClickListener() {
                    @Override public void onClick(DialogInterface dlg, int w) {
                        String nm = cleanName(e.getText().toString());
                        if (nm.isEmpty()) { host.toast("Nama tidak boleh kosong"); return; }
                        host.cmd("MN," + idx + "," + nm);
                        host.cmd("G");
                    }
                }).show();
    }

    private void askDelete(final int idx, String nm) {
        if (!host.ready()) { host.toast("Hubungkan ke remot dulu"); return; }
        if (count <= 1) { host.toast("Model terakhir tidak bisa dihapus"); return; }
        new AlertDialog.Builder(c, android.R.style.Theme_Material_Dialog_Alert)
                .setMessage("Hapus model " + nm + " beserta semua pengaturannya (rate, trim, endpoint, dan lainnya)? Tidak bisa dibatalkan.")
                .setNegativeButton("Batal", null)
                .setPositiveButton("Hapus", new DialogInterface.OnClickListener() {
                    @Override public void onClick(DialogInterface dlg, int w) {
                        host.cmd("MX," + idx);
                        host.cmd("G");
                    }
                }).show();
    }

    @Override void onAck(String name, int code) {
        if (name.startsWith("MA")) {
            host.toast(code == 200 ? "Model ditambahkan" : (code == 409 ? "Daftar penuh, maksimum " + MAX_MODELS + " model" : "Nama model tidak valid"));
        } else if (name.equals("MN")) {
            host.toast(code == 200 ? "Nama diganti" : "Gagal mengganti nama");
        } else if (name.equals("MX")) {
            host.toast(code == 200 ? "Model dihapus" : (code == 409 ? "Model terakhir tidak bisa dihapus" : "Gagal menghapus model"));
        }
    }
}
