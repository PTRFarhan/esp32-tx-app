package com.edgetx.remote;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.MotionEvent;
import android.view.View;

/**
 * Joystick virtual berbentuk kotak, seperti gimbal remot.
 * Sumbu X selalu kembali ke tengah saat dilepas.
 * Sumbu Y kembali ke tengah juga, kecuali holdY = true (throttle): nilainya bertahan di tempat
 * dan digeser relatif dari titik sentuh, jadi menyentuh pad tidak membuat throttle melompat.
 * Throttle (holdY) punya tiga mode saat jari dilepas: bertahan (bawaan), kembali ke tengah, atau kembali ke bawah.
 * Nilai x dan y 0.0..1.0, y = 1.0 artinya paling atas.
 */
public class StickView extends View {
    public static final int THR_HOLD = 0, THR_CENTER = 1, THR_BOTTOM = 2;
    private final boolean holdY;
    private int thrMode = THR_HOLD;
    private final String label;
    private float vx = 0.5f;
    private float vy;
    private int activeId = -1;
    private float downY;
    private float vyAtDown;
    private float padLeft, padTop, side;

    private final Paint pBg = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint pLine = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint pKnob = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint pRing = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint pText = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final RectF rect = new RectF();

    public StickView(Context c, boolean holdY, String label) {
        super(c);
        this.holdY = holdY;
        this.label = label;
        this.vy = holdY ? 0f : 0.5f;
        pBg.setColor(Ui.CARD2);
        pLine.setColor(Ui.STROKE);
        pLine.setStyle(Paint.Style.STROKE);
        pLine.setStrokeWidth(3f);
        pKnob.setColor(Ui.ACC);
        pRing.setColor(Ui.alpha(Ui.ACC, 0x55));
        pText.setColor(Ui.MUT);
    }

    /** Perilaku throttle saat dilepas. Berlaku hanya untuk pad throttle (holdY). */
    public void setThrottleMode(int mode) {
        if (!holdY) return;
        thrMode = mode;
        if (activeId == -1) {
            vy = (mode == THR_CENTER) ? 0.5f : 0f;     // bertahan dan bawah mulai dari 0 supaya aman
            invalidate();
        }
    }

    /** Posisi Y saat tidak disentuh. */
    private float restY() {
        if (!holdY) return 0.5f;
        if (thrMode == THR_CENTER) return 0.5f;
        if (thrMode == THR_BOTTOM) return 0f;
        return vy;      // bertahan
    }

    public float getVx() { return vx; }
    public float getVy() { return vy; }

    /** Kembali ke posisi aman: X tengah, throttle 0 atau Y tengah. */
    public void reset() {
        activeId = -1;
        vx = 0.5f;
        vy = holdY ? (thrMode == THR_CENTER ? 0.5f : 0f) : 0.5f;
        invalidate();
    }

    /** Throttle ke nol tanpa mengubah X. */
    public void zeroY() {
        if (holdY) {
            vy = 0f;
            invalidate();
        }
    }

    @Override
    protected void onSizeChanged(int w, int h, int ow, int oh) {
        float pad = 8f * getResources().getDisplayMetrics().density;
        side = Math.min(w, h) - 2f * pad;
        if (side < 10f) side = 10f;
        padLeft = (w - side) / 2f;
        padTop = (h - side) / 2f;
        pText.setTextSize(Math.max(18f, side * 0.07f));
        pLine.setStrokeWidth(Math.max(2f, side * 0.008f));
    }

    @Override
    protected void onDraw(Canvas c) {
        rect.set(padLeft, padTop, padLeft + side, padTop + side);
        float r = side * 0.06f;
        c.drawRoundRect(rect, r, r, pBg);
        c.drawRoundRect(rect, r, r, pLine);
        float mx = padLeft + side / 2f;
        float my = padTop + side / 2f;
        c.drawLine(mx, padTop, mx, padTop + side, pLine);
        c.drawLine(padLeft, my, padLeft + side, my, pLine);
        c.drawText(label, padLeft + side * 0.04f, padTop + side * 0.10f, pText);

        float cx = padLeft + vx * side;
        float cy = padTop + (1f - vy) * side;
        float kr = side * 0.085f;
        if (activeId != -1) c.drawCircle(cx, cy, kr * 1.7f, pRing);
        c.drawCircle(cx, cy, kr, pKnob);
    }

    private static float clamp01(float v) {
        return v < 0f ? 0f : (v > 1f ? 1f : v);
    }

    private void setFromTouch(float px, float py) {
        vx = clamp01((px - padLeft) / side);
        if (holdY) {
            vy = clamp01(vyAtDown + (downY - py) / side);
        } else {
            vy = clamp01(1f - (py - padTop) / side);
        }
        invalidate();
    }

    private void release() {
        activeId = -1;
        vx = 0.5f;
        vy = restY();
        invalidate();
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                if (activeId != -1) return true;     // sudah ada jari yang mengendalikan pad ini
                int idx = e.getActionIndex();
                activeId = e.getPointerId(idx);
                downY = e.getY(idx);
                vyAtDown = vy;
                setFromTouch(e.getX(idx), e.getY(idx));
                if (getParent() != null) getParent().requestDisallowInterceptTouchEvent(true);
                return true;
            }
            case MotionEvent.ACTION_MOVE: {
                if (activeId == -1) return true;
                int idx = e.findPointerIndex(activeId);
                if (idx >= 0) setFromTouch(e.getX(idx), e.getY(idx));
                return true;
            }
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_UP: {
                if (e.getPointerId(e.getActionIndex()) == activeId) release();
                return true;
            }
            case MotionEvent.ACTION_CANCEL:
                release();
                return true;
            default:
                return true;
        }
    }
}
