import java.math.BigDecimal
import java.math.BigInteger
import java.text.Normalizer

// dCBOR profile: no JVM port of the Rust adapter's `dcbor` crate exists, so
// D5 (numeric reduction), D6 (NaN pin), D7 (zero unification), and D8 (NFC
// normalization) are reimplemented directly here. D3 (map key sort) and
// bignum handling reuse the exact same rules as rfc8949 (see Rfc8949.kt).

private val DCBOR_TWO_POW_64: BigInteger = BigInteger.ONE.shiftLeft(64)

private fun encodeFloatDcbor(literal: String): ByteArray {
    val f = literal.toDoubleOrNull() ?: throw EncodeException("float: invalid literal \"$literal\"")
    if (f.isNaN()) {
        // D6: NaN always uses the single canonical f16 payload.
        return byteArrayOf(0xf9.toByte(), 0x7e, 0x00)
    }
    if (!f.isInfinite() && f == Math.floor(f)) {
        // D5/D7: a float with no fractional part (including -0.0) reduces to
        // the shortest-int form, unifying with the plain integer encoding.
        val big = BigDecimal(f).toBigInteger()
        if (big >= BigInteger.ZERO) {
            if (big < DCBOR_TWO_POW_64) return encodeHead(0, big)
        } else {
            val arg = BigInteger.valueOf(-1).subtract(big)
            if (arg < DCBOR_TWO_POW_64) return encodeHead(1, arg)
        }
        // Falls through to shortest-float form below if it doesn't fit the
        // native int range (no dcbor vector exercises this edge).
    }
    tryF16(f)?.let { return byteArrayOf(0xf9.toByte()) + it }
    tryF32(f)?.let { return byteArrayOf(0xfa.toByte()) + it }
    return byteArrayOf(0xfb.toByte()) + doubleToBeBytes(f)
}

private fun encodeBignumDcbor(sign: String, value: String): ByteArray {
    val (tag, bytes) = bignumTagAndBytes(sign, value)
    var out = encodeHead(6, BigInteger.valueOf(tag.toLong()))
    out += encodeHead(2, BigInteger.valueOf(bytes.size.toLong()))
    out += bytes
    return out
}

fun encodeDcbor(value: LogicalValue): ByteArray = when (value) {
    is LogicalValue.Int -> encodeInt(value.value)
    is LogicalValue.Float -> encodeFloatDcbor(value.value)
    is LogicalValue.Text -> {
        // D8: normalize to NFC before UTF-8 encoding.
        val bytes = Normalizer.normalize(value.value, Normalizer.Form.NFC).toByteArray(Charsets.UTF_8)
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
        for (item in value.items) out += encodeDcbor(item)
        out
    }
    is LogicalValue.Map -> {
        // dCBOR's reference encoder (the `dcbor` crate's `Map::insert`) stores
        // entries in a real map keyed by canonical-encoded bytes, so two
        // logical entries that canonicalize to the same key (e.g. via D7
        // zero unification or D8 NFC normalization) collapse to one, last
        // write wins. Mirror that with a LinkedHashMap keyed by hex(key).
        val dedup = LinkedHashMap<String, Pair<ByteArray, ByteArray>>()
        for ((k, v) in value.entries) {
            val encodedKey = encodeDcbor(k)
            dedup[hexEncode(encodedKey)] = encodedKey to encodeDcbor(v)
        }
        val sorted = dedup.values.sortedWith { a, b -> compareBytesUnsigned(a.first, b.first) }
        var out = encodeHead(5, BigInteger.valueOf(sorted.size.toLong()))
        for ((k, v) in sorted) {
            out += k
            out += v
        }
        out
    }
    is LogicalValue.Tag -> encodeHead(6, BigInteger(value.tag.toString())) + encodeDcbor(value.value)
    is LogicalValue.Bignum -> encodeBignumDcbor(value.sign, value.value)
}
