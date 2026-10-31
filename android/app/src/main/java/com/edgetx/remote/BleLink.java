package com.edgetx.remote;

import android.annotation.SuppressLint;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.BluetoothStatusCodes;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanFilter;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.Context;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelUuid;

import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

/**
 * Koneksi BLE ke remot ESP32 TX (Nordic UART Service).
 * Semua callback ke Listener dipanggil di main thread.
 */
@SuppressLint("MissingPermission")
public class BleLink {
    public static final UUID NUS = UUID.fromString("6e400001-b5a3-f393-e0a9-e50e24dcca9e");
    public static final UUID RX_CHAR = UUID.fromString("6e400002-b5a3-f393-e0a9-e50e24dcca9e");   // HP menulis ke sini
    public static final UUID TX_CHAR = UUID.fromString("6e400003-b5a3-f393-e0a9-e50e24dcca9e");   // remot kirim status (notify)
    private static final UUID CCCD = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");

    public static final int IDLE = 0;
    public static final int SCANNING = 1;
    public static final int CONNECTING = 2;
    public static final int READY = 3;

    public interface Listener {
        void onLinkState(int state, String message);
        void onStatusLine(String line);
        void onMtu(int mtu);
    }

    private final Context ctx;
    private final Listener listener;
    private final Handler main = new Handler(Looper.getMainLooper());

    private BluetoothAdapter adapter;
    private BluetoothLeScanner scanner;
    private BluetoothGatt gatt;
    private BluetoothGattCharacteristic rx;
    private boolean rxNoResponse;
    private boolean scanning;
    private int state = IDLE;
    private final StringBuilder rxBuf = new StringBuilder();
    private int mtu = 23;
    private boolean busy;
    private long busyAt;
    private long lastTextAt;
    private byte[] pendingFrame;
    // Pengiriman berkas besar (update firmware). Data dikirim dengan jendela: maksimal bulkWindow byte belum diakui remot.
    private volatile boolean bulkActive;
    private java.io.InputStream bulkIn;
    private long bulkSize, bulkSent;
    private volatile long bulkAcked;
    private int bulkWindow = 8192;
    private byte[] bulkBuf = new byte[0];
    private int bulkLen;
    private final java.util.ArrayDeque<byte[]> textQ = new java.util.ArrayDeque<>();
    private final Runnable discoverFallback = new Runnable() {
        @Override public void run() {
            if (gatt != null && state == CONNECTING) gatt.discoverServices();
        }
    };
    private final Runnable pump = new Runnable() {
        @Override public void run() {
            pumpOnce();
            if (state == READY) main.postDelayed(this, bulkActive ? 1 : 8);
        }
    };

    private final Runnable scanTimeout = new Runnable() {
        @Override public void run() {
            if (state == SCANNING) {
                stopScan();
                setState(IDLE, "ESP32 TX tidak ditemukan. Pastikan Bluetooth remot ON.");
            }
        }
    };

    private final Runnable connectTimeout = new Runnable() {
        @Override public void run() {
            if (state == CONNECTING) {
                closeGatt();
                setState(IDLE, "Gagal tersambung, coba lagi.");
            }
        }
    };

    public BleLink(Context ctx, Listener listener) {
        this.ctx = ctx.getApplicationContext();
        this.listener = listener;
    }

    public boolean isReady() { return state == READY; }
    public int getState() { return state; }

    private void setState(int s, String msg) {
        state = s;
        listener.onLinkState(s, msg);
    }

