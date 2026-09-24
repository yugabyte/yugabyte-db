use std::borrow::Cow;

#[cfg(target_os = "windows")]
pub fn decode_from_bytes(output: &[u8]) -> Cow<'_, str> {
    use encoding_rs::Encoding;
    use std::sync::LazyLock;

    fn get_encoding() -> &'static Encoding {
        let acp = unsafe { winapi::um::winnls::GetACP() };

        codepage::to_encoding(acp as u16).unwrap_or(encoding_rs::UTF_8)
    }

    static ENCODING: LazyLock<&'static Encoding> = LazyLock::new(get_encoding);

    let (decoded, _, had_errors) = ENCODING.decode(output);

    if had_errors { String::from_utf8_lossy(output) } else { decoded }
}

#[cfg(not(target_os = "windows"))]
pub fn decode_from_bytes(output: &[u8]) -> Cow<'_, str> {
    String::from_utf8_lossy(output)
}
