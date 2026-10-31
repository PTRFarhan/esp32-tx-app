package com.edgetx.remote;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.DashPathEffect;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.view.View;

/** Grafik pengaturan: kurva channel (rate, expo, endpoint, trim, reverse) dan kekuatan pancar NRF. */
final class Graphs {
    private Graphs() {}

    private static int clamp4095(int v) { return v < 0 ? 0 : (v > 4095 ? 4095 : v); }

    /**
     * Keluaran satu channel untuk masukan stik v (0..4095). Rumusnya sama dengan applyChannels di firmware:
     * rate dan expo, reverse, trim, lalu endpoint (bawah dan atas berbeda).
     */
    static int curve(int v, int dr, int ex, boolean rev, int trim, int lo, int hi) {
        int x = v - 2048;
        x = x * dr / 100;
        if (ex > 0) {
            int cube = (x * x / 2048) * x / 2048;
            x = (x * (100 - ex) + cube * ex) / 100;
        }
        int val = 2048 + x;
        if (rev) val = 4095 - val;
        val = clamp4095(val + trim * 10);
        int d = val - 2048;
        d = d * (d >= 0 ? hi : lo) / 100;
        return clamp4095(2048 + d);
    }

    // ------------------------------------------------------------ kurva channel
    static class CurveView extends View {
        private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Path path = new Path();
        private final RectF r = new RectF();
        private int dr = 100, ex = 0, lo = 100, hi = 100, trim = 0;
        private boolean rev;
        private int live = -1;

        CurveView(Context c) { super(c); }

        void setCurve(int dr, int ex, int lo, int hi, int trim, boolean rev) {
            this.dr = dr; this.ex = ex; this.lo = lo; this.hi = hi; this.trim = trim; this.rev = rev;
            invalidate();
        }

        /** Nilai keluaran channel sekarang (0..4095), atau -1 kalau tidak ada. */
        void setLive(int ch) {
            if (ch != live) { live = ch; invalidate(); }
        }

        @Override protected void onMeasure(int wSpec, int hSpec) {
            int w = MeasureSpec.getSize(wSpec);
            setMeasuredDimension(w, Math.round(w * 0.62f));
        }

        @Override protected void onDraw(Canvas cv) {
            float w = getWidth(), h = getHeight();
            float d = getResources().getDisplayMetrics().density;
            float padL = 30 * d, padR = 10 * d, padT = 10 * d, padB = 20 * d;
            float gx = padL, gy = padT, gw = w - padL - padR, gh = h - padT - padB;

            p.setStyle(Paint.Style.FILL);
            p.setColor(Ui.CARD2);
            r.set(0, 0, w, h);
            cv.drawRoundRect(r, 12 * d, 12 * d, p);

            // garis bantu
            p.setStyle(Paint.Style.STROKE);
            p.setStrokeWidth(1f * d);
            for (int i = 0; i <= 4; i++) {
                p.setColor(i == 2 ? 0xFF3A4C63 : Ui.STROKE);
                float x = gx + gw * i / 4f, y = gy + gh * i / 4f;
                cv.drawLine(x, gy, x, gy + gh, p);
                cv.drawLine(gx, y, gx + gw, y, p);
            }
            // acuan lurus 100% (putus-putus)
            p.setColor(Ui.alpha(Ui.MUT, 150));
            p.setPathEffect(new DashPathEffect(new float[]{5 * d, 5 * d}, 0));
            cv.drawLine(gx, gy + gh, gx + gw, gy, p);
            p.setPathEffect(null);

            // kurva
            path.reset();
            int steps = Math.max(2, (int) (gw / (1.5f * d)));
            for (int i = 0; i <= steps; i++) {
                int v = Math.round(4095f * i / steps);
                int o = curve(v, dr, ex, rev, trim, lo, hi);
                float x = gx + gw * i / steps;
                float y = gy + gh - gh * o / 4095f;
                if (i == 0) path.moveTo(x, y); else path.lineTo(x, y);
            }
            p.setColor(Ui.ACC);
            p.setStrokeWidth(3f * d);
            p.setStrokeCap(Paint.Cap.ROUND);
            p.setStrokeJoin(Paint.Join.ROUND);
            cv.drawPath(path, p);

            // posisi sekarang: titik pada kurva yang paling dekat dengan keluaran channel
            if (live >= 0) {
                int best = 2048, bd = Integer.MAX_VALUE;
                for (int v = 0; v <= 4095; v += 8) {
                    int dd = Math.abs(curve(v, dr, ex, rev, trim, lo, hi) - live);
                    if (dd < bd) { bd = dd; best = v; }
                }
                int o = curve(best, dr, ex, rev, trim, lo, hi);
                float x = gx + gw * best / 4095f, y = gy + gh - gh * o / 4095f;
                p.setStyle(Paint.Style.FILL);
                p.setColor(0xFF001018);
                cv.drawCircle(x, y, 7 * d, p);
                p.setColor(Ui.OK);
                cv.drawCircle(x, y, 5 * d, p);
            }

            // label sumbu
            p.setStyle(Paint.Style.FILL);
            p.setTypeface(Typeface.DEFAULT);
            p.setTextSize(10 * d);
            p.setColor(Ui.MUT);
            p.setTextAlign(Paint.Align.CENTER);
            cv.drawText("-100", gx, h - 5 * d, p);
            cv.drawText("0", gx + gw / 2f, h - 5 * d, p);
            cv.drawText("+100", gx + gw, h - 5 * d, p);
            p.setTextAlign(Paint.Align.RIGHT);
            cv.drawText("+100", gx - 4 * d, gy + 8 * d, p);
            cv.drawText("0", gx - 4 * d, gy + gh / 2f + 4 * d, p);
            cv.drawText("-100", gx - 4 * d, gy + gh, p);
        }
    }