    /** Cari remot EDGE-TX lalu sambungkan otomatis. Izin BLUETOOTH_SCAN dan BLUETOOTH_CONNECT harus sudah diberikan. */
    public void start() {
        if (state != IDLE) return;
        BluetoothManager bm = (BluetoothManager) ctx.getSystemService(Context.BLUETOOTH_SERVICE);
        adapter = bm == null ? null : bm.getAdapter();
        if (adapter == null || !adapter.isEnabled()) {
            setState(IDLE, "Bluetooth HP mati. Nyalakan dulu.");
            return;
        }
        scanner = adapter.getBluetoothLeScanner();
        if (scanner == null) {
            setState(IDLE, "Scanner Bluetooth tidak tersedia.");
            return;
        }
        List<ScanFilter> filters = new ArrayList<>();
        filters.add(new ScanFilter.Builder().setServiceUuid(new ParcelUuid(NUS)).build());
        ScanSettings settings = new ScanSettings.Builder()
                .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
                .build();
        scanning = true;
        setState(SCANNING, "Mencari ESP32 TX...");
        scanner.startScan(filters, settings, scanCb);
        main.postDelayed(scanTimeout, 15000);
    }

    /** Batalkan pencarian atau putuskan koneksi. */
    public void stop() {
        stopScan();
        main.removeCallbacks(connectTimeout);
        closeGatt();
        if (state != IDLE) setState(IDLE, "Terputus");
    }

    private void stopScan() {
        main.removeCallbacks(scanTimeout);
        if (scanning && scanner != null) {
            try { scanner.stopScan(scanCb); } catch (Exception ignored) { }
        }
        scanning = false;
    }

    private void closeGatt() {
        BluetoothGatt g = gatt;
        gatt = null;
        rx = null;
        rxBuf.setLength(0);
        textQ.clear();
        pendingFrame = null;
        bulkActive = false;
        bulkIn = null;
        busy = false;
        mtu = 23;
        main.removeCallbacks(pump);
        main.removeCallbacks(discoverFallback);
        if (g != null) {
            try { g.disconnect(); } catch (Exception ignored) { }
            try { g.close(); } catch (Exception ignored) { }
        }
    }

    private final ScanCallback scanCb = new ScanCallback() {
        @Override public void onScanResult(int callbackType, ScanResult result) {
            if (state != SCANNING) return;
            stopScan();
            connect(result.getDevice());
        }

        @Override public void onScanFailed(int errorCode) {
            stopScan();
            setState(IDLE, "Pencarian gagal (kode " + errorCode + ")");
        }
    };

    private void connect(BluetoothDevice dev) {
        String name = dev.getName();
        setState(CONNECTING, "Menyambung ke " + (name == null ? "ESP32 TX" : name) + "...");
        main.postDelayed(connectTimeout, 12000);
        gatt = dev.connectGatt(ctx, false, gattCb, BluetoothDevice.TRANSPORT_LE);
    }

    private void becomeReady() {
        if (state != CONNECTING) return;
        main.removeCallbacks(connectTimeout);
        setState(READY, "Terhubung ke ESP32 TX");
        main.removeCallbacks(pump);
        main.post(pump);
    }

    private void failConnect(String msg) {
        main.removeCallbacks(connectTimeout);
        closeGatt();
        setState(IDLE, msg);
    }

