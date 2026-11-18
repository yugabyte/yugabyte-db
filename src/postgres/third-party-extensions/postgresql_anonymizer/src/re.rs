///
/// # Regular Expressions
///
///
use regex::Regex;
use std::ffi::CString;
use std::sync::OnceLock;

//
// These Regex are static and should be compiled once and for all.
//
// Currently there's no straitforward way to do this. We chose to use the
// OnceLock method, which is available in std::sync
// However it is a bit more verbose than once_cell or lazy_static.
//
// https://github.com/rust-lang/regex/issues/1034#issuecomment-1629989813
// https://docs.rs/once_cell/latest/once_cell/#faq
//

//----------------------------------------------------------------------------
// Matches
//----------------------------------------------------------------------------

pub fn is_match_indirect_identifier(haystack: &str) -> bool {
    static RE: OnceLock<Regex> = OnceLock::new();
    RE.get_or_init(|| Regex::new(r"(?is)^ *(QUASI|INDIRECT) +IDENTIFIER *$").unwrap())
        .is_match(haystack)
}

pub fn is_match_masked(haystack: &str) -> bool {
    static RE: OnceLock<Regex> = OnceLock::new();
    RE.get_or_init(|| Regex::new(r"(?is)^ *MASKED *$").unwrap())
        .is_match(haystack)
}

pub fn is_match_not_masked(haystack: &str) -> bool {
    static RE: OnceLock<Regex> = OnceLock::new();
    RE.get_or_init(|| Regex::new(r"(?is)^ *NOT +MASKED *$").unwrap())
        .is_match(haystack)
}

pub fn is_match_restricted(haystack: &str) -> bool {
    static RE: OnceLock<Regex> = OnceLock::new();
    RE.get_or_init(|| Regex::new(r"(?is)^ *RESTRICTED *$").unwrap())
        .is_match(haystack)
}

pub fn is_match_trusted(haystack: &str) -> bool {
    static RE: OnceLock<Regex> = OnceLock::new();
    RE.get_or_init(|| Regex::new(r"(?is)^ *TRUSTED *$").unwrap())
        .is_match(haystack)
}

pub fn is_match_untrusted(haystack: &str) -> bool {
    static RE: OnceLock<Regex> = OnceLock::new();
    RE.get_or_init(|| Regex::new(r"(?is)^ *UNTRUSTED *$").unwrap())
        .is_match(haystack)
}

//----------------------------------------------------------------------------
// Captures
//----------------------------------------------------------------------------

pub fn capture_function(haystack: &str) -> Option<&str> {
    static RE: OnceLock<Regex> = OnceLock::new();
    let caps = RE
        .get_or_init(|| Regex::new(r"(?is)^ *MASKED +WITH +FUNCTION +(.*) *$").unwrap())
        .captures(haystack)?;
    // return the first match
    Some(caps.get(1).unwrap().as_str())
}

pub fn capture_tablesample(haystack: &str) -> Option<&str> {
    static RE: OnceLock<Regex> = OnceLock::new();
    let caps = RE
        .get_or_init(|| Regex::new(r"(?is)^ *TABLESAMPLE +(.*) *$").unwrap())
        .captures(haystack)?;
    // return the first match
    Some(caps.get(1).unwrap().as_str())
}

pub fn capture_value(haystack: &str) -> Option<&str> {
    static RE: OnceLock<Regex> = OnceLock::new();
    let caps = RE
        .get_or_init(|| Regex::new(r"(?is)^ *MASKED +WITH +VALUE +(.*) *$").unwrap())
        .captures(haystack)?;
    // return the first match
    Some(caps.get(1).unwrap().as_str())
}

/// Extract the `MASKED WHEN` condition
///
/// ```
/// # use anon::re::capture_when;
///
/// let a = capture_when("MASKED WHEN id >= 500");
/// assert_eq!(a,Some("id >= 500"));
///
/// let b = capture_when(" MASKED   WHEN      id >= 500     ");
/// assert_eq!(b,Some("id >= 500     "));
///
/// let c = capture_when("MASKE WHEN id >= 500");
/// assert_eq!(c,None);
///
/// let d = capture_when("masked when not is_admin");
/// assert_eq!(d,Some("not is_admin"))
/// ```
///
pub fn capture_when(haystack: &str) -> Option<&str> {
    static RE: OnceLock<Regex> = OnceLock::new();
    let caps = RE
        .get_or_init(|| Regex::new(r"(?is)^ *MASKED +WHEN +(.*) *$").unwrap())
        .captures(haystack)?;
    // return the first match
    Some(caps.get(1).unwrap().as_str())
}

