package com.edgetx.remote;

/**
 * Format paket ke remot EDGE TX lewat BLE (Nordic UART, karakteristik RX).
 * Biner 18 byte: 0xA5, 8 x uint16 little endian (CH1..CH8), lalu 1 byte XOR dari byte ke-1 sampai ke-16.
 * Kelas ini murni Java (tanpa API Android) supaya bisa diuji di komputer.
 */
final class Frame {
    static final int CH_COUNT = 8;
    static final int MAX = 4095;
    static final int MID = 2048;

    private Frame() {}

    static int clamp(int v) {
        return v < 0 ? 0 : (v > MAX ? MAX : v);
    }

    /** Nilai 0.0..1.0 menjadi 0..4095. */
    static int fromUnit(float u) {
        if (u < 0f) u = 0f;
        if (u > 1f) u = 1f;
        return Math.round(u * MAX);
    }

    static byte[] build(int[] ch) {
        byte[] b = new byte[18];
        b[0] = (byte) 0xA5;
        int x = 0;
        for (int i = 0; i < CH_COUNT; i++) {
            int v = clamp(ch[i]);
            int lo = v & 0xFF;
            int hi = (v >> 8) & 0xFF;
            b[1 + 2 * i] = (byte) lo;
            b[2 + 2 * i] = (byte) hi;
            x ^= lo ^ hi;
        }
        b[17] = (byte) x;
        return b;
    }

    /** Status dari remot, contoh baris: "V7.42,H1,L5,K1". */
    static final class Status {
        float volt;
        int hp;     // 1 = remot memakai input HP
        int link;   // 0..5 bar link NRF24
        int ok;     // 1 = data HP diterima, 0 = failsafe
    }

    static Status parseStatus(String line) {
        if (line == null) return null;
        Status s = new Status();
        boolean any = false;
        for (String t : line.trim().split(",")) {
            t = t.trim();
            if (t.length() < 2) continue;
            String val = t.substring(1);
            try {
                switch (t.charAt(0)) {
                    case 'V': s.volt = Float.parseFloat(val); any = true; break;
                    case 'H': s.hp = Integer.parseInt(val); any = true; break;
                    case 'L': s.link = Integer.parseInt(val); any = true; break;
                    case 'K': s.ok = Integer.parseInt(val); any = true; break;
                    default: break;
                }
            } catch (NumberFormatException e) {
                return null;
            }
        }
        return any ? s : null;
    }
}
