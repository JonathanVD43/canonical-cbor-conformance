use serde_json::Value;

#[derive(Debug, Clone, PartialEq)]
pub enum LogicalValue {
    Int(String),
    Float { width: String, value: String },
    Text(String),
    Bytes(String),
    Bool(bool),
    Null,
    Array(Vec<LogicalValue>),
    Map(Vec<(LogicalValue, LogicalValue)>),
    Tag(u64, Box<LogicalValue>),
    Bignum { sign: String, value: String },
}

#[derive(Debug)]
pub struct ParseError(pub String);

pub fn parse(json: &Value) -> Result<LogicalValue, ParseError> {
    let obj = json.as_object().ok_or_else(|| ParseError("expected a JSON object".to_string()))?;
    let t = obj
        .get("type")
        .and_then(Value::as_str)
        .ok_or_else(|| ParseError("missing \"type\" field".to_string()))?;
    match t {
        "int" => {
            let v = obj.get("value").and_then(Value::as_str)
                .ok_or_else(|| ParseError("int: missing \"value\"".to_string()))?;
            Ok(LogicalValue::Int(v.to_string()))
        }
        "float" => {
            let width = obj.get("width").and_then(Value::as_str)
                .ok_or_else(|| ParseError("float: missing \"width\"".to_string()))?;
            let value = obj.get("value").and_then(Value::as_str)
                .ok_or_else(|| ParseError("float: missing \"value\"".to_string()))?;
            Ok(LogicalValue::Float { width: width.to_string(), value: value.to_string() })
        }
        "text" => {
            let v = obj.get("value").and_then(Value::as_str)
                .ok_or_else(|| ParseError("text: missing \"value\"".to_string()))?;
            Ok(LogicalValue::Text(v.to_string()))
        }
        "bytes" => {
            let v = obj.get("value").and_then(Value::as_str)
                .ok_or_else(|| ParseError("bytes: missing \"value\"".to_string()))?;
            Ok(LogicalValue::Bytes(v.to_string()))
        }
        "bool" => {
            let v = obj.get("value").and_then(Value::as_bool)
                .ok_or_else(|| ParseError("bool: missing \"value\"".to_string()))?;
            Ok(LogicalValue::Bool(v))
        }
        "null" => Ok(LogicalValue::Null),
        "array" => {
            let items = obj.get("items").and_then(Value::as_array)
                .ok_or_else(|| ParseError("array: missing \"items\"".to_string()))?;
            let parsed = items.iter().map(parse).collect::<Result<Vec<_>, _>>()?;
            Ok(LogicalValue::Array(parsed))
        }
        "map" => {
            let entries = obj.get("entries").and_then(Value::as_array)
                .ok_or_else(|| ParseError("map: missing \"entries\"".to_string()))?;
            let mut parsed = Vec::with_capacity(entries.len());
            for entry in entries {
                let pair = entry.as_array()
                    .ok_or_else(|| ParseError("map entry must be a 2-element array".to_string()))?;
                if pair.len() != 2 {
                    return Err(ParseError("map entry must have exactly 2 items".to_string()));
                }
                parsed.push((parse(&pair[0])?, parse(&pair[1])?));
            }
            Ok(LogicalValue::Map(parsed))
        }
        "tag" => {
            let tag = obj.get("tag").and_then(Value::as_u64)
                .ok_or_else(|| ParseError("tag: missing \"tag\" number".to_string()))?;
            let value = obj.get("value")
                .ok_or_else(|| ParseError("tag: missing \"value\"".to_string()))?;
            Ok(LogicalValue::Tag(tag, Box::new(parse(value)?)))
        }
        "bignum" => {
            let sign = obj.get("sign").and_then(Value::as_str)
                .ok_or_else(|| ParseError("bignum: missing \"sign\"".to_string()))?;
            let value = obj.get("value").and_then(Value::as_str)
                .ok_or_else(|| ParseError("bignum: missing \"value\"".to_string()))?;
            Ok(LogicalValue::Bignum { sign: sign.to_string(), value: value.to_string() })
        }
        other => Err(ParseError(format!("unknown logical-value type: {other:?}"))),
    }
}

