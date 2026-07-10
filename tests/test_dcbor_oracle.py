from oracle.dcbor_oracle import encode_dcbor


def test_zero_unification_int_and_float_and_negative_zero_all_same():
    int_zero = encode_dcbor({"type": "int", "value": "0"})
    float_zero = encode_dcbor({"type": "float", "width": "auto", "value": "0.0"})
    neg_zero = encode_dcbor({"type": "float", "width": "auto", "value": "-0.0"})
    assert int_zero == bytes.fromhex("00")
    assert float_zero == bytes.fromhex("00")
    assert neg_zero == bytes.fromhex("00")


def test_numeric_reduction_whole_float_becomes_int():
    assert encode_dcbor({"type": "float", "width": "auto", "value": "2.0"}) == bytes.fromhex("02")


def test_fractional_float_not_reduced_uses_shortest_form():
    assert encode_dcbor({"type": "float", "width": "auto", "value": "2.5"}) == bytes.fromhex("f94100")


def test_nfc_normalization():
    decomposed = {"type": "text", "value": "café"}  # e + combining acute
    precomposed_bytes = encode_dcbor(decomposed)
    assert precomposed_bytes == bytes.fromhex("6563616 6c3a9".replace(" ", ""))


def test_nan_canonical_payload():
    assert encode_dcbor({"type": "float", "width": "auto", "value": "NaN"}) == bytes.fromhex("f97e00")