    private final BluetoothGattCallback gattCb = new BluetoothGattCallback() {
        @Override
        public void onConnectionStateChange(final BluetoothGatt g, int status, int newState) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                g.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH);
                if (g.requestMtu(185)) {
                    main.postDelayed(discoverFallback, 2000);   // kalau onMtuChanged tidak datang
                } else {
                    g.discoverServices();
                }
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                main.post(new Runnable() {
                    @Override public void run() {
                        if (g != gatt) {              // callback dari koneksi lama
                            try { g.close(); } catch (Exception ignored) { }
                            return;
                        }
                        boolean wasReady = state == READY;
                        main.removeCallbacks(connectTimeout);
                        closeGatt();
                        setState(IDLE, wasReady ? "Koneksi ke remot terputus" : "Gagal tersambung, coba lagi.");
                    }
                });
            }
        }

        @Override
        public void onMtuChanged(final BluetoothGatt g, final int newMtu, final int status) {
            main.post(new Runnable() {
                @Override public void run() {
                    if (g != gatt) return;
                    if (status == BluetoothGatt.GATT_SUCCESS) {
                        mtu = newMtu;
                        listener.onMtu(newMtu);
                    }
                    main.removeCallbacks(discoverFallback);
                    g.discoverServices();
                }
            });
        }

        @Override
        public void onCharacteristicWrite(BluetoothGatt g, BluetoothGattCharacteristic c, int status) {
            busy = false;
            if (bulkActive) {
                main.post(new Runnable() {
                    @Override public void run() {
                        if (bulkActive && state == READY) { main.removeCallbacks(pump); main.post(pump); }
                    }
                });
            }
        }

        @Override
        public void onServicesDiscovered(final BluetoothGatt g, final int status) {
            main.post(new Runnable() {
                @Override public void run() {
                    if (g != gatt || state != CONNECTING) return;
                    BluetoothGattService svc = g.getService(NUS);
                    if (status != BluetoothGatt.GATT_SUCCESS || svc == null) {
                        failConnect("Layanan UART remot tidak ditemukan.");
                        return;
                    }
                    rx = svc.getCharacteristic(RX_CHAR);
                    BluetoothGattCharacteristic tx = svc.getCharacteristic(TX_CHAR);
                    if (rx == null) {
                        failConnect("Karakteristik RX remot tidak ditemukan.");
                        return;
                    }
                    rxNoResponse = (rx.getProperties() & BluetoothGattCharacteristic.PROPERTY_WRITE_NO_RESPONSE) != 0;
                    BluetoothGattDescriptor cccd = tx == null ? null : tx.getDescriptor(CCCD);
                    if (tx != null && cccd != null) {
                        g.setCharacteristicNotification(tx, true);
                        writeDescriptor(g, cccd, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
                        // lanjut ke becomeReady() di onDescriptorWrite
                    } else {
                        becomeReady();
                    }
                }
            });
        }

        @Override
        public void onDescriptorWrite(final BluetoothGatt g, BluetoothGattDescriptor d, int status) {
            main.post(new Runnable() {
                @Override public void run() {
                    if (g == gatt) becomeReady();
                }
            });
        }

        // Android 13 ke atas memakai varian ber-parameter value
        @Override
        public void onCharacteristicChanged(BluetoothGatt g, BluetoothGattCharacteristic c, byte[] value) {
            handleRx(value);
        }

        // Android 12 memakai varian lama
        @Override
        @SuppressWarnings("deprecation")
        public void onCharacteristicChanged(BluetoothGatt g, BluetoothGattCharacteristic c) {
            if (Build.VERSION.SDK_INT < 33) handleRx(c.getValue());
        }
    };

    @SuppressWarnings("deprecation")
    private static void writeDescriptor(BluetoothGatt g, BluetoothGattDescriptor d, byte[] value) {
        if (Build.VERSION.SDK_INT >= 33) {
            g.writeDescriptor(d, value);
        } else {
            d.setValue(value);
            g.writeDescriptor(d);
        }
    }

    private void handleRx(byte[] value) {
        if (value == null) return;
        final byte[] copy = value.clone();
        main.post(new Runnable() {
            @Override public void run() {
                for (byte b : copy) {
                    char c = (char) (b & 0xFF);
                    if (c == '\n') {
                        String line = rxBuf.toString().trim();
                        rxBuf.setLength(0);
                        if (!line.isEmpty()) listener.onStatusLine(line);
                    } else if (c != '\r' && rxBuf.length() < 6000) {
                        rxBuf.append(c);
                    }
                }
            }
        });
    }

    /** Paket kendali. Hanya paket terbaru yang disimpan, yang lama dibuang kalau radio masih sibuk. */
    public void sendFrame(byte[] data) {
        if (state == READY) pendingFrame = data;
    }

    /** Perintah teks (tanpa \\n, ditambahkan di sini). Dikirim berurutan dan tidak pernah dibuang. */
    public void sendLine(String line) {
        if (state != READY || bulkActive) return;     // selama kirim berkas, remot menganggap semua byte adalah isi berkas
        byte[] all = (line + "\n").getBytes(java.nio.charset.StandardCharsets.US_ASCII);
        int chunk = Math.max(20, mtu - 3);
        for (int i = 0; i < all.length; i += chunk) {
            int n = Math.min(chunk, all.length - i);
            byte[] part = new byte[n];
            System.arraycopy(all, i, part, 0, n);
            textQ.add(part);
        }
    }

    private void pumpOnce() {
        if (state != READY || gatt == null || rx == null) return;
        long now = android.os.SystemClock.uptimeMillis();
        if (busy && now - busyAt > 250) busy = false;     // callback tidak datang
        if (busy) return;
        if (bulkActive) { pumpBulk(); return; }
        if (!textQ.isEmpty() && now - lastTextAt >= 45) {
            if (write(textQ.peek(), BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)) {
                textQ.poll();
                lastTextAt = now;
            }
            return;
        }
        byte[] f = pendingFrame;
        if (f != null) {
            int type = rxNoResponse ? BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
                                    : BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT;
            if (write(f, type)) pendingFrame = null;
        }
    }

    public int getMtu() { return mtu; }

    /** Mulai kirim berkas mentah ke remot (tanpa pembungkus). Remot harus sudah dalam mode terima. */
    public void startBulk(java.io.InputStream in, long size, int window) {
        bulkIn = in;
        bulkSize = size;
        bulkSent = 0;
        bulkAcked = 0;
        bulkWindow = window;
        bulkLen = 0;
        pendingFrame = null;
        textQ.clear();
        bulkActive = true;
        main.removeCallbacks(pump);
        main.post(pump);
    }

    public void stopBulk() {
        bulkActive = false;
        bulkLen = 0;
        try { if (bulkIn != null) bulkIn.close(); } catch (Exception ignored) { }
        bulkIn = null;
    }

    public void bulkAck(long n) { if (n > bulkAcked) bulkAcked = n; }
    public long bulkSent() { return bulkSent; }
    public boolean bulkAllSent() { return bulkSent >= bulkSize; }

    private void pumpBulk() {
        try {
            if (bulkLen == 0) {
                if (bulkSent >= bulkSize) { bulkActive = false; return; }     // semua terkirim, tinggal menunggu remot
                if (bulkSent - bulkAcked >= bulkWindow) return;                // tunggu pengakuan dari remot
                int want = (int) Math.min(Math.max(20, mtu - 3), bulkSize - bulkSent);
                if (bulkBuf.length < want) bulkBuf = new byte[want];
                int got = 0;
                while (got < want) {
                    int r = bulkIn.read(bulkBuf, got, want - got);
                    if (r < 0) break;
                    got += r;
                }
                if (got <= 0) { bulkActive = false; return; }
                bulkLen = got;
            }
            byte[] part = bulkLen == bulkBuf.length ? bulkBuf : java.util.Arrays.copyOf(bulkBuf, bulkLen);
            int type = rxNoResponse ? BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE
                                    : BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT;
            if (write(part, type)) {
                bulkSent += bulkLen;
                bulkLen = 0;
            }
        } catch (Exception e) {
            bulkActive = false;
        }
    }

    @SuppressWarnings("deprecation")
    private boolean write(byte[] data, int type) {
        boolean ok;
        if (Build.VERSION.SDK_INT >= 33) {
            ok = gatt.writeCharacteristic(rx, data, type) == BluetoothStatusCodes.SUCCESS;
        } else {
            rx.setWriteType(type);
            rx.setValue(data);
            ok = gatt.writeCharacteristic(rx);
        }
        if (ok) { busy = true; busyAt = android.os.SystemClock.uptimeMillis(); }
        return ok;
    }
}
