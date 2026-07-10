import org.json.JSONObject
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith

class LogicalValueTest {
    @Test
    fun parsesInt() {
        val v = parseLogicalValue(JSONObject("""{"type":"int","value":"42"}"""))
        assertEquals(LogicalValue.Int("42"), v)
    }

    @Test
    fun parsesFloat() {
        val v = parseLogicalValue(JSONObject("""{"type":"float","width":"auto","value":"2.5"}"""))
        assertEquals(LogicalValue.Float("auto", "2.5"), v)
    }

    @Test
    fun parsesText() {
        val v = parseLogicalValue(JSONObject("""{"type":"text","value":"café"}"""))
        assertEquals(LogicalValue.Text("café"), v)
    }

    @Test
    fun parsesBytes() {
        val v = parseLogicalValue(JSONObject("""{"type":"bytes","value":"deadbeef"}"""))
        assertEquals(LogicalValue.Bytes("deadbeef"), v)
    }

    @Test
    fun parsesBoolAndNull() {
        assertEquals(LogicalValue.Bool(true), parseLogicalValue(JSONObject("""{"type":"bool","value":true}""")))
        assertEquals(LogicalValue.Null, parseLogicalValue(JSONObject("""{"type":"null"}""")))
    }

    @Test
    fun parsesArray() {
        val v = parseLogicalValue(
            JSONObject(
                """{"type":"array","items":[{"type":"int","value":"1"},{"type":"int","value":"2"}]}"""
            )
        )
        assertEquals(LogicalValue.Array(listOf(LogicalValue.Int("1"), LogicalValue.Int("2"))), v)
    }

    @Test
    fun parsesMap() {
        val v = parseLogicalValue(
            JSONObject(
                """{"type":"map","entries":[[{"type":"text","value":"a"},{"type":"int","value":"1"}]]}"""
            )
        )
        assertEquals(
            LogicalValue.Map(listOf(LogicalValue.Text("a") to LogicalValue.Int("1"))),
            v,
        )
    }

    @Test
    fun parsesTag() {
        val v = parseLogicalValue(
            JSONObject("""{"type":"tag","tag":100,"value":{"type":"int","value":"5"}}""")
        )
        assertEquals(LogicalValue.Tag(100uL, LogicalValue.Int("5")), v)
    }

    @Test
    fun parsesTagAtULongMax() {
        // Regression: obj.get("tag") returns a BigInteger for JSON integer
        // literals too large for Long/Int, and the old Long-based pipeline
        // silently truncated this. 18446744073709551615 == ULong.MAX_VALUE.
        val v = parseLogicalValue(
            JSONObject("""{"type":"tag","tag":18446744073709551615,"value":{"type":"int","value":"5"}}""")
        )
        assertEquals(LogicalValue.Tag(ULong.MAX_VALUE, LogicalValue.Int("5")), v)
    }

    @Test
    fun rejectsTagAboveULongMax() {
        assertFailsWith<ParseException> {
            parseLogicalValue(
                JSONObject("""{"type":"tag","tag":18446744073709551616,"value":{"type":"int","value":"5"}}""")
            )
        }
    }

    @Test
    fun parsesBignum() {
        val v = parseLogicalValue(
            JSONObject("""{"type":"bignum","sign":"positive","value":"18446744073709551616"}""")
        )
        assertEquals(LogicalValue.Bignum("positive", "18446744073709551616"), v)
    }

    @Test
    fun rejectsUnknownType() {
        assertFailsWith<ParseException> { parseLogicalValue(JSONObject("""{"type":"nonsense"}""")) }
    }

    @Test
    fun rejectsMissingType() {
        assertFailsWith<ParseException> { parseLogicalValue(JSONObject("""{}""")) }
    }
}
