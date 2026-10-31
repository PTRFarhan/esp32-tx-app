package com.edgetx.remote;

import android.content.Context;
import android.content.SharedPreferences;
import android.content.res.ColorStateList;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

/** Warna, pembuat komponen, dan tampilan gambar sendiri (bar, gauge, sinyal). */
final class Ui {
    // Warna tema aktif. Diisi oleh applyTheme() sebelum tampilan dibangun (lihat loadTheme di onCreate).
    static int BG, CARD, CARD2, STROKE, TX, MUT, ACC, OK, WARN, BAD;

    /** Satu set warna tema. OK, WARN, BAD dibuat tetap mudah dibedakan di semua tema (hijau, kuning, merah). */
    static final class Theme {
        final String name;
        final int bg, card, card2, stroke, tx, mut, acc, ok, warn, bad;
        Theme(String name, int bg, int card, int card2, int stroke, int tx, int mut, int acc, int ok, int warn, int bad) {
            this.name = name; this.bg = bg; this.card = card; this.card2 = card2; this.stroke = stroke;
            this.tx = tx; this.mut = mut; this.acc = acc; this.ok = ok; this.warn = warn; this.bad = bad;
        }
    }

    static final Theme[] THEMES = {
        new Theme("Midnight Biru",  0xFF090D13, 0xFF111821, 0xFF182230, 0xFF223044, 0xFFE6EDF3, 0xFF7D8B99, 0xFF3FB6FF, 0xFF3DDC84, 0xFFFFB020, 0xFFFF4D5E),
        new Theme("Karbon Merah",   0xFF0D0D0F, 0xFF18181C, 0xFF222227, 0xFF34343B, 0xFFF2EEE9, 0xFF8C8A89, 0xFFFF3B3B, 0xFF3DDC84, 0xFFFFB020, 0xFFFF7A3D),
        new Theme("Hijau Militer",  0xFF0B0F0A, 0xFF141C11, 0xFF1E2A19, 0xFF324428, 0xFFE7EFD8, 0xFF8A9A7B, 0xFF8BC53F, 0xFF3DDC84, 0xFFE0A930, 0xFFE0543A),
        new Theme("Ungu Neon",      0xFF0D0820, 0xFF18102E, 0xFF241640, 0xFF3B2460, 0xFFF0E9FF, 0xFF9988C2, 0xFFBE3CFF, 0xFF2EE6A6, 0xFFFFC14D, 0xFFFF4D8D),
        new Theme("Putih Bersih",   0xFFF4F6F8, 0xFFFFFFFF, 0xFFEFF2F5, 0xFFD7DEE4, 0xFF14202B, 0xFF6B7885, 0xFF0A84FF, 0xFF1FAE63, 0xFFE08900, 0xFFE0324B),
    };
    static int themeIndex = 0;
    private static final String PREF = "ui_theme";
    private static final String KEY = "idx";

    /** Dipanggil sekali di awal onCreate, sebelum buildUi(), supaya semua warna sudah benar saat tampilan dibuat. */
    static void loadTheme(Context c) {
        int idx = c.getSharedPreferences(PREF, Context.MODE_PRIVATE).getInt(KEY, 0);
        applyTheme(idx);
    }

    static void saveTheme(Context c, int idx) {
        c.getSharedPreferences(PREF, Context.MODE_PRIVATE).edit().putInt(KEY, idx).apply();
        applyTheme(idx);
    }

    static void applyTheme(int idx) {
        if (idx < 0 || idx >= THEMES.length) idx = 0;
        themeIndex = idx;
        Theme t = THEMES[idx];
        BG = t.bg; CARD = t.card; CARD2 = t.card2; STROKE = t.stroke; TX = t.tx; MUT = t.mut;
        ACC = t.acc; OK = t.ok; WARN = t.warn; BAD = t.bad;
    }

    /** Teks yang kontras di atas warna aksen (ACC/OK/WARN/BAD), beda untuk tema terang dan gelap. */
    static int onAccent() { return themeIndex == 4 ? 0xFFFFFFFF : 0xFF001018; }

    private Ui() {}

    static int dp(Context c, int v) {
        return Math.round(v * c.getResources().getDisplayMetrics().density);
    }

    static TextView text(Context c, String s, int sp, int color, boolean bold) {
        TextView t = new TextView(c);
        t.setText(s);
        t.setTextSize(sp);
        t.setTextColor(color);
        if (bold) t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }

    static GradientDrawable round(Context c, int fill, int radiusDp, int stroke) {
        GradientDrawable g = new GradientDrawable();
        g.setColor(fill);
        g.setCornerRadius(dp(c, radiusDp));
        if (stroke != 0) g.setStroke(dp(c, 1), stroke);
        return g;
    }

    static LinearLayout.LayoutParams lp(int w, int h) {
        return new LinearLayout.LayoutParams(w, h);
    }

    static LinearLayout.LayoutParams lpm(Context c, int w, int h, int l, int t, int r, int b) {
        LinearLayout.LayoutParams p = new LinearLayout.LayoutParams(w, h);
        p.setMargins(dp(c, l), dp(c, t), dp(c, r), dp(c, b));
        return p;
    }

    /** Kartu bertepi bulat dengan judul. Isi ditambahkan ke LinearLayout yang dikembalikan. */
    static LinearLayout card(Context c, LinearLayout parent, String title) {
        LinearLayout box = new LinearLayout(c);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setBackground(round(c, CARD, 16, STROKE));
        box.setPadding(dp(c, 14), dp(c, 12), dp(c, 14), dp(c, 12));
        if (title != null) {
            TextView t = text(c, title.toUpperCase(), 12, ACC, true);
            t.setLetterSpacing(0.12f);
            box.addView(t, lpm(c, -1, -2, 0, 0, 0, 6));
        }
        parent.addView(box, lpm(c, -1, -2, 0, 0, 0, 12));
        return box;
    }