    // ------------------------------------------------------------ daya pancar NRF
    /** Empat batang naik. Batang sampai level terpilih menyala, di bawahnya perkiraan jangkauan relatif. */
    static class PowerView extends View {
        private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final RectF r = new RectF();
        private int level = 3;
        private static final String[] DB = {"-18 dBm", "-12 dBm", "-6 dBm", "0 dBm", "MATI"};
        private static final String[] RANGE = {"sekitar 12%", "sekitar 25%", "sekitar 50%", "100%"};

        PowerView(Context c) { super(c); }

        void set(int l) {
            if (l < 0) l = 0;
            if (l > 4) l = 4;
            if (l != level) { level = l; invalidate(); }
        }

        @Override protected void onMeasure(int wSpec, int hSpec) {
            int w = MeasureSpec.getSize(wSpec);
            setMeasuredDimension(w, Math.round(w * 0.34f));
        }

        @Override protected void onDraw(Canvas cv) {
            float w = getWidth(), h = getHeight();
            float d = getResources().getDisplayMetrics().density;
            p.setStyle(Paint.Style.FILL);
            p.setColor(Ui.CARD2);
            r.set(0, 0, w, h);
            cv.drawRoundRect(r, 12 * d, 12 * d, p);
            float bw = 22 * d, gap = 8 * d, x0 = 18 * d, base = h - 14 * d;
            for (int i = 0; i < 4; i++) {
                float bh = (h - 28 * d) * (0.3f + 0.7f * i / 3f);
                p.setColor(level < 4 && i <= level ? (level == 3 ? Ui.ACC : (level == 0 ? Ui.WARN : Ui.OK)) : Ui.STROKE);
                r.set(x0 + i * (bw + gap), base - bh, x0 + i * (bw + gap) + bw, base);
                cv.drawRoundRect(r, 5 * d, 5 * d, p);
            }
            float tx = x0 + 4 * (bw + gap) + 12 * d;
            p.setTextAlign(Paint.Align.LEFT);
            p.setColor(Ui.TX);
            p.setTypeface(Typeface.DEFAULT_BOLD);
            p.setTextSize(20 * d);
            cv.drawText(DB[level], tx, h / 2f - 2 * d, p);
            p.setTypeface(Typeface.DEFAULT);
            p.setTextSize(12 * d);
            p.setColor(Ui.MUT);
            cv.drawText(level == 4 ? "Tidak memancar" : "Jangkauan " + RANGE[level], tx, h / 2f + 18 * d, p);
        }
    }

