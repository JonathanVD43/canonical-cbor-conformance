#![no_main]
use arbitrary::Unstructured;
use libfuzzer_sys::fuzz_target;

fuzz_target!(|data: &[u8]| {
    let mut u = Unstructured::new(data);
    if let Ok(value) = adapter_fuzz::generate(&mut u) {
        // Coverage-guided input selection only: the dcbor crate's encoder is
        // never the grader. The independent Ruby cbor-dcbor-gem oracle
        // supplies each surviving candidate's expected bytes offline.
        let _ = adapter::dcbor::encode(&value);
    }
});