///
/// This is a naïve replacement for SplitGUCList
///
/// https://regex101.com/r/pJI5QU/1
///
pub fn capture_guc_list(haystack: CString) -> Vec<String> {
    let hay = haystack.to_str().expect("haystack should be valid");
    static RE: OnceLock<Regex> = OnceLock::new();
    let caps_iter = RE
        .get_or_init(|| Regex::new(r"[^,(?! )]+").unwrap())
        .captures_iter(hay);

    let mut v: Vec<String> = vec![];
    for c in caps_iter {
        v.push(c.get(0).unwrap().as_str().into());
    }
    v
}

#[cfg(test)]
mod tests {
    use crate::re::*;

    #[test]
    fn test_capture_function() {
        assert_eq!(
            Some("public.foo($$x$$)"),
            capture_function("masked WITH function public.foo($$x$$)")
        );
        assert_eq!(None, capture_function("MASKED WITH public.foo($$x$$)"));
    }
    #[test]
    fn test_capture_guc_list() {
        assert_eq!(vec!["a", "b", "c"], capture_guc_list(c"a,b , c".into()));
        assert_eq!(
            vec!["a", "b", "c"],
            capture_guc_list(c"a,,,,,,,,b,c".into())
        );
        assert_eq!(
            vec!["abc", "dkeiij", "zofk355f"],
            capture_guc_list(c"abc dkeiij zofk355f".into())
        );
    }

    #[test]
    fn test_capture_tablesample() {
        assert_eq!(
            Some("SYSTEM(10)"),
            capture_tablesample("TABLESAMPLE SYSTEM(10)")
        );
        assert_eq!(
            Some("sySTEM(10)"),
            capture_tablesample(" tablesample  sySTEM(10)")
        );
        assert_eq!(None, capture_tablesample("TABLESAMPLE"));
        assert_eq!(None, capture_tablesample("TABLE SAMPLE SYSTEM(10)"));
    }

    #[test]
    fn test_capture_value() {
        assert_eq!(Some("NULL "), capture_value("MASKED  WiTH value NULL "));
    }

    #[test]
    fn test_re_indirect_identifier() {
        assert!(is_match_indirect_identifier("INDIRECT IDENTIFIER"));
        assert!(is_match_indirect_identifier("quasi identifier"));
        assert!(is_match_indirect_identifier(" QuAsI    idenTIFIER  "));
        assert!(!is_match_indirect_identifier("IDENTIFIER"));
        assert!(!is_match_indirect_identifier("quasi-identifier"));
    }

    #[test]
    fn test_regex_masked() {
        assert!(is_match_masked("MASKED"));
        assert!(is_match_masked("  MaSKeD       "));
        assert!(!is_match_masked("MAKSED"));
    }

    #[test]
    fn test_regex_masked_with_function() {
        assert!(capture_function("MASKED WITH FUNCTION public.foo()").is_some());
        assert!(capture_function(" masked  WITH funCTION bar(0,$$y$$) ").is_some());
        assert!(
            capture_function(
                " masked  WITH funCTION bar(0,
                                                                    $$y$$) "
            )
            .is_some()
        );
        assert!(capture_function("MASKED WITH FUNCTION").is_none());
        assert!(capture_function("MASKED WITH public.foo()").is_none());
    }

    #[test]
    fn test_regex_masked_with_value() {
        assert!(capture_value("MASKED WITH VALUE $$zero$$").is_some());
        assert!(capture_value(" masked  WITH vaLue  NULL ").is_some());
        assert!(capture_value("MASKED WITH VALUE").is_none());
        assert!(capture_value("MASKED WITH 0").is_none());
    }
    #[test]
    fn test_re_not_masked() {
        assert!(is_match_not_masked("NOT MASKED"));
        assert!(is_match_not_masked("NOT    MASKED"));
        assert!(is_match_not_masked("not masked"));
        assert!(is_match_not_masked(" NoT MaSkED "));
        assert!(is_match_not_masked(" NoT MaSkED "));
        assert!(!is_match_not_masked("NOTMASKED"));
    }

    #[test]
    fn test_re_restricted() {
        assert!(is_match_restricted("RESTRICTED"));
        assert!(is_match_restricted("     restricted "));
        assert!(!is_match_restricted("NOT RESTRICTED"));
        assert!(!is_match_restricted("cjzncdelncdl,c"));
    }

    #[test]
    fn test_re_trusted() {
        assert!(is_match_trusted("TRUSTED"));
        assert!(is_match_trusted("     trusted "));
        assert!(!is_match_trusted("TRUSTTED"));
        assert!(!is_match_trusted("UNTRUSTED"));
    }

    #[test]
    fn test_re_untrusted() {
        assert!(is_match_untrusted("UNTRUSTED"));
        assert!(is_match_untrusted("     untrusted "));
        assert!(!is_match_untrusted("UNTRUSTTED"));
        assert!(!is_match_untrusted("TRUSTED"));
    }
}