    // ------------------------------------------------------------ analisis sinyal 2,4 GHz
    /** Spektrum 126 kanal NRF (0..100), kurva halus terisi gradien, penanda WiFi/Bluetooth dan kanal TX. */
    static class SpectrumView extends View {
        private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Path path = new Path();
        private final Path fillPath = new Path();
        private final RectF r = new RectF();
        private int[] lvl = new int[126];
        private int[] peak = new int[126];
        private boolean hasData;
        private int txCh = 110;

        SpectrumView(Context c) { super(c); }

        /** sp: 126 nilai 0..100, atau null kalau analisis belum aktif. */
        void set(int[] sp) {
            if (sp == null) { hasData = false; invalidate(); return; }
            hasData = true;
            for (int i = 0; i < 126 && i < sp.length; i++) {
                int v = sp[i];
                lvl[i] = v;
                peak[i] = Math.max(v, (int) (peak[i] * 0.93f));
            }
            invalidate();
        }

        @Override protected void onMeasure(int wSpec, int hSpec) {
            int w = MeasureSpec.getSize(wSpec);
            setMeasuredDimension(w, Math.round(w * 0.56f));
        }

        @Override protected void onDraw(Canvas cv) {
            float w = getWidth(), h = getHeight();
            float d = getResources().getDisplayMetrics().density;
            float padL = 6 * d, padR = 6 * d, padT = 14 * d, padB = 26 * d;
            float gx = padL, gy = padT, gw = w - padL - padR, gh = h - padT - padB;
            float yb = gy + gh;

            p.setStyle(Paint.Style.FILL);
            p.setColor(Ui.CARD2);
            r.set(0, 0, w, h);
            cv.drawRoundRect(r, 14 * d, 14 * d, p);

            p.setStyle(Paint.Style.STROKE);
            p.setStrokeWidth(1f * d);
            for (int i = 1; i <= 3; i++) {
                p.setColor(Ui.alpha(Ui.STROKE, 160));
                float y = gy + gh * i / 3f;
                cv.drawLine(gx, y, gx + gw, y, p);
            }

            if (!hasData) {
                p.setStyle(Paint.Style.FILL);
                p.setColor(Ui.MUT);
                p.setTextAlign(Paint.Align.CENTER);
                p.setTextSize(13 * d);
                cv.drawText("Menunggu data analisis...", w / 2f, h / 2f, p);
                return;
            }

            // kurva halus (rata-rata 3 kanal) terisi gradien dari warna aksen
            path.reset();
            fillPath.reset();
            boolean first = true;
            for (int c = 0; c < 126; c++) {
                int l0 = lvl[c > 0 ? c - 1 : 0], l1 = lvl[c], l2 = lvl[c < 125 ? c + 1 : 125];
                int v = (l0 + 2 * l1 + l2) / 4;
                float x = gx + gw * c / 125f;
                float y = yb - gh * v / 100f;
                if (first) { path.moveTo(x, y); fillPath.moveTo(x, yb); fillPath.lineTo(x, y); first = false; }
                else { path.lineTo(x, y); fillPath.lineTo(x, y); }
            }
            fillPath.lineTo(gx + gw, yb);
            fillPath.close();

            p.setStyle(Paint.Style.FILL);
            p.setShader(new LinearGradient(0, gy, 0, yb, Ui.alpha(Ui.ACC, 190), Ui.alpha(Ui.ACC, 10), Shader.TileMode.CLAMP));
            cv.drawPath(fillPath, p);
            p.setShader(null);

            p.setStyle(Paint.Style.STROKE);
            p.setStrokeWidth(2.4f * d);
            p.setStrokeCap(Paint.Cap.ROUND);
            p.setStrokeJoin(Paint.Join.ROUND);
            p.setColor(Ui.ACC);
            cv.drawPath(path, p);

            // puncak yang turun pelan: titik-titik halus
            p.setStyle(Paint.Style.FILL);
            p.setColor(Ui.alpha(Ui.TX, 150));
            for (int c = 0; c < 126; c += 2) {
                float x = gx + gw * c / 125f;
                float y = yb - gh * peak[c] / 100f;
                cv.drawCircle(x, y, 1.1f * d, p);
            }

            // kanal TX sendiri
            float txX = gx + gw * txCh / 125f;
            p.setColor(Ui.alpha(Ui.WARN, 220));
            Path tri = new Path();
            tri.moveTo(txX - 5 * d, gy - 2 * d);
            tri.lineTo(txX + 5 * d, gy - 2 * d);
            tri.lineTo(txX, gy + 6 * d);
            tri.close();
            cv.drawPath(tri, p);

            // label kanal WiFi 1/6/11 dan iklan Bluetooth
            p.setTextAlign(Paint.Align.CENTER);
            p.setTextSize(10 * d);
            p.setColor(Ui.MUT);
            int[] wifiCh = {0, 25, 50}; String[] wifiNm = {"W1", "W6", "W11"};
            for (int i = 0; i < 3; i++) cv.drawText(wifiNm[i], gx + gw * wifiCh[i] / 125f, h - 8 * d, p);
            int[] btCh = {2, 26, 80};
            p.setColor(Ui.alpha(Ui.MUT, 170));
            for (int bch : btCh) cv.drawText("B", gx + gw * bch / 125f, gy + gh + 11 * d, p);
            p.setColor(Ui.WARN);
            p.setTypeface(Typeface.DEFAULT_BOLD);
            cv.drawText("TX", txX, h - 8 * d, p);
            p.setTypeface(Typeface.DEFAULT);
        }
    }

