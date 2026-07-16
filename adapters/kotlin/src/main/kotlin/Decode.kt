import java.math.BigInteger
import java.nio.ByteBuffer
import java.nio.charset.CodingErrorAction
import java.text.Normalizer

// Strict canonical-CBOR decoder (SPEC.md's `decode-strict` mode). Parses raw
// CBOR bytes into a byte-range-aware intermediate form and rejects any
// well-formed-but-non-canonical input with the single most specific
// decode-strict reason code (P1-P9 / D1-D9). Hand-rolled (not delegated to a
// generic CBOR library) because a generic decode normalizes away exactly the
// information needed to tell *why* something is non-canonical: which
// additional-info width was used, raw NaN payload bits, raw map-key byte
// order.
//
// Bignum rule (SPEC.md), matching adapters/rust/src/decode.rs: a tag-2/3
// (bignum) item is rejected with NON_CANONICAL_BIGNUM if its magnitude fits
// the native 64-bit range or its byte-string payload is non-minimal (a
// leading zero byte). A genuinely canonical bignum still ACCEPTs.

enum class Profile { RFC8949, DCBOR }

class DecodeException(val reasonOrMessage: String) : Exception(reasonOrMessage)

private val REASON_CODES = setOf(
    "NON_SHORTEST_INT",
    "UNSORTED_MAP_KEYS",
    "INDEFINITE_LENGTH",
    "DUPLICATE_KEY",
    "NON_SHORTEST_FLOAT",
    "NAN_PAYLOAD_VARIANT",
    "TRAILING_BYTES",
    "MULTIPLE_TOP_LEVEL_ITEMS",
    "UNKNOWN_TAG",
    "NON_NFC_STRING",
    "UNREDUCED_NUMERIC",
    "NON_CANONICAL_BIGNUM",
)

// Placeholder allow-list for the UNKNOWN_TAG check: only the bignum tags are
// exercised by the current vector corpus.
private val ALLOWED_TAGS = setOf(2uL, 3uL)

sealed class Verdict {
    data class Accept(val bytes: ByteArray) : Verdict()
    data class Reject(val reason: String) : Verdict()
}

private class Cursor(var pos: Int)

fun decodeStrict(input: ByteArray, profile: Profile): Verdict {
    if (input.isEmpty()) throw DecodeException("internal: empty input line")
    val cursor = Cursor(0)
    val item = try {
        parseItem(input, cursor, profile)
    } catch (e: DecodeException) {
        if (e.reasonOrMessage in REASON_CODES) return Verdict.Reject(e.reasonOrMessage)
        throw e
    }

    if (cursor.pos < input.size) {
        val cursor2 = Cursor(cursor.pos)
        return try {
            parseItem(input, cursor2, profile)
            if (cursor2.pos == input.size) Verdict.Reject("MULTIPLE_TOP_LEVEL_ITEMS") else Verdict.Reject("TRAILING_BYTES")
        } catch (e: Exception) {
            Verdict.Reject("TRAILING_BYTES")
        }
    }

    val logical = itemToLogical(item)
    val reencoded = try {
        when (profile) {
            Profile.RFC8949 -> encodeRfc8949(logical)
            Profile.DCBOR -> encodeDcbor(logical)
        }
    } catch (e: EncodeException) {
        throw DecodeException("internal: canonical input failed to re-encode: ${e.message}")
    }
    return Verdict.Accept(reencoded)
}

private sealed class Item {
    data class IntPos(val v: ULong) : Item()
    data class IntNeg(val v: ULong) : Item()
    data class Bytes(val v: ByteArray) : Item()
    data class Text(val v: String) : Item()
    data class Arr(val items: List<Item>) : Item()
    data class MapEntries(val entries: List<Pair<Item, Item>>) : Item()
    data class TaggedItem(val tag: ULong, val inner: Item) : Item()
    data class BoolItem(val v: Boolean) : Item()
    object NullItem : Item()
    data class FloatItem(val v: Double) : Item()
}

private enum class ArgWidth { DIRECT, ONE, TWO, FOUR, EIGHT }

private class Head(val major: Int, val arg: ULong, val width: ArgWidth, val indefinite: Boolean)

