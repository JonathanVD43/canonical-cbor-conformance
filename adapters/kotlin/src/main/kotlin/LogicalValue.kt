import java.math.BigInteger
import org.json.JSONArray
import org.json.JSONObject

private val U64_MAX = BigInteger("18446744073709551615")

sealed class LogicalValue {
    data class Int(val value: String) : LogicalValue()
    data class Float(val width: String, val value: String) : LogicalValue()
    data class Text(val value: String) : LogicalValue()
    data class Bytes(val value: String) : LogicalValue()
    data class Bool(val value: Boolean) : LogicalValue()
    object Null : LogicalValue()
    data class Array(val items: List<LogicalValue>) : LogicalValue()
    data class Map(val entries: List<Pair<LogicalValue, LogicalValue>>) : LogicalValue()
    data class Tag(val tag: ULong, val value: LogicalValue) : LogicalValue()
    data class Bignum(val sign: String, val value: String) : LogicalValue()
}

class ParseException(message: String) : Exception(message)

private fun JSONObject.requiredString(field: String, typeName: String): String {
    if (!has(field) || isNull(field)) throw ParseException("$typeName: missing \"$field\"")
    val v = get(field)
    if (v !is String) throw ParseException("$typeName: missing \"$field\"")
    return v
}

fun parseLogicalValue(json: Any?): LogicalValue {
    val obj = json as? JSONObject ?: throw ParseException("expected a JSON object")
    if (!obj.has("type") || obj.isNull("type")) throw ParseException("missing \"type\" field")
    val t = obj.get("type") as? String ?: throw ParseException("missing \"type\" field")
    return when (t) {
        "int" -> LogicalValue.Int(obj.requiredString("value", "int"))
        "float" -> LogicalValue.Float(
            obj.requiredString("width", "float"),
            obj.requiredString("value", "float"),
        )
        "text" -> LogicalValue.Text(obj.requiredString("value", "text"))
        "bytes" -> LogicalValue.Bytes(obj.requiredString("value", "bytes"))
        "bool" -> {
            if (!obj.has("value") || obj.isNull("value")) throw ParseException("bool: missing \"value\"")
            val v = obj.get("value") as? Boolean ?: throw ParseException("bool: missing \"value\"")
            LogicalValue.Bool(v)
        }
        "null" -> LogicalValue.Null
        "array" -> {
            val items = obj.opt("items") as? JSONArray ?: throw ParseException("array: missing \"items\"")
            LogicalValue.Array((0 until items.length()).map { parseLogicalValue(items.get(it)) })
        }
        "map" -> {
            val entries = obj.opt("entries") as? JSONArray ?: throw ParseException("map: missing \"entries\"")
            val parsed = (0 until entries.length()).map { i ->
                val pair = entries.get(i) as? JSONArray
                    ?: throw ParseException("map entry must be a 2-element array")
                if (pair.length() != 2) throw ParseException("map entry must have exactly 2 items")
                parseLogicalValue(pair.get(0)) to parseLogicalValue(pair.get(1))
            }
            LogicalValue.Map(parsed)
        }
        "tag" -> {
            if (!obj.has("tag") || obj.isNull("tag")) throw ParseException("tag: missing \"tag\" number")
            val tag = try {
                val big = when (val raw = obj.get("tag")) {
                    is Int -> BigInteger.valueOf(raw.toLong())
                    is Long -> BigInteger.valueOf(raw)
                    is BigInteger -> raw
                    else -> throw ParseException("tag: missing \"tag\" number")
                }
                if (big.signum() < 0 || big > U64_MAX) throw ParseException("tag: missing \"tag\" number")
                big.toString().toULong()
            } catch (e: ParseException) {
                throw e
            } catch (e: Exception) {
                throw ParseException("tag: missing \"tag\" number")
            }
            if (!obj.has("value") || obj.isNull("value")) throw ParseException("tag: missing \"value\"")
            LogicalValue.Tag(tag, parseLogicalValue(obj.get("value")))
        }
        "bignum" -> LogicalValue.Bignum(
            obj.requiredString("sign", "bignum"),
            obj.requiredString("value", "bignum"),
        )
        else -> throw ParseException("unknown logical-value type: \"$t\"")
    }
}