    /** Kepadatan 13 kanal WiFi (lebar 22 MHz), dihitung dari array spektrum 126 kanal. Batang membulat dengan gradien. */
    static class WifiBarsView extends View {
        private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final RectF r = new RectF();
        private final float[] shown = new float[13];
        private int[] load = new int[13];
        private int best = 0, worst = 0;
        private boolean hasData;

        WifiBarsView(Context c) { super(c); }

        void set(int[] sp) {
            if (sp == null) { hasData = false; invalidate(); return; }
            hasData = true;
            best = 0; worst = 0;
            for (int wch = 0; wch < 13; wch++) {
                int center = 12 + 5 * wch, a = Math.max(0, center - 8), b = Math.min(125, center + 8), sum = 0, cnt = 0;
                for (int i = a; i <= b; i++) { sum += sp[i]; cnt++; }
                load[wch] = cnt > 0 ? sum / cnt : 0;
                if (load[wch] < load[best]) best = wch;
                if (load[wch] > load[worst]) worst = wch;
            }
            invalidate();
        }

        @Override protected void onMeasure(int wSpec, int hSpec) {
            int w = MeasureSpec.getSize(wSpec);
            setMeasuredDimension(w, Math.round(w * 0.42f));
        }

        @Override protected void onDraw(Canvas cv) {
            float w = getWidth(), h = getHeight();
            float d = getResources().getDisplayMetrics().density;
            p.setStyle(Paint.Style.FILL);
            p.setColor(Ui.CARD2);
            r.set(0, 0, w, h);
            cv.drawRoundRect(r, 14 * d, 14 * d, p);
            if (!hasData) {
                p.setColor(Ui.MUT);
                p.setTextAlign(Paint.Align.CENTER);
                p.setTextSize(13 * d);
                cv.drawText("Menunggu data analisis...", w / 2f, h / 2f, p);
                return;
            }
            float padB = 20 * d, padT = 10 * d, gap = 4 * d;
            float bw = (w - gap * 14) / 13f;
            float base = h - padB, top = padT;
            for (int i = 0; i < 13; i++) {
                float t = (load[i] / 100f) * (base - top);
                shown[i] += (t - shown[i]) * 0.35f;
                float x = gap + i * (bw + gap);
                boolean isBest = i == best, isWorst = i == worst && load[worst] > 0;
                int col = isBest ? Ui.OK : (isWorst ? Ui.BAD : Ui.ACC);
                p.setColor(Ui.alpha(col, isBest ? 255 : 210));
                float bh = Math.max(shown[i], 2 * d);
                r.set(x, base - bh, x + bw, base);
                cv.drawRoundRect(r, bw / 2.2f, bw / 2.2f, p);
            }
            p.setTextAlign(Paint.Align.CENTER);
            p.setTextSize(9.5f * d);
            p.setColor(Ui.MUT);
            int[] mark = {0, 5, 10, 12}; String[] markNm = {"1", "6", "11", "13"};
            for (int i = 0; i < 4; i++) cv.drawText(markNm[i], gap + mark[i] * (bw + gap) + bw / 2f, h - 5 * d, p);
        }

        int bestChannel() { return best + 1; }
        int worstChannel() { return worst + 1; }
    }
}
