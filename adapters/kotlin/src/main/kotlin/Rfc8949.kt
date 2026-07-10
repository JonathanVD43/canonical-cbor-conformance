import java.math.BigInteger

class EncodeException(message: String) : Exception(message)

private val TWO_POW_64: BigInteger = BigInteger.ONE.shiftLeft(64)

private fun bigIntToBytes(v: BigInteger, width: Int): ByteArray {
    val raw = v.toByteArray()
    val unsigned = if (raw.size > 1 && raw[0] == 0.toByte()) raw.copyOfRange(1, raw.size) else raw
    val out = ByteArray(width)
    unsigned.copyInto(out, destinationOffset = width - unsigned.size)
    return out
}

private fun stripLeadingZeros(bytes: ByteArray): ByteArray {
    var start = 0
    while (start < bytes.size - 1 && bytes[start] == 0.toByte()) start++
    return bytes.copyOfRange(start, bytes.size)
}

fun encodeHead(majorType: Int, arg: BigInteger): ByteArray {
    val mt = majorType shl 5
    return when {
        arg < BigInteger.valueOf(24) -> byteArrayOf((mt or arg.toInt()).toByte())
        arg <= BigInteger.valueOf(0xff) -> byteArrayOf((mt or 24).toByte()) + bigIntToBytes(arg, 1)
        arg <= BigInteger.valueOf(0xffff) -> byteArrayOf((mt or 25).toByte()) + bigIntToBytes(arg, 2)
        arg <= BigInteger.valueOf(0xffffffffL) -> byteArrayOf((mt or 26).toByte()) + bigIntToBytes(arg, 4)
        else -> byteArrayOf((mt or 27).toByte()) + bigIntToBytes(arg, 8)
    }
}

// Not private: reused by Dcbor.kt (same shortest-int rule applies to both profiles).
fun encodeInt(value: String): ByteArray {
    val n = try {
        BigInteger(value)
    } catch (e: Exception) {
        throw EncodeException("int: invalid integer literal \"$value\": ${e.message}")
    }
    return if (n >= BigInteger.ZERO) {
        if (n >= TWO_POW_64) throw EncodeException("int: $n exceeds native range")
        encodeHead(0, n)
    } else {
        val arg = BigInteger.valueOf(-1).subtract(n)
        if (arg >= TWO_POW_64) throw EncodeException("int: $n exceeds native range")
        encodeHead(1, arg)
    }
}

// Not private: reused by Dcbor.kt.
fun doubleToBeBytes(f: Double): ByteArray {
    val bits = java.lang.Double.doubleToRawLongBits(f)
    val out = ByteArray(8)
    for (i in 0 until 8) out[i] = ((bits ushr (56 - 8 * i)) and 0xffL).toByte()
    return out
}

private fun parseFloatLiteral(literal: String): Double =
    literal.toDoubleOrNull() ?: throw EncodeException("float: invalid literal \"$literal\"")

private fun encodeFloat(width: String, literal: String): ByteArray {
    val f = parseFloatLiteral(literal)
    if (f.isNaN()) {
        // P6: NaN always uses the single canonical f16 payload, regardless of width.
        return byteArrayOf(0xf9.toByte(), 0x7e, 0x00)
    }
    if (width == "auto") {
        tryF16(f)?.let { return byteArrayOf(0xf9.toByte()) + it }
        tryF32(f)?.let { return byteArrayOf(0xfa.toByte()) + it }
        return byteArrayOf(0xfb.toByte()) + doubleToBeBytes(f)
    }
    return when (width) {
        "f16" -> byteArrayOf(0xf9.toByte()) + (tryF16(f)
            ?: throw EncodeException("float: $f cannot round-trip at requested width f16"))
        "f32" -> byteArrayOf(0xfa.toByte()) + (tryF32(f)
            ?: throw EncodeException("float: $f cannot round-trip at requested width f32"))
        "f64" -> byteArrayOf(0xfb.toByte()) + doubleToBeBytes(f)
        else -> throw EncodeException("float: unknown width \"$width\"")
    }
}

// Shared with the dcbor profile: bignum tag+magnitude rules are identical in
// both profiles, so both encoders build on this single validated computation.
fun bignumTagAndBytes(sign: String, value: String): Pair<Int, ByteArray> {
    val magnitude = try {
        BigInteger(value)
    } catch (e: Exception) {
        throw EncodeException("bignum: invalid magnitude \"$value\": ${e.message}")
    }
    if (magnitude < TWO_POW_64) {
        val tag = if (sign == "positive") 2 else 3
        throw EncodeException(
            "bignum magnitude $magnitude fits in a native CBOR integer and must not be encoded as tag $tag (SPEC.md bignum rule)"
        )
    }
    val rawInt = when (sign) {
        "positive" -> magnitude
        "negative" -> magnitude.subtract(BigInteger.ONE)
        else -> throw EncodeException("bignum: unknown sign \"$sign\"")
    }
    val tag = if (sign == "positive") 2 else 3
    val bytes = stripLeadingZeros(rawInt.toByteArray())
    return tag to bytes
}

private fun encodeBignum(sign: String, value: String): ByteArray {
    val (tag, bytes) = bignumTagAndBytes(sign, value)
    var out = encodeHead(6, BigInteger.valueOf(tag.toLong()))
    out += encodeHead(2, BigInteger.valueOf(bytes.size.toLong()))
    out += bytes
    return out
}

// Not private: reused by Dcbor.kt (same bytewise map-key sort rule applies to both profiles).
fun compareBytesUnsigned(a: ByteArray, b: ByteArray): Int {
    val len = minOf(a.size, b.size)
    for (i in 0 until len) {
        val ai = a[i].toInt() and 0xff
        val bi = b[i].toInt() and 0xff
        if (ai != bi) return ai - bi
    }
    return a.size - b.size
}

fun encodeRfc8949(value: LogicalValue): ByteArray = when (value) {
    is LogicalValue.Int -> encodeInt(value.value)
    is LogicalValue.Text -> {
        val bytes = value.value.toByteArray(Charsets.UTF_8)
        encodeHead(3, BigInteger.valueOf(bytes.size.toLong())) + bytes
    }
    is LogicalValue.Bytes -> {
        val bytes = hexDecode(value.value)
        encodeHead(2, BigInteger.valueOf(bytes.size.toLong())) + bytes
    }
    is LogicalValue.Bool -> byteArrayOf(if (value.value) 0xf5.toByte() else 0xf4.toByte())
    LogicalValue.Null -> byteArrayOf(0xf6.toByte())
    is LogicalValue.Array -> {
        var out = encodeHead(4, BigInteger.valueOf(value.items.size.toLong()))
        for (item in value.items) out += encodeRfc8949(item)
        out
    }
    is LogicalValue.Float -> encodeFloat(value.width, value.value)
    is LogicalValue.Map -> {
        val encoded = value.entries.map { (k, v) -> encodeRfc8949(k) to encodeRfc8949(v) }
        val sorted = encoded.sortedWith { a, b -> compareBytesUnsigned(a.first, b.first) }
        var out = encodeHead(5, BigInteger.valueOf(value.entries.size.toLong()))
        for ((k, v) in sorted) {
            out += k
            out += v
        }
        out
    }
    is LogicalValue.Tag -> encodeHead(6, BigInteger(value.tag.toString())) + encodeRfc8949(value.value)
    is LogicalValue.Bignum -> encodeBignum(value.sign, value.value)
}
