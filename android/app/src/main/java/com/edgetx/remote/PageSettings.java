package com.edgetx.remote;

import android.app.AlertDialog;
import android.content.DialogInterface;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.json.JSONObject;

import java.util.Locale;

/** Pengaturan remot: layar, suara, baterai, kalibrasi, konektivitas, cadangan. */
class PageSettings extends Page {
    static final String[] SND = {"Disconnect", "Booting", "Pindah menu", "Scroll", "Klik",
            "Terhubung ke HP (Bluetooth)", "HP masuk ke WiFi", "Alarm RX putus", "Alarm baterai habis", "Timer selesai"};

    private TextView tvCal, tvCalLive;
    private Button btnCal1, btnCal2, btnCal3;
    private TextView tvBk;
    private Graphs.PowerView nrfView;
    private CheckBox cbThrCenter, cbThrBottom;
    private boolean thrBusy;
    private String[] restoreChunks;
    private int restoreIdx = -1;

    PageSettings(Host h) { super(h); }

    private static Controls.Getter gi(final String key) {
        return new Controls.Getter() { @Override public int get(JSONObject d) { return Controls.gi(d, key); } };
    }

    /** Kartu tema tampilan aplikasi: 5 pilihan warna, hanya berlaku di HP (tidak dikirim ke remot). */
    private void buildThemeCard(LinearLayout col) {
        LinearLayout t = Ui.card(c, col, "Tema tampilan aplikasi");
        LinearLayout grid = new LinearLayout(c);
        grid.setOrientation(LinearLayout.VERTICAL);
        LinearLayout row1 = Ui.row(c), row2 = Ui.row(c);
        for (int i = 0; i < Ui.THEMES.length; i++) {
            final int idx = i;
            Ui.Theme th = Ui.THEMES[i];
            LinearLayout cell = new LinearLayout(c);
            cell.setOrientation(LinearLayout.VERTICAL);
            cell.setGravity(android.view.Gravity.CENTER);
            cell.setPadding(Ui.dp(c, 4), Ui.dp(c, 8), Ui.dp(c, 4), Ui.dp(c, 8));
            boolean cur = (i == Ui.themeIndex);
            android.graphics.drawable.GradientDrawable bgD = Ui.round(c, th.card, 14, cur ? th.acc : th.stroke);
            if (cur) bgD.setStroke(Ui.dp(c, 2), th.acc);
            cell.setBackground(bgD);
            LinearLayout swRow = Ui.row(c);
            swRow.setGravity(android.view.Gravity.CENTER);
            swRow.addView(swatch(th.acc), Ui.lpm(c, Ui.dp(c, 16), Ui.dp(c, 16), 2, 0, 2, 0));
            swRow.addView(swatch(th.ok), Ui.lpm(c, Ui.dp(c, 16), Ui.dp(c, 16), 2, 0, 2, 0));
            swRow.addView(swatch(th.bad), Ui.lpm(c, Ui.dp(c, 16), Ui.dp(c, 16), 2, 0, 2, 0));
            cell.addView(swRow, Ui.lpm(c, -2, -2, 0, 0, 0, 6));
            TextView lbl = Ui.text(c, th.name, 11, th.tx, cur);
            lbl.setGravity(android.view.Gravity.CENTER);
            cell.addView(lbl);
            cell.setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View v) {
                    if (idx == Ui.themeIndex) return;
                    Ui.saveTheme(c, idx);
                    host.recreateUi();
                }
            });
            (i < 3 ? row1 : row2).addView(cell, new LinearLayout.LayoutParams(0, -2, 1f));
        }
        LinearLayout pad = new LinearLayout(c);
        pad.addView(new View(c), new LinearLayout.LayoutParams(0, -2, 1f));
        row2.addView(pad, new LinearLayout.LayoutParams(0, -2, 1f));
        grid.addView(row1, Ui.lpm(c, -1, -2, 0, 0, 0, 8));
        grid.addView(row2, Ui.lp(-1, -2));
        t.addView(grid);
        k.note(t, "Mengganti warna tampilan aplikasi di HP ini saja. Tidak memengaruhi layar OLED di remot (lihat Tampilan dashboard remot di bawah).");
    }

    private View swatch(int color) {
        View v = new View(c);
        v.setBackground(Ui.round(c, color, 8, 0));
        return v;
    }

    @Override View build() {
        LinearLayout col = scrollColumn();

        buildThemeCard(col);

        LinearLayout th = Ui.card(c, col, "Throttle stik HP");
        cbThrCenter = thrCheck(th, "Throttle kembali ke tengah saat dilepas");
        cbThrBottom = thrCheck(th, "Throttle kembali ke bawah saat dilepas");
        int tm = ((MainActivity) c).throttleMode();
        cbThrCenter.setChecked(tm == StickView.THR_CENTER);
        cbThrBottom.setChecked(tm == StickView.THR_BOTTOM);
        cbThrCenter.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override public void onCheckedChanged(CompoundButton b, boolean on) { thrChanged(true, on); }
        });
        cbThrBottom.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override public void onCheckedChanged(CompoundButton b, boolean on) { thrChanged(false, on); }
        });
        k.note(th, "Tanpa centang, throttle bertahan di posisi terakhir (bawaan, cocok untuk pesawat). Centang tengah untuk perahu atau rover, centang bawah supaya throttle turun sendiri ke 0 saat dilepas. Hanya berlaku di aplikasi ini.");

        LinearLayout s = Ui.card(c, col, "Layar");
        k.slider(s, "Kecerahan OLED", "bri", 0, 10, Controls.NUM, gi("bri"));
        k.choice(s, "Tema OLED", "thm", new String[]{"Terang", "Gelap"}, new int[]{0, 1}, gi("thm"));

        LinearLayout a = Ui.card(c, col, "Suara");
        k.slider(a, "Volume", "svol", 1, 10, Controls.NUM, gi("svol"));
        for (int i = 0; i < SND.length; i++) {
            LinearLayout row = Ui.row(c);
            LinearLayout inner = new LinearLayout(c);
            inner.setOrientation(LinearLayout.VERTICAL);
            k.toggle(inner, SND[i], "snd" + i, maskGetter(i));
            row.addView(inner, new LinearLayout.LayoutParams(0, -2, 1f));
            Button t = Ui.button(c, "Tes", Ui.CARD2, Ui.TX);
            final int idx = i;
            t.setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View v) { host.set("test", idx); }
            });
            row.addView(t, Ui.lpm(c, Ui.dp(c, 56), Ui.dp(c, 36), 8, 0, 0, 0));
            a.addView(row);
        }

        LinearLayout ds = Ui.card(c, col, "Tampilan dashboard remot");
        k.choice(ds, "Gaya dashboard", "dash", new String[]{"1 Klasik", "2 Bar 8CH", "3 Timer", "4 Vertikal", "5 Gimbal"}, new int[]{0, 1, 2, 3, 4}, gi("dsh"));
        k.note(ds, "Memilih tata letak layar utama OLED. Klasik: dua stik dan status. Bar 8CH: delapan channel dengan angka persen. Timer: waktu besar dengan empat bar stik. Vertikal: delapan bar tegak dengan sakelar. Gimbal: dua lingkaran stik dengan timer. Pilihan disimpan di remot. Dari tombol remot: Menu, Tampilan Dashboard, lalu putar untuk melihat pratinjau langsung.");

        LinearLayout bz = Ui.card(c, col, "Buzzer");
        k.choice(bz, "Jenis buzzer", "btype", new String[]{"Aktif (ON/OFF)", "Pasif (bernada)"}, new int[]{0, 1}, gi("btype"));
        k.slider(bz, "Nada buzzer pasif", "btone", 10, 50, new Controls.Fmt() {
            @Override public String fmt(int v) { return String.format(Locale.US, "%.1f kHz", v / 10f); }
        }, gi("btone"));
        k.action(bz, "Tes bunyi booting", Ui.CARD2, Ui.TX, new View.OnClickListener() {
            @Override public void onClick(View v) { host.set("test", 1); }
        });
        k.note(bz, "Buzzer pasif paling keras di frekuensi resonansinya, biasanya 2.5 sampai 4 kHz. Geser nada sampai bunyinya paling keras, lalu atur Volume di atas. Volume memakai 10 tingkat yang naik rata di telinga, dari sangat pelan sampai keras penuh. Jenis Aktif dipakai untuk buzzer yang berbunyi sendiri saat diberi tegangan.");

        LinearLayout nrf = Ui.card(c, col, "Daya pancar NRF");
        nrfView = new Graphs.PowerView(c);
        nrf.addView(nrfView, Ui.lpm(c, -1, -2, 0, 0, 0, 6));
        buildNrfChoice(nrf);
        k.note(nrf, "Mengatur kekuatan pancar dari remot ke receiver. Receiver tidak perlu diubah atau diprogram ulang, karena hanya sisi pemancar yang berubah. MAX = jarak terjauh (bawaan). Tiap turun satu tingkat, jangkauan kira-kira setengahnya. Pakai daya kecil untuk uji di dalam ruangan atau menghemat baterai. OFF mematikan pancaran sama sekali (RX masuk failsafe), jadi jangan dipakai saat terbang. Pengaturan OFF disimpan: sesudah remot dihidupkan ulang tetap OFF sampai Anda mengubahnya. Perubahan langsung berlaku.");

        LinearLayout b = Ui.card(c, col, "Alarm baterai remot");
        k.slider(b, "Batas bunyi", "blow", 50, 84, new Controls.Fmt() {
            @Override public String fmt(int v) { return String.format(Locale.US, "%.1f V", v / 10f); }
        }, gi("blow"));

        LinearLayout cal = Ui.card(c, col, "Kalibrasi stik");
        tvCal = Ui.text(c, "", 13, Ui.TX, false);
        cal.addView(tvCal);
        tvCalLive = Ui.text(c, "", 11, Ui.MUT, false);
        tvCalLive.setTypeface(android.graphics.Typeface.MONOSPACE);
        cal.addView(tvCalLive, Ui.lpm(c, -1, -2, 0, 4, 0, 4));
        LinearLayout cr = Ui.row(c);
        btnCal1 = Ui.button(c, "Mulai", Ui.ACC, 0xFF001018);
        btnCal2 = Ui.button(c, "Lanjut", Ui.OK, 0xFF001008);
        btnCal3 = Ui.button(c, "Batal", Ui.CARD2, Ui.TX);
        btnCal1.setOnClickListener(new View.OnClickListener() { @Override public void onClick(View v) { host.set("cal", 1); } });
        btnCal2.setOnClickListener(new View.OnClickListener() { @Override public void onClick(View v) { host.set("cal", 2); } });
        btnCal3.setOnClickListener(new View.OnClickListener() { @Override public void onClick(View v) { host.set("cal", 3); } });
        for (Button x : new Button[]{btnCal1, btnCal2, btnCal3}) {
            LinearLayout.LayoutParams p = Ui.lpm(c, 0, Ui.dp(c, 42), 3, 0, 3, 0);
            p.weight = 1f;
            cr.addView(x, p);
        }
        cal.addView(cr);
        k.note(cal, "Langkah 1: gerakkan kedua stik penuh ke semua sisi, tekan Lanjut. Langkah 2: lepas stik di tengah, tekan Lanjut lagi untuk menyimpan.");

        LinearLayout n = Ui.card(c, col, "Konektivitas");
        k.note(n, "Bluetooth dan WiFi tidak bisa menyala bersamaan. Aplikasi ini memakai Bluetooth. WiFi hanya dipakai saat update firmware (tab Update).");
        k.action(n, "Matikan Bluetooth dan putuskan aplikasi", Ui.CARD2, Ui.BAD, new View.OnClickListener() {
            @Override public void onClick(View v) {
                new AlertDialog.Builder(c, android.R.style.Theme_Material_Dialog_Alert)
                        .setMessage("Bluetooth remot dimatikan dan aplikasi terputus. Hidupkan lagi dari menu KONEKTIVITAS di remot. Lanjut?")
                        .setNegativeButton("Batal", null)
                        .setPositiveButton("Matikan", new DialogInterface.OnClickListener() {
                            @Override public void onClick(DialogInterface dlg, int w) { host.set("bt", 0); }
                        }).show();
            }
        });
        k.note(n, "Untuk memakai web WiFi remot: matikan Bluetooth di sini, lalu nyalakan WiFi dari menu KONEKTIVITAS di remot.");

        LinearLayout bk = Ui.card(c, col, "Cadangan pengaturan");
        k.action(bk, "Simpan cadangan ke file", Ui.ACC, 0xFF001018, new View.OnClickListener() {
            @Override public void onClick(View v) {
                tvBk.setText("Mengambil data dari remot...");
                host.cmd("BK");
            }
        });
        k.action(bk, "Pulihkan dari file", Ui.CARD2, Ui.TX, new View.OnClickListener() {
            @Override public void onClick(View v) { ((MainActivity) c).pickRestoreFile(); }
        });
        tvBk = k.note(bk, "Cadangan berisi semua model dan pengaturan. Setelah dipulihkan, remot restart.");
        return view;
    }

    private final Button[] nrfBtn = new Button[5];
    private static final String[] NRF_NAMES = {"OFF", "MIN", "LOW", "HIGH", "MAX"};
    private static final int[] NRF_VALS = {4, 0, 1, 2, 3};

    private void nrfMark(int cur) {
        for (int i = 0; i < 5; i++) {
            boolean on = NRF_VALS[i] == cur;
            Ui.paint(c, nrfBtn[i], on ? (cur == 4 ? Ui.BAD : Ui.ACC) : 0x00000000, on ? 0xFF001018 : Ui.MUT);
        }
        nrfView.set(cur);
    }

    private void nrfSend(int v) {
        nrfUserAt = android.os.SystemClock.uptimeMillis();
        nrfMark(v);
        host.set("nrf", v);
    }

    private long nrfUserAt;

    /** Lima pilihan: OFF, MIN, LOW, HIGH, MAX. OFF meminta konfirmasi dulu karena RX akan masuk failsafe. */
    private void buildNrfChoice(LinearLayout parent) {
        LinearLayout seg = Ui.row(c);
        seg.setBackground(Ui.round(c, Ui.CARD2, 12, 0));
        seg.setPadding(Ui.dp(c, 3), Ui.dp(c, 3), Ui.dp(c, 3), Ui.dp(c, 3));
        for (int i = 0; i < 5; i++) {
            final int val = NRF_VALS[i];
            Button b = new Button(c);
            b.setText(NRF_NAMES[i]);
            b.setAllCaps(false);
            b.setTextSize(12);
            b.setMinHeight(0);
            b.setMinimumHeight(Ui.dp(c, 36));
            b.setMinWidth(0);
            b.setMinimumWidth(0);
            b.setPadding(Ui.dp(c, 2), 0, Ui.dp(c, 2), 0);
            b.setOnClickListener(new View.OnClickListener() {
                @Override public void onClick(View v) {
                    if (val != 4) { nrfSend(val); return; }
                    new AlertDialog.Builder(c, android.R.style.Theme_Material_Dialog_Alert)
                            .setMessage("Matikan pancaran NRF? Remot berhenti mengirim ke RX dan RX masuk failsafe. Jangan dilakukan saat model sedang terbang. Pengaturan ini disimpan, jadi setelah remot restart pancaran tetap mati sampai Anda menyalakannya lagi.")
                            .setNegativeButton("Batal", null)
                            .setPositiveButton("Matikan", new DialogInterface.OnClickListener() {
                                @Override public void onClick(DialogInterface dlg, int w) { nrfSend(4); }
                            }).show();
                }
            });
            nrfBtn[i] = b;
            seg.addView(b, new LinearLayout.LayoutParams(0, -2, 1f));
        }
        parent.addView(seg, Ui.lpm(c, -1, -2, 0, 6, 0, 6));
        nrfMark(3);
        ctls.add(new Controls.Ctl() {
            @Override void bind(JSONObject d) {
                if (android.os.SystemClock.uptimeMillis() - nrfUserAt < 1500) return;
                nrfMark(d.optInt("nrf", 3));
            }
        });
    }

    private CheckBox thrCheck(LinearLayout parent, String label) {
        CheckBox cb = new CheckBox(c);
        cb.setText(label);
        cb.setTextColor(Ui.TX);
        cb.setTextSize(14);
        cb.setButtonTintList(Ui.tint(Ui.ACC, 0xFF8793A1));
        parent.addView(cb, Ui.lpm(c, -1, -2, 0, 2, 0, 2));
        return cb;
    }

    /** Dua pilihan saling meniadakan. Dua-duanya kosong berarti throttle bertahan. */
    private void thrChanged(boolean center, boolean on) {
        if (thrBusy) return;
        thrBusy = true;
        if (on) (center ? cbThrBottom : cbThrCenter).setChecked(false);
        thrBusy = false;
        int m = cbThrCenter.isChecked() ? StickView.THR_CENTER : (cbThrBottom.isChecked() ? StickView.THR_BOTTOM : StickView.THR_HOLD);
        ((MainActivity) c).setThrottleMode(m);
    }

    private static Controls.Getter maskGetter(final int bit) {
        return new Controls.Getter() {
            @Override public int get(JSONObject d) { return (d.optInt("smask", 0) >> bit) & 1; }
        };
    }

    @Override void onData(JSONObject d) {
        super.onData(d);
        JSONObject cb = d.optJSONObject("cb");
        boolean on = cb != null && cb.optInt("on") == 1;
        if (on) {
            int st = cb.optInt("st");
            tvCal.setText(st == 0 ? "Langkah 1 dari 2: gerakkan stik penuh ke semua sisi." : "Langkah 2 dari 2: lepas stik di tengah.");
            tvCal.setTextColor(Ui.WARN);
            org.json.JSONArray mn = cb.optJSONArray("mn"), mx = cb.optJSONArray("mx");
            if (mn != null && mx != null) {
                StringBuilder sb = new StringBuilder();
                for (int i = 0; i < 4; i++) {
                    sb.append(String.format(Locale.US, "S%d min %4d  maks %4d%n", i + 1, mn.optInt(i), mx.optInt(i)));
                }
                tvCalLive.setText(sb.toString().trim());
            }
        } else {
            tvCal.setText("Tidak sedang kalibrasi.");
            tvCal.setTextColor(Ui.TX);
            tvCalLive.setText("");
        }
        btnCal1.setEnabled(!on);
        btnCal2.setEnabled(on);
        btnCal3.setEnabled(on);
        btnCal2.setText(on && cb.optInt("st") == 1 ? "Simpan" : "Lanjut");
    }

    // ---------------------------------------------------------------- cadangan
    @Override void onBackup(String text) {
        if (!text.startsWith("EDGETX1:")) { tvBk.setText("Data cadangan dari remot tidak dikenali."); return; }
        tvBk.setText("Pilih tempat menyimpan file.");
        ((MainActivity) c).saveBackup(text);
    }

    /** Mulai pemulihan dari teks cadangan. Dikirim per 50 byte, tiap potongan menunggu balasan A,RS,200. */
    void startRestore(String text) {
        text = text.trim();
        // Cadangan 300 byte (6 potongan), 650 byte (13 potongan, dengan nama model), dan 800 byte (16 potongan, dengan mix bebas) diterima
        if (!text.startsWith("EDGETX1:") || (text.length() != 8 + 600 && text.length() != 8 + 1300 && text.length() != 8 + 1600)) {
            host.toast("File cadangan tidak cocok");
            return;
        }
        String hex = text.substring(8);
        if (!hex.matches("[0-9A-Fa-f]+")) { host.toast("File cadangan rusak"); return; }
        int n = hex.length() / 100;
        restoreChunks = new String[n];
        for (int i = 0; i < n; i++) restoreChunks[i] = hex.substring(i * 100, i * 100 + 100);
        restoreIdx = 0;
        tvBk.setText("Memulihkan... 0/" + n);
        host.cmd("RS,0," + restoreChunks[0]);
    }

    @Override void onAck(String name, int code) {
        if (restoreIdx < 0) return;
        if (name.equals("RS")) {
            if (code != 200) { restoreIdx = -1; tvBk.setText("Pemulihan gagal pada potongan data."); return; }
            restoreIdx++;
            if (restoreIdx < restoreChunks.length) {
                tvBk.setText("Memulihkan... " + restoreIdx + "/" + restoreChunks.length);
                host.cmd("RS," + restoreIdx + "," + restoreChunks[restoreIdx]);
            } else {
                host.cmd("RC");
            }
        } else if (name.equals("RC")) {
            restoreIdx = -1;
            tvBk.setText(code == 200 ? "Berhasil. Remot restart, hubungkan ulang aplikasi." : "Pemulihan ditolak (kode " + code + ").");
        }
    }
}
