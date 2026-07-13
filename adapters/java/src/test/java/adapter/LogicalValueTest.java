package adapter;

import static org.junit.jupiter.api.Assertions.*;

import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import java.math.BigInteger;
import java.util.List;
import org.junit.jupiter.api.Test;

class LogicalValueTest {
    private static final ObjectMapper MAPPER = new ObjectMapper()
        .configure(DeserializationFeature.USE_BIG_INTEGER_FOR_INTS, true);

    private static JsonNode parse(String json) {
        try {
            return MAPPER.readTree(json);
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }

    @Test
    void parsesInt() {
        LogicalValue v = LogicalValue.parse(parse("{\"type\":\"int\",\"value\":\"42\"}"));
        assertEquals(new LogicalValue.Int("42"), v);
    }

    @Test
    void parsesFloat() {
        LogicalValue v = LogicalValue.parse(parse("{\"type\":\"float\",\"width\":\"auto\",\"value\":\"2.5\"}"));
        assertEquals(new LogicalValue.Flt("auto", "2.5"), v);
    }

    @Test
    void parsesText() {
        LogicalValue v = LogicalValue.parse(parse("{\"type\":\"text\",\"value\":\"café\"}"));
        assertEquals(new LogicalValue.Text("café"), v);
    }

    @Test
    void parsesBytes() {
        LogicalValue v = LogicalValue.parse(parse("{\"type\":\"bytes\",\"value\":\"deadbeef\"}"));
        assertEquals(new LogicalValue.Bytes("deadbeef"), v);
    }

    @Test
    void parsesBoolAndNull() {
        assertEquals(new LogicalValue.Bool(true), LogicalValue.parse(parse("{\"type\":\"bool\",\"value\":true}")));
        assertEquals(new LogicalValue.Null(), LogicalValue.parse(parse("{\"type\":\"null\"}")));
    }

    @Test
    void parsesArray() {
        LogicalValue v = LogicalValue.parse(
            parse("{\"type\":\"array\",\"items\":[{\"type\":\"int\",\"value\":\"1\"},{\"type\":\"int\",\"value\":\"2\"}]}"));
        assertEquals(new LogicalValue.Arr(List.of(new LogicalValue.Int("1"), new LogicalValue.Int("2"))), v);
    }

    @Test
    void parsesMap() {
        LogicalValue v = LogicalValue.parse(
            parse("{\"type\":\"map\",\"entries\":[[{\"type\":\"text\",\"value\":\"a\"},{\"type\":\"int\",\"value\":\"1\"}]]}"));
        assertEquals(
            new LogicalValue.Map(List.of(java.util.Map.entry(new LogicalValue.Text("a"), new LogicalValue.Int("1")))),
            v);
    }

    @Test
    void parsesTag() {
        LogicalValue v = LogicalValue.parse(parse("{\"type\":\"tag\",\"tag\":100,\"value\":{\"type\":\"int\",\"value\":\"5\"}}"));
        assertEquals(new LogicalValue.Tag(BigInteger.valueOf(100), new LogicalValue.Int("5")), v);
    }

    @Test
    void parsesTagAtU64Max() {
        // Regression: the tag field must survive as BigInteger through the
        // JSON layer without narrowing through a signed 64-bit intermediate.
        // 18446744073709551615 == 2^64-1.
        LogicalValue v = LogicalValue.parse(
            parse("{\"type\":\"tag\",\"tag\":18446744073709551615,\"value\":{\"type\":\"int\",\"value\":\"5\"}}"));
        assertEquals(new LogicalValue.Tag(new BigInteger("18446744073709551615"), new LogicalValue.Int("5")), v);
    }

    @Test
    void rejectsTagAboveU64Max() {
        assertThrows(LogicalValue.ParseException.class, () -> LogicalValue.parse(
            parse("{\"type\":\"tag\",\"tag\":18446744073709551616,\"value\":{\"type\":\"int\",\"value\":\"5\"}}")));
    }

    @Test
    void parsesBignum() {
        LogicalValue v = LogicalValue.parse(
            parse("{\"type\":\"bignum\",\"sign\":\"positive\",\"value\":\"18446744073709551616\"}"));
        assertEquals(new LogicalValue.Bignum("positive", "18446744073709551616"), v);
    }

    @Test
    void rejectsUnknownType() {
        assertThrows(LogicalValue.ParseException.class, () -> LogicalValue.parse(parse("{\"type\":\"nonsense\"}")));
    }

    @Test
    void rejectsMissingType() {
        assertThrows(LogicalValue.ParseException.class, () -> LogicalValue.parse(parse("{}")));
    }
}
