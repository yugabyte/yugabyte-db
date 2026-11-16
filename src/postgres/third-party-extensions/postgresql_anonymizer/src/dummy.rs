///
/// Call a faking function based on its name and locale
///
/// For instance:
///  dummy!(Firstname,"ar_SA") will call FirstName<AR_SA>.fake()
///
/// The list of supported locales is here
/// https://docs.rs/fake/latest/fake/locales/
///
/// We use the ISO codes for the locale instead of the fake-rs codes
///
#[macro_export]
macro_rules! dummy {
    ($struct: ident, $locale: ident ) => {
        match $locale.as_ref() {
            "ar_SA" => $struct(AR_SA).fake(),
            "en_US" => $struct(EN).fake(),
            "fr_FR" => $struct(FR_FR).fake(),
            "ja_JP" => $struct(JA_JP).fake(),
            "pt_BR" => $struct(PT_BR).fake(),
            "zh_CN" => $struct(ZH_CN).fake(),
            "zh_TW" => $struct(ZH_TW).fake(),
            _ => panic!(
                "Anon: {} is not a supported locale for {}",
                $locale,
                stringify!($struct),
            ),
        }
    };
}

/// Convert a i32 Range into a usize Range
#[macro_export]
macro_rules! range_usize {
    ( $range_i32: ident ) => {
        core::ops::Range::<usize> {
            start: *$range_i32.lower().unwrap().get().unwrap() as usize,
            end: *$range_i32.upper().unwrap().get().unwrap() as usize,
        }
    };
}

#[macro_export]
macro_rules! dummy_with_range {
    ($struct: ident, $locale: ident, $range_i32: ident ) => {
        match &$locale as &str{
//            "ar_SA" => $struct(AR_SA,crate::range_usize!($range_i32)).fake::<Vec<String>>().join(" "),
            "en_US" => $struct(EN,$crate::range_usize!($range_i32)).fake::<Vec<String>>().join(" "),
            "fr_FR" => $struct(FR_FR,$crate::range_usize!($range_i32)).fake::<Vec<String>>().join(" "),
            "ja_JP" => $struct(JA_JP,$crate::range_usize!($range_i32)).fake::<Vec<String>>().join(" "),
            "pt_BR" => $struct(PT_BR,$crate::range_usize!($range_i32)).fake::<Vec<String>>().join(" "),
            "zh_CN" => $struct(ZH_CN,$crate::range_usize!($range_i32)).fake::<Vec<String>>().join(" "),
            "zh_TW" => $struct(ZH_TW,$crate::range_usize!($range_i32)).fake::<Vec<String>>().join(" "),
            _       => panic!(  "Anon: {} is not a supported locale",
                                $locale
                        ),
        }
    }
}

/// Create a Rust binding for a function that has only a French locale
#[macro_export]
macro_rules! declare_french_fn_String {
    ($name: tt, $struct: ident ) => {
        #[pg_extern]
        pub fn $name() -> String {
            $struct(FR_FR).fake()
        }
    };
}
pub(crate) use declare_french_fn_String;

/// Create a simple Rust binding function for a given fake-rs Struct
#[macro_export]
macro_rules! declare_fn_String {
    ($name: tt, $struct: ident) => {
        #[pg_extern]
        pub fn $name() -> String {
            $struct.fake()
        }
    };
}
pub(crate) use declare_fn_String;

///
/// Create 2 Rust functions for a given **localized** fake-rs Struct
///
/// For instance:
///    the macro declare_fn_String!(dummy_first_name,Firstname)
///    will create 2 PGRX external functions:
///         * `dummy_first_name_locale(locale: &str)`
///         * `dummy_first_name()`
///    and those 2 functions will be linked to 2 SQL functions by PGRX
///         * SELECT anon.dummy_first_name_locale(locale TEXT)
///         * SELECT anon.dummy_first_name()
///
#[macro_export]
macro_rules! declare_l10n_fn_String {
    ($name: tt, $struct: ident) => {
        paste::paste! {
            #[pg_extern]
            pub fn [ < $name _locale > ](locale: String) -> String {
                dummy!($struct,locale)
            }
        }

        #[pg_extern]
        pub fn $name() -> String {
            let locale = &$crate::guc::ANON_DUMMY_LOCALE
                .get()
                .unwrap_or(c"en_us".into())
                .into_string()
                .expect("should be a string");
            dummy!($struct, locale)
        }
    };
}
pub(crate) use declare_l10n_fn_String;

#[macro_export]
macro_rules! declare_l10n_fn_with_range_to_string {
    ($name: tt, $struct: ident) => {
        paste::paste! {
            #[pg_extern]
            pub fn [ < $name _locale > ](
                    locale: String,
                    r: pgrx::Range<i32>)
                -> String {
                return $crate::dummy_with_range!($struct,locale,r);
            }
        }

        #[pg_extern]
        pub fn $name(r: pgrx::Range<i32>) -> String {
            let locale = $crate::guc::ANON_DUMMY_LOCALE
                .get()
                .unwrap_or(c"en_us".into())
                .into_string()
                .expect("should be a string");
            return $crate::dummy_with_range!($struct, locale, r);
        }
    };
}
pub(crate) use declare_l10n_fn_with_range_to_string;
