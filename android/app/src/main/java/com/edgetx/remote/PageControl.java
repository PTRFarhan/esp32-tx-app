package com.edgetx.remote;

import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import org.json.JSONObject;

import java.util.Locale;

/**
 * Layar kendali: stik kiri = CH4 (X) dan CH3 (Y, throttle bertahan), stik kanan = CH1 dan CH2.
 * CH5 dan CH6 saklar (0 atau 4095). CH7 dan CH8 saklar 3 posisi (0, 2048, 4095).
 * Tombol model di tengah menampilkan model yang dipakai dan membuka daftar model dari remot.
 */
class PageControl extends Page {
    final int[] ch = {Frame.MID, Frame.MID, 0, Frame.MID, 0, 0, Frame.MID, Frame.MID};
    private StickView stickLeft, stickRight;
    private Button btnCh5, btnCh6;
    private LinearLayout tri7, tri8;
    private final int[] triPos = {1, 1};   // 0 = 0, 1 = tengah (2048), 2 = 4095
    private TextView tvRead, tvHint, tv7, tv8;
    private Button btnModel;
    private boolean ch5on, ch6on;
    private LinearLayout root, center;
    private LinearLayout srcBox;
    private boolean landscape = true;

    PageControl(Host h) { super(h); }

    @Override View build() {
        root = new LinearLayout(c);
        root.setBackgroundColor(Ui.BG);
        stickLeft = new StickView(c, true, "CH4 / CH3");
        stickRight = new StickView(c, false, "CH1 / CH2");

        center = new LinearLayout(c);
        center.setOrientation(LinearLayout.VERTICAL);
        center.setPadding(Ui.dp(c, 8), Ui.dp(c, 4), Ui.dp(c, 8), Ui.dp(c, 4));

        srcBox = new LinearLayout(c);
        srcBox.setOrientation(LinearLayout.VERTICAL);
        k.choice(srcBox, null, "src", new String[]{"Input: REMOT", "Input: HP"}, new int[]{0, 1},
                new Controls.Getter() { @Override public int get(JSONObject d) { return Controls.gi(d, "src"); } });
        center.addView(srcBox);

        tvHint = Ui.text(c, "", 12, Ui.MUT, false);
        center.addView(tvHint, Ui.lpm(c, -1, -2, 0, 2, 0, 4));

        LinearLayout r1 = Ui.row(c);
        btnCh5 = Ui.button(c, "CH5 OFF", Ui.CARD2, Ui.TX);
        btnCh6 = Ui.button(c, "CH6 OFF", Ui.CARD2, Ui.TX);
        btnCh5.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { ch5on = !ch5on; renderSwitches(); }
        });
        btnCh6.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { ch6on = !ch6on; renderSwitches(); }
        });
        r1.addView(btnCh5, Ui.lpm(c, 0, Ui.dp(c, 42), 0, 0, 3, 0));
        ((LinearLayout.LayoutParams) btnCh5.getLayoutParams()).weight = 1f;
        r1.addView(btnCh6, Ui.lpm(c, 0, Ui.dp(c, 42), 3, 0, 0, 0));
        ((LinearLayout.LayoutParams) btnCh6.getLayoutParams()).weight = 1f;
        center.addView(r1, Ui.lpm(c, -1, -2, 0, 2, 0, 4));

        btnModel = Ui.button(c, "MODEL: -", Ui.CARD2, Ui.TX);
        btnModel.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { pickModel(); }
        });
        center.addView(btnModel, Ui.lpm(c, -1, Ui.dp(c, 42), 0, 0, 0, 4));

        tv7 = Ui.text(c, "", 12, Ui.TX, false);
        tri7 = tri(0, 6, tv7, "CH7");
        tv8 = Ui.text(c, "", 12, Ui.TX, false);
        tri8 = tri(1, 7, tv8, "CH8");
        center.addView(tv7);
        center.addView(tri7, Ui.lpm(c, -1, Ui.dp(c, 40), 0, 0, 0, 4));
        center.addView(tv8);
        center.addView(tri8, Ui.lpm(c, -1, Ui.dp(c, 40), 0, 0, 0, 4));

        tvRead = Ui.text(c, "", 10, Ui.MUT, false);
        tvRead.setTypeface(android.graphics.Typeface.MONOSPACE);
        center.addView(tvRead);

        renderSwitches();
        layout();
        view = root;
        return root;
    }

    private static final int[] TRI_VAL = {0, Frame.MID, Frame.MAX};
    private static final String[] TRI_TXT = {"0", "Tengah", "4095"};

    /** Saklar 3 posisi: hanya 0, tengah (2048), atau 4095. */
    private LinearLayout tri(final int slot, final int idx, final TextView label, final String name) {
        final LinearLayout row = Ui.row(c);
        for (int i = 0; i < 3; i++) {
            final int pos = i;
            Button b = Ui.button(c, TRI_TXT[i], Ui.CARD2, Ui.TX);
            b.setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View v) { setTri(slot, idx, pos, label, name, row); }
            });
            LinearLayout.LayoutParams lp = Ui.lpm(c, 0, -1, i == 0 ? 0 : 3, 0, i == 2 ? 0 : 3, 0);
            lp.weight = 1f;
            row.addView(b, lp);
        }
        setTri(slot, idx, 1, label, name, row);
        return row;
    }

    private void setTri(int slot, int idx, int pos, TextView label, String name, LinearLayout row) {
        triPos[slot] = pos;
        ch[idx] = TRI_VAL[pos];
        label.setText(name + "  " + TRI_VAL[pos]);
        for (int i = 0; i < 3; i++) {
            Button b = (Button) row.getChildAt(i);
            Ui.paint(c, b, i == pos ? Ui.ACC : Ui.CARD2, i == pos ? 0xFF000000 : Ui.TX);
        }
    }

    private void renderSwitches() {
        btnCh5.setText(ch5on ? "CH5 ON" : "CH5 OFF");
        btnCh6.setText(ch6on ? "CH6 ON" : "CH6 OFF");
        Ui.paint(c, btnCh5, ch5on ? Ui.OK : Ui.CARD2, ch5on ? 0xFF000000 : Ui.TX);
        Ui.paint(c, btnCh6, ch6on ? Ui.OK : Ui.CARD2, ch6on ? 0xFF000000 : Ui.TX);
    }

    /** Susun ulang untuk landscape (tiga kolom) atau portrait (stik di atas, kontrol di bawah). */
    private void layout() {
        root.removeAllViews();
        detach(stickLeft); detach(stickRight); detach(center);
        if (landscape) {
            root.setOrientation(LinearLayout.HORIZONTAL);
            root.setGravity(Gravity.CENTER);
            root.addView(stickLeft, new LinearLayout.LayoutParams(0, -1, 1f));
            ScrollView sv = new ScrollView(c);
            sv.setVerticalScrollBarEnabled(false);
            sv.addView(center, new ScrollView.LayoutParams(-1, -2));
            root.addView(sv, new LinearLayout.LayoutParams(0, -1, 1.15f));
            root.addView(stickRight, new LinearLayout.LayoutParams(0, -1, 1f));
        } else {
            root.setOrientation(LinearLayout.VERTICAL);
            LinearLayout sticks = new LinearLayout(c);
            sticks.setOrientation(LinearLayout.HORIZONTAL);
            sticks.addView(stickLeft, new LinearLayout.LayoutParams(0, -1, 1f));
            sticks.addView(stickRight, new LinearLayout.LayoutParams(0, -1, 1f));
            root.addView(sticks, new LinearLayout.LayoutParams(-1, 0, 1f));
            ScrollView sv = new ScrollView(c);
            sv.setVerticalScrollBarEnabled(false);
            sv.addView(center, new ScrollView.LayoutParams(-1, -2));
            root.addView(sv, new LinearLayout.LayoutParams(-1, 0, 1.05f));
        }
    }

    private static void detach(View v) {
        if (v.getParent() instanceof android.view.ViewGroup) ((android.view.ViewGroup) v.getParent()).removeView(v);
    }

    @Override void onConfig(boolean land) {
        if (land == landscape) return;
        landscape = land;
        if (root != null) layout();
    }

    /** Dipanggil tiap 25 ms sebelum paket dikirim. */
    void sample() {
        ch[0] = Frame.fromUnit(stickRight.getVx());
        ch[1] = Frame.fromUnit(stickRight.getVy());
        ch[2] = Frame.fromUnit(stickLeft.getVy());
        ch[3] = Frame.fromUnit(stickLeft.getVx());
        ch[4] = ch5on ? Frame.MAX : 0;
        ch[5] = ch6on ? Frame.MAX : 0;
    }

    /** Semua kendali kembali ke posisi aman. */
    void resetAll() {
        if (stickLeft == null) return;
        stickLeft.reset();
        stickRight.reset();
        ch5on = false;
        ch6on = false;
        renderSwitches();
        setTri(0, 6, 1, tv7, "CH7", tri7);
        setTri(1, 7, 1, tv8, "CH8", tri8);
    }

    /** Perilaku throttle saat dilepas: StickView.THR_HOLD, THR_CENTER atau THR_BOTTOM. */
    void applyThrottleMode(int mode) {
        if (stickLeft != null) stickLeft.setThrottleMode(mode);
    }

    @Override void onData(org.json.JSONObject d) {
        super.onData(d);
        if (btnModel == null) return;
        String nm = PageModel.nameOf(d, d.optInt("mdl", 0));
        btnModel.setText("MODEL: " + (nm.isEmpty() ? "-" : nm) + "   ▾");
    }

    /** Daftar model dari remot. Memilih satu langsung memindahkan remot ke model itu. */
    private void pickModel() {
        org.json.JSONObject d = host.data();
        if (!host.ready() || d == null) { host.toast("Hubungkan ke remot dulu"); return; }
        org.json.JSONArray a = d.optJSONArray("mn");
        if (a == null || a.length() == 0) { host.toast("Daftar model belum diterima"); return; }
        final String[] names = new String[a.length()];
        for (int i = 0; i < names.length; i++) names[i] = a.optString(i, "?");
        new android.app.AlertDialog.Builder(c, android.R.style.Theme_Material_Dialog_Alert)
                .setTitle("Pilih model")
                .setItems(names, new android.content.DialogInterface.OnClickListener() {
                    @Override public void onClick(android.content.DialogInterface dlg, int which) {
                        btnModel.setText("MODEL: " + names[which] + "   ▾");
                        host.set("model", which);
                        host.cmd("G");
                    }
                }).show();
    }

    void setHint(String s) {
        if (tvHint != null) tvHint.setText(s);
    }

    void refreshReadout() {
        if (tvRead == null) return;
        tvRead.setText(String.format(Locale.US,
                "1:%4d 2:%4d 3:%4d 4:%4d%n5:%4d 6:%4d 7:%4d 8:%4d",
                ch[0], ch[1], ch[2], ch[3], ch[4], ch[5], ch[6], ch[7]));
    }

    @Override void onHide() { resetAll(); }
}
