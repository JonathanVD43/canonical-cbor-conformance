// Hand-rolled IEEE-754 binary16 (f16) <-> binary64 (f64) bit-level conversion.
// No dependency-free JVM library performs this; ported from the same
// round-to-nearest-even algorithm the Rust adapter's `half` crate uses.

fun doubleToF16Bits(value: Double): Int {
    val x = java.lang.Double.doubleToRawLongBits(value)
    val sign = (x ushr 63) and 1L
    val exp = (x ushr 52) and 0x7ffL
    val man = x and 0xfffffffffffffL

    if (exp == 0x7ffL) {
        // Infinity or NaN.
        val newMan = man ushr 42
        val nanBit = if (man != 0L) 1L shl 9 else 0L
        return (((sign shl 15) or 0x7c00L or newMan or nanBit) and 0xffffL).toInt()
    }

    val halfSignBits = sign shl 15
    val halfExp = exp - 1023L + 15L

    if (halfExp >= 0x1fL) {
        return ((halfSignBits or 0x7c00L) and 0xffffL).toInt()
    }

    if (halfExp <= 0L) {
        if (10L - halfExp > 21L) {
            // Underflows even a subnormal half: signed zero.
            return (halfSignBits and 0xffffL).toInt()
        }
        val manWithHidden = man or 0x10000000000000L
        var halfMan = manWithHidden ushr (43L - halfExp).toInt()
        val roundBit = 1L shl (42L - halfExp).toInt()
        if ((manWithHidden and roundBit) != 0L && (manWithHidden and (3L * roundBit - 1L)) != 0L) {
            halfMan += 1L
        }
        return ((halfSignBits or halfMan) and 0xffffL).toInt()
    }

    val halfExpBits = halfExp shl 10
    val halfMan = man ushr 42
    val roundBit = 1L shl 41
    val combined = halfSignBits or halfExpBits or halfMan
    val roundUp = (man and roundBit) != 0L && (man and (3L * roundBit - 1L)) != 0L
    return ((if (roundUp) combined + 1L else combined) and 0xffffL).toInt()
}

fun f16BitsToDouble(bits: Int): Double {
    val i = bits.toLong() and 0xffffL
    val sign = (i ushr 15) and 1L
    val exp = (i ushr 10) and 0x1fL
    val man = i and 0x3ffL
    val sign64 = sign shl 63

    return when {
        exp == 0L && man == 0L -> java.lang.Double.longBitsToDouble(sign64)
        exp == 0L -> {
            var expAdj = -1L
            var manAdj = man
            while ((manAdj and 0x400L) == 0L) {
                manAdj = manAdj shl 1
                expAdj -= 1L
            }
            manAdj = manAdj and 0x3ffL
            val exp64 = 1023L - 15L + expAdj
            val man64 = manAdj shl 42
            java.lang.Double.longBitsToDouble(sign64 or (exp64 shl 52) or man64)
        }
        exp == 0x1fL -> {
            if (man == 0L) {
                java.lang.Double.longBitsToDouble(sign64 or 0x7ff0000000000000L)
            } else {
                java.lang.Double.longBitsToDouble(sign64 or 0x7ff0000000000000L or (man shl 42))
            }
        }
        else -> {
            val exp64 = exp - 15L + 1023L
            val man64 = man shl 42
            java.lang.Double.longBitsToDouble(sign64 or (exp64 shl 52) or man64)
        }
    }
}

private fun sameBits(a: Double, b: Double): Boolean =
    java.lang.Double.doubleToRawLongBits(a) == java.lang.Double.doubleToRawLongBits(b)

// Callers must handle NaN before reaching these helpers — sameBits's raw-bits
// comparison never matches NaN to NaN (mirrors adapters/rust/src/rfc8949.rs).
fun tryF16(f: Double): ByteArray? {
    val bits = doubleToF16Bits(f)
    val back = f16BitsToDouble(bits)
    if (!sameBits(back, f)) return null
    return byteArrayOf(((bits ushr 8) and 0xff).toByte(), (bits and 0xff).toByte())
}

fun tryF32(f: Double): ByteArray? {
    val v = f.toFloat()
    val back = v.toDouble()
    if (!sameBits(back, f)) return null
    val bits = java.lang.Float.floatToRawIntBits(v)
    return byteArrayOf(
        ((bits ushr 24) and 0xff).toByte(),
        ((bits ushr 16) and 0xff).toByte(),
        ((bits ushr 8) and 0xff).toByte(),
        (bits and 0xff).toByte(),
    )
}