    static Button button(Context c, String label, int bg, int fg) {
        Button b = new Button(c);
        b.setText(label);
        b.setTextSize(13);
        b.setAllCaps(false);
        b.setPadding(dp(c, 8), 0, dp(c, 8), 0);
        paint(c, b, bg, fg);
        return b;
    }

    static void paint(Context c, Button b, int bg, int fg) {
        b.setBackground(round(c, bg, 12, 0));
        b.setTextColor(fg);
    }

    static ColorStateList tint(int on, int off) {
        return new ColorStateList(
                new int[][]{new int[]{android.R.attr.state_checked}, new int[]{}},
                new int[]{on, off});
    }

    // ------------------------------------------------------------ bar nilai
    /** Bar horizontal. Mode tengah: terisi dari titik tengah (untuk nilai channel 0..4095 dengan 2048 di tengah). */
    static class BarView extends View {
        private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final RectF r = new RectF();
        private float frac;
        private boolean centered;
        private int color = ACC;

        BarView(Context c, boolean centered) {
            super(c);
            this.centered = centered;
        }

        void set(float f, int col) {
            if (f < 0f) f = 0f;
            if (f > 1f) f = 1f;
            if (f != frac || col != color) {
                frac = f;
                color = col;
                invalidate();
            }
        }

        @Override protected void onDraw(Canvas cv) {
            float w = getWidth(), h = getHeight();
            float rad = h / 2f;
            p.setColor(CARD2);
            r.set(0, 0, w, h);
            cv.drawRoundRect(r, rad, rad, p);
            p.setColor(color);
            if (centered) {
                float mid = w / 2f, x = w * frac;
                r.set(Math.min(mid, x), 0, Math.max(mid, x), h);
                cv.drawRoundRect(r, rad, rad, p);
                p.setColor(0x66FFFFFF);
                cv.drawRect(mid - 1, 0, mid + 1, h, p);
            } else {
                r.set(0, 0, Math.max(h, w * frac), h);
                cv.drawRoundRect(r, rad, rad, p);
            }
        }
    }

    // ------------------------------------------------------------ gauge baterai
    static class GaugeView extends View {
        private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final RectF r = new RectF();
        private float pct;
        private String big = "--";
        private String small = "";

        GaugeView(Context c) {
            super(c);
            p.setStyle(Paint.Style.STROKE);
            p.setStrokeCap(Paint.Cap.ROUND);
        }

        void set(float pct, String big, String small) {
            this.pct = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
            this.big = big;
            this.small = small;
            invalidate();
        }

        @Override protected void onDraw(Canvas cv) {
            float w = getWidth(), h = getHeight();
            float s = Math.min(w, h);
            float sw = s * 0.09f;
            p.setStrokeWidth(sw);
            r.set((w - s) / 2f + sw, (h - s) / 2f + sw, (w + s) / 2f - sw, (h + s) / 2f - sw);
            p.setColor(CARD2);
            cv.drawArc(r, 135, 270, false, p);
            int col = pct < 20 ? BAD : (pct < 45 ? WARN : OK);
            p.setColor(col);
            cv.drawArc(r, 135, 270f * pct / 100f, false, p);
            p.setStyle(Paint.Style.FILL);
            p.setTextAlign(Paint.Align.CENTER);
            p.setColor(TX);
            p.setTypeface(Typeface.DEFAULT_BOLD);
            p.setTextSize(s * 0.22f);
            cv.drawText(big, w / 2f, h / 2f + s * 0.05f, p);
            p.setTypeface(Typeface.DEFAULT);
            p.setColor(MUT);
            p.setTextSize(s * 0.105f);
            cv.drawText(small, w / 2f, h / 2f + s * 0.22f, p);
            p.setStyle(Paint.Style.STROKE);
        }
    }

    // ------------------------------------------------------------ bar sinyal
    static class SignalView extends View {
        private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final RectF r = new RectF();
        private int bars;

        SignalView(Context c) { super(c); }

        void set(int b) {
            if (b != bars) { bars = b; invalidate(); }
        }

        @Override protected void onDraw(Canvas cv) {
            float w = getWidth(), h = getHeight();
            float gap = w * 0.06f;
            float bw = (w - gap * 4) / 5f;
            for (int i = 0; i < 5; i++) {
                float bh = h * (0.28f + 0.72f * i / 4f);
                p.setColor(i < bars ? (bars <= 1 ? BAD : (bars <= 2 ? WARN : OK)) : CARD2);
                r.set(i * (bw + gap), h - bh, i * (bw + gap) + bw, h);
                cv.drawRoundRect(r, bw / 3f, bw / 3f, p);
            }
        }
    }

    /** Titik status kecil bulat. */
    static View dot(Context c, int color) {
        View v = new View(c);
        v.setBackground(round(c, color, 8, 0));
        return v;
    }

    static LinearLayout row(Context c) {
        LinearLayout r = new LinearLayout(c);
        r.setOrientation(LinearLayout.HORIZONTAL);
        r.setGravity(Gravity.CENTER_VERTICAL);
        return r;
    }

    static int alpha(int color, int a) {
        return Color.argb(a, Color.red(color), Color.green(color), Color.blue(color));
    }
}