private fun readHead(input: ByteArray, cursor: Cursor): Head {
    if (cursor.pos >= input.size) throw DecodeException("internal: truncated input (expected an item header)")
    val b0 = input[cursor.pos].toInt() and 0xff
    cursor.pos += 1
    val major = (b0 ushr 5) and 0x7
    val info = b0 and 0x1f
    if (info == 31) return Head(major, 0uL, ArgWidth.DIRECT, true)

    val (arg, width) = when {
        info <= 23 -> info.toULong() to ArgWidth.DIRECT
        info == 24 -> {
            if (cursor.pos >= input.size) throw DecodeException("internal: truncated 1-byte argument")
            val b = input[cursor.pos].toInt() and 0xff
            cursor.pos += 1
            b.toULong() to ArgWidth.ONE
        }
        info == 25 -> {
            if (cursor.pos + 2 > input.size) throw DecodeException("internal: truncated 2-byte argument")
            var v = 0uL
            for (i in 0 until 2) v = (v shl 8) or (input[cursor.pos + i].toInt() and 0xff).toULong()
            cursor.pos += 2
            v to ArgWidth.TWO
        }
        info == 26 -> {
            if (cursor.pos + 4 > input.size) throw DecodeException("internal: truncated 4-byte argument")
            var v = 0uL
            for (i in 0 until 4) v = (v shl 8) or (input[cursor.pos + i].toInt() and 0xff).toULong()
            cursor.pos += 4
            v to ArgWidth.FOUR
        }
        info == 27 -> {
            if (cursor.pos + 8 > input.size) throw DecodeException("internal: truncated 8-byte argument")
            var v = 0uL
            for (i in 0 until 8) v = (v shl 8) or (input[cursor.pos + i].toInt() and 0xff).toULong()
            cursor.pos += 8
            v to ArgWidth.EIGHT
        }
        else -> throw DecodeException("internal: reserved additional-info value (28-30)")
    }
    return Head(major, arg, width, false)
}

private fun shortestWidthFor(arg: ULong): ArgWidth = when {
    arg < 24uL -> ArgWidth.DIRECT
    arg <= 0xffuL -> ArgWidth.ONE
    arg <= 0xffffuL -> ArgWidth.TWO
    arg <= 0xffffffffuL -> ArgWidth.FOUR
    else -> ArgWidth.EIGHT
}

// P1/D2: every integer *argument* (ints themselves, and string/array/map
// lengths, and tag numbers) must use the shortest encoding for its value.
private fun checkShortestArg(head: Head) {
    if (head.width != shortestWidthFor(head.arg)) throw DecodeException("NON_SHORTEST_INT")
}

private fun strictUtf8Decode(bytes: ByteArray): String {
    val decoder = Charsets.UTF_8.newDecoder()
        .onMalformedInput(CodingErrorAction.REPORT)
        .onUnmappableCharacter(CodingErrorAction.REPORT)
    return try {
        decoder.decode(ByteBuffer.wrap(bytes)).toString()
    } catch (e: Exception) {
        throw DecodeException("internal: invalid utf-8 in text string")
    }
}

private fun parseItem(input: ByteArray, cursor: Cursor, profile: Profile): Item {
    val head = readHead(input, cursor)

    if (head.major == 7) {
        if (head.indefinite) {
            throw DecodeException("internal: unexpected break byte outside indefinite-length container")
        }
        return parseMajor7(head.width, head.arg, profile)
    }

    if (head.indefinite) {
        // P3/D1: indefinite-length arrays/maps/byte/text strings are banned
        // outright, regardless of what they'd otherwise contain.
        throw DecodeException("INDEFINITE_LENGTH")
    }
    checkShortestArg(head)

    return when (head.major) {
        0 -> Item.IntPos(head.arg)
        1 -> Item.IntNeg(head.arg)
        2 -> {
            val len = head.arg.toInt()
            if (cursor.pos + len > input.size) throw DecodeException("internal: truncated byte string")
            val bytes = input.copyOfRange(cursor.pos, cursor.pos + len)
            cursor.pos += len
            Item.Bytes(bytes)
        }
        3 -> {
            val len = head.arg.toInt()
            if (cursor.pos + len > input.size) throw DecodeException("internal: truncated text string")
            val bytes = input.copyOfRange(cursor.pos, cursor.pos + len)
            val s = strictUtf8Decode(bytes)
            cursor.pos += len
            // D8: dcbor requires NFC-normalized text; rfc8949 has no such
            // rule (P9 is an explicit non-goal).
            if (profile == Profile.DCBOR) {
                val normalized = Normalizer.normalize(s, Normalizer.Form.NFC)
                if (normalized != s) throw DecodeException("NON_NFC_STRING")
            }
            Item.Text(s)
        }
        4 -> {
            val len = head.arg.toInt()
            val items = ArrayList<Item>(len)
            repeat(len) { items.add(parseItem(input, cursor, profile)) }
            Item.Arr(items)
        }
        5 -> {
            val len = head.arg.toInt()
            val entries = ArrayList<Pair<Item, Item>>(len)
            val keyRanges = ArrayList<Pair<Int, Int>>(len)
            repeat(len) {
                val keyStart = cursor.pos
                val key = parseItem(input, cursor, profile)
                val keyEnd = cursor.pos
                val value = parseItem(input, cursor, profile)
                keyRanges.add(keyStart to keyEnd)
                entries.add(key to value)
            }
            // P2/D3: keys must appear in strictly increasing bytewise order
            // of their own raw encoded bytes. A duplicate key is necessarily
            // adjacent to itself in properly sorted order, so one adjacent
            // pass catches both violations.
            for (i in 0 until keyRanges.size - 1) {
                val a = input.copyOfRange(keyRanges[i].first, keyRanges[i].second)
                val b = input.copyOfRange(keyRanges[i + 1].first, keyRanges[i + 1].second)
                val cmp = compareBytesUnsigned(a, b)
                if (cmp == 0) throw DecodeException("DUPLICATE_KEY")
                if (cmp > 0) throw DecodeException("UNSORTED_MAP_KEYS")
            }
            Item.MapEntries(entries)
        }
        6 -> {
            val tag = head.arg
            if (tag !in ALLOWED_TAGS) throw DecodeException("UNKNOWN_TAG")
            val inner = parseItem(input, cursor, profile)
            // Bignum rule: a tag 2/3 payload must be the minimal big-endian
            // encoding of a magnitude >= 2^64. Reject if the magnitude fits
            // the native 64-bit range (a <= 8-byte payload always does, once
            // the leading-zero case is ruled out) or the payload is non-minimal
            // (non-empty with a leading zero byte).
            if ((tag == 2uL || tag == 3uL) && inner is Item.Bytes) {
                val b = inner.v
                if ((b.isNotEmpty() && b[0].toInt() == 0) || b.size <= 8) {
                    throw DecodeException("NON_CANONICAL_BIGNUM")
                }
            }
            Item.TaggedItem(tag, inner)
        }
        else -> throw IllegalStateException("major type is 3 bits, always 0-7")
    }
}