pub fn to_json(value: &LogicalValue) -> Value {
    match value {
        LogicalValue::Int(s) => serde_json::json!({"type": "int", "value": s}),
        LogicalValue::Float { width, value } => {
            serde_json::json!({"type": "float", "width": width, "value": value})
        }
        LogicalValue::Text(s) => serde_json::json!({"type": "text", "value": s}),
        LogicalValue::Bytes(hex) => serde_json::json!({"type": "bytes", "value": hex}),
        LogicalValue::Bool(b) => serde_json::json!({"type": "bool", "value": b}),
        LogicalValue::Null => serde_json::json!({"type": "null"}),
        LogicalValue::Array(items) => {
            serde_json::json!({"type": "array", "items": items.iter().map(to_json).collect::<Vec<_>>()})
        }
        LogicalValue::Map(entries) => {
            let entries: Vec<Value> = entries.iter().map(|(k, v)| serde_json::json!([to_json(k), to_json(v)])).collect();
            serde_json::json!({"type": "map", "entries": entries})
        }
        LogicalValue::Tag(tag, inner) => {
            serde_json::json!({"type": "tag", "tag": tag, "value": to_json(inner)})
        }
        LogicalValue::Bignum { sign, value } => {
            serde_json::json!({"type": "bignum", "sign": sign, "value": value})
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn parses_int() {
        let v = parse(&json!({"type": "int", "value": "42"})).unwrap();
        assert_eq!(v, LogicalValue::Int("42".to_string()));
    }

    #[test]
    fn parses_float() {
        let v = parse(&json!({"type": "float", "width": "auto", "value": "2.5"})).unwrap();
        assert_eq!(v, LogicalValue::Float { width: "auto".to_string(), value: "2.5".to_string() });
    }

    #[test]
    fn parses_text() {
        let v = parse(&json!({"type": "text", "value": "café"})).unwrap();
        assert_eq!(v, LogicalValue::Text("café".to_string()));
    }

    #[test]
    fn parses_bytes() {
        let v = parse(&json!({"type": "bytes", "value": "deadbeef"})).unwrap();
        assert_eq!(v, LogicalValue::Bytes("deadbeef".to_string()));
    }

    #[test]
    fn parses_bool_and_null() {
        assert_eq!(parse(&json!({"type": "bool", "value": true})).unwrap(), LogicalValue::Bool(true));
        assert_eq!(parse(&json!({"type": "null"})).unwrap(), LogicalValue::Null);
    }

    #[test]
    fn parses_array() {
        let v = parse(&json!({"type": "array", "items": [
            {"type": "int", "value": "1"},
            {"type": "int", "value": "2"}
        ]})).unwrap();
        assert_eq!(v, LogicalValue::Array(vec![
            LogicalValue::Int("1".to_string()),
            LogicalValue::Int("2".to_string()),
        ]));
    }

    #[test]
    fn parses_map() {
        let v = parse(&json!({"type": "map", "entries": [
            [{"type": "text", "value": "a"}, {"type": "int", "value": "1"}]
        ]})).unwrap();
        assert_eq!(v, LogicalValue::Map(vec![
            (LogicalValue::Text("a".to_string()), LogicalValue::Int("1".to_string())),
        ]));
    }

    #[test]
    fn parses_tag() {
        let v = parse(&json!({"type": "tag", "tag": 100, "value": {"type": "int", "value": "5"}})).unwrap();
        assert_eq!(v, LogicalValue::Tag(100, Box::new(LogicalValue::Int("5".to_string()))));
    }

    #[test]
    fn parses_bignum() {
        let v = parse(&json!({"type": "bignum", "sign": "positive", "value": "18446744073709551616"})).unwrap();
        assert_eq!(v, LogicalValue::Bignum { sign: "positive".to_string(), value: "18446744073709551616".to_string() });
    }

    #[test]
    fn rejects_unknown_type() {
        assert!(parse(&json!({"type": "nonsense"})).is_err());
    }

    #[test]
    fn rejects_missing_type() {
        assert!(parse(&json!({})).is_err());
    }
}
