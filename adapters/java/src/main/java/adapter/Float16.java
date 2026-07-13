package adapter;

/**
 * Hand-rolled IEEE-754 binary16 (f16) &lt;-&gt; binary64 (f64) bit-level
 * conversion. No dependency-free JVM library performs this; ported from the
 * same round-to-nearest-even algorithm the Rust adapter's `half` crate uses
 * (mirrors adapters/kotlin/src/main/kotlin/Float16.kt bit-for-bit).
 */
public final class Float16 {
    private Float16() {}

    public static int doubleToF16Bits(double value) {
        long x = Double.doubleToRawLongBits(value);
        long sign = (x >>> 63) & 1L;
        long exp = (x >>> 52) & 0x7ffL;
        long man = x & 0xfffffffffffffL;

        if (exp == 0x7ffL) {
            long newMan = man >>> 42;
            long nanBit = man != 0L ? 1L << 9 : 0L;
            return (int) (((sign << 15) | 0x7c00L | newMan | nanBit) & 0xffffL);
        }

        long halfSignBits = sign << 15;
        long halfExp = exp - 1023L + 15L;

        if (halfExp >= 0x1fL) {
            return (int) ((halfSignBits | 0x7c00L) & 0xffffL);
        }

        if (halfExp <= 0L) {
            if (10L - halfExp > 21L) {
                return (int) (halfSignBits & 0xffffL);
            }
            long manWithHidden = man | 0x10000000000000L;
            long halfMan = manWithHidden >>> (int) (43L - halfExp);
            long roundBit = 1L << (int) (42L - halfExp);
            if ((manWithHidden & roundBit) != 0L && (manWithHidden & (3L * roundBit - 1L)) != 0L) {
                halfMan += 1L;
            }
            return (int) ((halfSignBits | halfMan) & 0xffffL);
        }

        long halfExpBits = halfExp << 10;
        long halfMan = man >>> 42;
        long roundBit = 1L << 41;
        long combined = halfSignBits | halfExpBits | halfMan;
        boolean roundUp = (man & roundBit) != 0L && (man & (3L * roundBit - 1L)) != 0L;
        return (int) ((roundUp ? combined + 1L : combined) & 0xffffL);
    }

    public static double f16BitsToDouble(int bits) {
        long i = bits & 0xffffL;
        long sign = (i >>> 15) & 1L;
        long exp = (i >>> 10) & 0x1fL;
        long man = i & 0x3ffL;
        long sign64 = sign << 63;

        if (exp == 0L && man == 0L) {
            return Double.longBitsToDouble(sign64);
        }
        if (exp == 0L) {
            // Normalize the 10-bit subnormal significand by left-shifting
            // until its leading set bit reaches position 10 (the
            // implicit-bit position), counting shifts via expAdj. expAdj
            // MUST start at 1, not -1: for the smallest subnormal (man=1,
            // true value 2^-24), exactly 10 shifts are needed to reach
            // manAdj=0x400, and the final double exponent must come out to
            // 1023-24=999. That only holds if expAdj lands on 1-10=-9
            // (giving 1008+(-9)=999); a -1 starting point yields -11,
            // undercounting by 2 and silently quartering the reconstructed
            // magnitude of every subnormal f16 value.
            long expAdj = 1L;
            long manAdj = man;
            while ((manAdj & 0x400L) == 0L) {
                manAdj <<= 1;
                expAdj -= 1L;
            }
            manAdj &= 0x3ffL;
            long exp64 = 1023L - 15L + expAdj;
            long man64 = manAdj << 42;
            return Double.longBitsToDouble(sign64 | (exp64 << 52) | man64);
        }
        if (exp == 0x1fL) {
            if (man == 0L) {
                return Double.longBitsToDouble(sign64 | 0x7ff0000000000000L);
            }
            return Double.longBitsToDouble(sign64 | 0x7ff0000000000000L | (man << 42));
        }
        long exp64 = exp - 15L + 1023L;
        long man64 = man << 42;
        return Double.longBitsToDouble(sign64 | (exp64 << 52) | man64);
    }

    private static boolean sameBits(double a, double b) {
        return Double.doubleToRawLongBits(a) == Double.doubleToRawLongBits(b);
    }

    /** Callers must handle NaN before reaching these helpers -- sameBits's
     * raw-bits comparison never matches NaN to NaN. */
    public static byte[] tryF16(double f) {
        int bits = doubleToF16Bits(f);
        double back = f16BitsToDouble(bits);
        if (!sameBits(back, f)) return null;
        return new byte[] { (byte) ((bits >>> 8) & 0xff), (byte) (bits & 0xff) };
    }

    public static byte[] tryF32(double f) {
        float v = (float) f;
        double back = v;
        if (!sameBits(back, f)) return null;
        int bits = Float.floatToRawIntBits(v);
        return new byte[] {
            (byte) ((bits >>> 24) & 0xff),
            (byte) ((bits >>> 16) & 0xff),
            (byte) ((bits >>> 8) & 0xff),
            (byte) (bits & 0xff),
        };
    }
}