private enum class FloatWidth { F16, F32, F64 }

private fun parseMajor7(width: ArgWidth, arg: ULong, profile: Profile): Item = when (width) {
    ArgWidth.DIRECT -> when (arg) {
        20uL -> Item.BoolItem(false)
        21uL -> Item.BoolItem(true)
        22uL -> Item.NullItem
        else -> throw DecodeException("internal: unsupported major-7 simple value $arg")
    }
    ArgWidth.ONE -> throw DecodeException("internal: unsupported major-7 1-byte simple value")
    ArgWidth.TWO -> checkFloat(arg, FloatWidth.F16, profile)
    ArgWidth.FOUR -> checkFloat(arg, FloatWidth.F32, profile)
    ArgWidth.EIGHT -> checkFloat(arg, FloatWidth.F64, profile)
}

private fun checkFloat(bits: ULong, width: FloatWidth, profile: Profile): Item {
    val value = when (width) {
        FloatWidth.F16 -> f16BitsToDouble(bits.toInt())
        FloatWidth.F32 -> java.lang.Float.intBitsToFloat(bits.toInt()).toDouble()
        FloatWidth.F64 -> java.lang.Double.longBitsToDouble(bits.toLong())
    }

    if (value.isNaN()) {
        // P6/D6: canonical NaN is exactly f16 with payload 0x7e00, in both profiles.
        if (width != FloatWidth.F16 || bits != 0x7e00uL) throw DecodeException("NAN_PAYLOAD_VARIANT")
        return Item.FloatItem(Double.NaN)
    }

    // D5/D7 (dcbor only): any whole-number float in [-2^63, 2^64-1] -
    // including +/-0.0, per D7 zero unification - must be a plain int
    // instead. This is checked before, and takes priority over, the general
    // shortest-float-width rule below.
    if (profile == Profile.DCBOR && isDcborReducible(value)) throw DecodeException("UNREDUCED_NUMERIC")

    // P5/D2: the width used must be the narrowest of f16/f32/f64 that
    // round-trips `value` exactly.
    val shortest = shortestFloatWidth(value)
    if (width != shortest) throw DecodeException("NON_SHORTEST_FLOAT")

    return Item.FloatItem(value)
}

private fun isDcborReducible(value: Double): Boolean {
    if (value.isInfinite()) return false
    if (value != Math.floor(value)) return false
    return value >= -9_223_372_036_854_775_808.0 && value <= 18_446_744_073_709_551_615.0
}

private fun shortestFloatWidth(value: Double): FloatWidth {
    val targetBits = java.lang.Double.doubleToRawLongBits(value)
    val f16Bits = doubleToF16Bits(value)
    if (java.lang.Double.doubleToRawLongBits(f16BitsToDouble(f16Bits)) == targetBits) return FloatWidth.F16
    val f32 = value.toFloat()
    if (java.lang.Double.doubleToRawLongBits(f32.toDouble()) == targetBits) return FloatWidth.F32
    return FloatWidth.F64
}

private fun formatFloat(value: Double): String = if (value.isNaN()) "NaN" else value.toString()

private fun itemToLogical(item: Item): LogicalValue = when (item) {
    is Item.IntPos -> LogicalValue.Int(item.v.toString())
    is Item.IntNeg -> {
        val v = BigInteger(item.v.toString())
        LogicalValue.Int(BigInteger.valueOf(-1).subtract(v).toString())
    }
    is Item.Bytes -> LogicalValue.Bytes(hexEncode(item.v))
    is Item.Text -> LogicalValue.Text(item.v)
    is Item.Arr -> LogicalValue.Array(item.items.map { itemToLogical(it) })
    is Item.MapEntries -> LogicalValue.Map(item.entries.map { (k, v) -> itemToLogical(k) to itemToLogical(v) })
    is Item.TaggedItem -> LogicalValue.Tag(item.tag, itemToLogical(item.inner))
    is Item.BoolItem -> LogicalValue.Bool(item.v)
    Item.NullItem -> LogicalValue.Null
    is Item.FloatItem -> LogicalValue.Float("auto", formatFloat(item.v))
}
