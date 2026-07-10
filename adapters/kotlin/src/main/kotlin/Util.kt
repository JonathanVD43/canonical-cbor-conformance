fun hexEncode(bytes: ByteArray): String {
    val sb = StringBuilder(bytes.size * 2)
    for (b in bytes) {
        sb.append("%02x".format(b.toInt() and 0xff))
    }
    return sb.toString()
}

fun hexDecode(s: String): ByteArray {
    if (s.length % 2 != 0) {
        throw IllegalArgumentException("hex string has odd length: \"$s\"")
    }
    if (!s.all { it.code in 0..127 }) {
        throw IllegalArgumentException("hex string contains non-ASCII characters: \"$s\"")
    }
    val out = ByteArray(s.length / 2)
    for (i in out.indices) {
        val byteStr = s.substring(i * 2, i * 2 + 2)
        val value = byteStr.toIntOrNull(16)
            ?: throw IllegalArgumentException("invalid hex byte \"$byteStr\"")
        out[i] = value.toByte()
    }
    return out
}
