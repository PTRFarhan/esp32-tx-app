package com.edgetx.remote;

import android.content.Context;

import org.json.JSONObject;

/** Layanan dari MainActivity untuk halaman-halaman. */
interface Host {
    Context ctx();
    boolean ready();
    /** Kirim pengaturan "S,kunci,nilai" ke remot. */
    void set(String key, int value);
    /** Kirim satu baris perintah teks. */
    void cmd(String line);
    /** Bangun ulang seluruh tampilan (dipakai setelah ganti tema aplikasi). */
    void recreateUi();
    void toast(String msg);
    /** JSON status terakhir dari remot, atau null. */
    JSONObject data();
}
