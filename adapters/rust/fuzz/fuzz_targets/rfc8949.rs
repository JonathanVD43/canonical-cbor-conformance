#![no_main]
use arbitrary::Unstructured;
use libfuzzer_sys::fuzz_target;

fuzz_target!(|data: &[u8]| {
    let mut u = Unstructured::new(data);
    if let Ok(value) = adapter_fuzz::generate(&mut u) {
        // Coverage-guided input selection only: this crate's own encoder is
        // never the grader. The independent oracle (oracle/rfc8949_oracle.py)
        // supplies each surviving candidate's expected bytes offline.
        let _ = adapter::rfc8949::encode(&value);
    }
});
