package com.edgetx.remote;

import android.content.Context;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.ScrollView;

import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;

/** Dasar satu halaman (tab). */
abstract class Page {
    final Host host;
    final Context c;
    final List<Controls.Ctl> ctls = new ArrayList<>();
    final Controls k;
    View view;

    Page(Host host) {
        this.host = host;
        this.c = host.ctx();
        this.k = new Controls(host, ctls);
    }

    abstract View build();

    void onData(JSONObject d) {
        for (Controls.Ctl x : ctls) x.bind(d);
    }

    void onShow() { }
    void onHide() { }
    void onAck(String name, int code) { }
    void onBackup(String text) { }
    void onLink(boolean ready) { }
    void onConfig(boolean landscape) { }

    /** Halaman bergulir berisi kolom kartu. */
    LinearLayout scrollColumn() {
        ScrollView sv = new ScrollView(c);
        sv.setFillViewport(false);
        sv.setVerticalScrollBarEnabled(false);
        LinearLayout col = new LinearLayout(c);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setPadding(Ui.dp(c, 14), Ui.dp(c, 6), Ui.dp(c, 14), Ui.dp(c, 14));
        sv.addView(col, new ScrollView.LayoutParams(-1, -2));
        view = sv;
        return col;
    }
}
