// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.

//! Typed gflags for Rust.
//!
//! gflags stays the only flag registry in the process, because the C++ libraries parse the same
//! command line through it and `/varz`, `SetFlag`, flagfiles and the flags XML read only gflags.
//!
//! - [`FlagRef`] binds a flag that is already defined, in C++ or in Rust, by name.
//! - [`yb_flag!`] declares a new flag. Its definition must be passed to [`register`] before gflags
//!   parses the command line; the process's C++ `main` arranges that.
//!
//! Values cross into Rust through a [`FlagBackend`]: the C++ host's in a server, an in-memory one
//! in tests.

use std::collections::HashMap;
use std::marker::PhantomData;
use std::sync::{Mutex, OnceLock};

use tokio::sync::watch;

/// Where flag values come from.
pub trait FlagBackend: Send + Sync + 'static {
    /// The current value in gflags' string form, or None if there is no such flag.
    fn get(&self, name: &str) -> Option<String>;
    /// Registers a new flag; see [`FlagDef`].
    fn define(&self, def: &FlagInfo) -> Result<(), String>;
    /// Calls `on_change` with the new value every time the flag is set, and once now.
    fn watch(&self, name: &str, on_change: Box<dyn Fn(&str) + Send + Sync>) -> Result<(), String>;
}

static BACKEND: OnceLock<Box<dyn FlagBackend>> = OnceLock::new();

/// Installs the backend. Called once, at process start.
pub fn set_backend(backend: Box<dyn FlagBackend>) {
    if BACKEND.set(backend).is_err() {
        panic!("the flag backend is already set");
    }
}

fn backend() -> &'static dyn FlagBackend {
    BACKEND
        .get()
        .expect("no flag backend: call yb_flags::set_backend first")
        .as_ref()
}

/// A type gflags can hold.
pub trait FlagValue: Clone + Send + Sync + 'static {
    /// gflags' name for the type.
    const TYPE_NAME: &'static str;
    /// Parses gflags' string form.
    fn parse_flag(value: &str) -> Option<Self>;
    /// Formats the value in gflags' string form.
    fn format_flag(&self) -> String;
}

macro_rules! numeric_flag_value {
    ($t:ty, $name:literal) => {
        impl FlagValue for $t {
            const TYPE_NAME: &'static str = $name;
            fn parse_flag(value: &str) -> Option<Self> {
                value.parse().ok()
            }
            fn format_flag(&self) -> String {
                self.to_string()
            }
        }
    };
}

numeric_flag_value!(i32, "int32");
numeric_flag_value!(i64, "int64");
numeric_flag_value!(u64, "uint64");
numeric_flag_value!(f64, "double");

impl FlagValue for bool {
    const TYPE_NAME: &'static str = "bool";
    fn parse_flag(value: &str) -> Option<Self> {
        match value {
            "true" | "1" => Some(true),
            "false" | "0" => Some(false),
            _ => None,
        }
    }
    fn format_flag(&self) -> String {
        self.to_string()
    }
}

impl FlagValue for String {
    const TYPE_NAME: &'static str = "string";
    fn parse_flag(value: &str) -> Option<Self> {
        Some(value.to_string())
    }
    fn format_flag(&self) -> String {
        self.clone()
    }
}

/// A typed handle to a flag defined elsewhere (or by [`yb_flag!`]).
pub struct FlagRef<T> {
    name: &'static str,
    _type: PhantomData<fn() -> T>,
}

impl<T: FlagValue> FlagRef<T> {
    /// Binds the flag named `name`. A missing flag or a type mismatch is a programming error and
    /// panics at first use.
    pub const fn new(name: &'static str) -> FlagRef<T> {
        FlagRef {
            name,
            _type: PhantomData,
        }
    }

    /// The flag's name.
    pub fn name(&self) -> &'static str {
        self.name
    }

    /// The current value. Reads gflags each time, so use [`FlagRef::subscribe`] on hot paths.
    pub fn get(&self) -> T {
        let value = backend()
            .get(self.name)
            .unwrap_or_else(|| panic!("no gflag named {}", self.name));
        T::parse_flag(&value)
            .unwrap_or_else(|| panic!("gflag {} = {value:?} is not a {}", self.name, T::TYPE_NAME))
    }

    /// A channel that always holds the current value; it changes when `SetFlag` changes the flag.
    pub fn subscribe(&self) -> watch::Receiver<T> {
        static CHANNELS: OnceLock<Mutex<HashMap<&'static str, Box<dyn std::any::Any + Send>>>> =
            OnceLock::new();
        let mut channels = CHANNELS.get_or_init(Default::default).lock().unwrap();
        if let Some(sender) = channels
            .get(self.name)
            .and_then(|s| s.downcast_ref::<watch::Sender<T>>())
        {
            return sender.subscribe();
        }
        let (sender, receiver) = watch::channel(self.get());
        let notify = sender.clone();
        let name = self.name;
        backend()
            .watch(
                name,
                Box::new(move |value| {
                    if let Some(parsed) = T::parse_flag(value) {
                        notify.send_replace(parsed);
                    }
                }),
            )
            .unwrap_or_else(|e| panic!("watching gflag {name}: {e}"));
        channels.insert(name, Box::new(sender));
        receiver
    }
}

/// A flag tag, as yb's `TAG_FLAG` (src/yb/util/flags/flag_tags.h).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FlagTag {
    /// Can be changed at runtime with `SetFlag`.
    Runtime,
    /// Not meant for most users.
    Advanced,
    /// Changing it can lose data or break the cluster.
    Unsafe,
    /// Not shown in documentation.
    Hidden,
    /// May change without notice.
    Experimental,
    /// Holds sensitive data; redacted where flags are shown.
    SensitiveInfo,
}

impl FlagTag {
    fn as_str(self) -> &'static str {
        match self {
            FlagTag::Runtime => "runtime",
            FlagTag::Advanced => "advanced",
            FlagTag::Unsafe => "unsafe",
            FlagTag::Hidden => "hidden",
            FlagTag::Experimental => "experimental",
            FlagTag::SensitiveInfo => "sensitive_info",
        }
    }
}

/// What a backend needs to register a flag.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct FlagInfo {
    /// gflags' type name.
    pub type_name: &'static str,
    /// The flag's name.
    pub name: &'static str,
    /// The default, in gflags' string form.
    pub default_value: String,
    /// The help text.
    pub help: &'static str,
    /// The source file that declares it.
    pub file: &'static str,
    /// yb flag tags.
    pub tags: Vec<&'static str>,
}

/// A flag declared in Rust. Declare with [`yb_flag!`]; register with [`register`].
pub struct FlagDef<T> {
    flag: FlagRef<T>,
    default: fn() -> T,
    help: &'static str,
    file: &'static str,
    tags: &'static [FlagTag],
}

impl<T: FlagValue> FlagDef<T> {
    #[doc(hidden)]
    pub const fn new(
        name: &'static str,
        default: fn() -> T,
        help: &'static str,
        file: &'static str,
        tags: &'static [FlagTag],
    ) -> FlagDef<T> {
        FlagDef {
            flag: FlagRef::new(name),
            default,
            help,
            file,
            tags,
        }
    }
}

impl<T> std::ops::Deref for FlagDef<T> {
    type Target = FlagRef<T>;
    fn deref(&self) -> &FlagRef<T> {
        &self.flag
    }
}

/// Type-erased [`FlagDef`], for [`register`].
pub trait AnyFlagDef: Sync {
    /// The registration data.
    fn info(&self) -> FlagInfo;
}

impl<T: FlagValue> AnyFlagDef for FlagDef<T> {
    fn info(&self) -> FlagInfo {
        FlagInfo {
            type_name: T::TYPE_NAME,
            name: self.flag.name,
            default_value: (self.default)().format_flag(),
            help: self.help,
            file: self.file,
            tags: self.tags.iter().map(|t| t.as_str()).collect(),
        }
    }
}

/// Registers flags declared in Rust with gflags. Must run before gflags parses the command line,
/// and only on the code path of the process that owns them, so another server built into the same
/// executable does not show them.
pub fn register(defs: &[&dyn AnyFlagDef]) {
    for def in defs {
        let info = def.info();
        backend()
            .define(&info)
            .unwrap_or_else(|e| panic!("registering gflag {}: {e}", info.name));
    }
}

/// Declares a flag owned by Rust code.
///
/// ```
/// yb_flags::yb_flag!(
///     /// Worker threads of the runtime; 0 means one per core.
///     pub EXAMPLE_THREADS: i32 = 0, "example_threads", "Worker threads; 0 means one per core.",
///     [yb_flags::FlagTag::Advanced]
/// );
/// assert_eq!(EXAMPLE_THREADS.name(), "example_threads");
/// ```
#[macro_export]
macro_rules! yb_flag {
    ($(#[$doc:meta])* $vis:vis $ident:ident : $t:ty = $default:expr, $name:literal, $help:literal,
     [$($tag:expr),* $(,)?]) => {
        $(#[$doc])*
        $vis static $ident: $crate::FlagDef<$t> =
            $crate::FlagDef::new($name, || $default, $help, file!(), &[$($tag),*]);
    };
}

/// A [`FlagBackend`] for tests: flags live in a map, and `set` notifies watchers like `SetFlag`.
#[derive(Default)]
pub struct FakeFlagBackend {
    state: Mutex<FakeState>,
}

type Watchers = Vec<Box<dyn Fn(&str) + Send + Sync>>;

#[derive(Default)]
struct FakeState {
    values: HashMap<String, String>,
    defined: Vec<FlagInfo>,
    watchers: HashMap<String, Watchers>,
}

impl FakeFlagBackend {
    /// Sets a flag's value, defining it if needed, and notifies watchers.
    pub fn set(&self, name: &str, value: &str) {
        let mut state = self.state.lock().unwrap();
        state.values.insert(name.to_string(), value.to_string());
        if let Some(watchers) = state.watchers.get(name) {
            for watcher in watchers {
                watcher(value);
            }
        }
    }

    /// The flags registered so far.
    pub fn defined(&self) -> Vec<FlagInfo> {
        self.state.lock().unwrap().defined.clone()
    }
}

impl FlagBackend for FakeFlagBackend {
    fn get(&self, name: &str) -> Option<String> {
        self.state.lock().unwrap().values.get(name).cloned()
    }

    fn define(&self, def: &FlagInfo) -> Result<(), String> {
        let mut state = self.state.lock().unwrap();
        if state.values.contains_key(def.name) {
            return Err("already defined".into());
        }
        state
            .values
            .insert(def.name.to_string(), def.default_value.clone());
        state.defined.push(def.clone());
        Ok(())
    }

    fn watch(&self, name: &str, on_change: Box<dyn Fn(&str) + Send + Sync>) -> Result<(), String> {
        let mut state = self.state.lock().unwrap();
        let value = state.values.get(name).cloned().ok_or("no such flag")?;
        on_change(&value);
        state
            .watchers
            .entry(name.to_string())
            .or_default()
            .push(on_change);
        Ok(())
    }
}

impl<T: FlagBackend> FlagBackend for std::sync::Arc<T> {
    fn get(&self, name: &str) -> Option<String> {
        (**self).get(name)
    }
    fn define(&self, def: &FlagInfo) -> Result<(), String> {
        (**self).define(def)
    }
    fn watch(&self, name: &str, on_change: Box<dyn Fn(&str) + Send + Sync>) -> Result<(), String> {
        (**self).watch(name, on_change)
    }
}

#[cfg(test)]
mod tests {
    use std::sync::Arc;

    use super::*;

    yb_flag!(
        /// A test flag.
        TEST_INT: i32 = 7, "test_rust_int", "A test flag.", [FlagTag::Runtime, FlagTag::Hidden]
    );
    yb_flag!(TEST_BOOL: bool = true, "test_rust_bool", "Another test flag.", []);

    // One backend per test binary, since it is process-global like gflags.
    fn fake() -> &'static Arc<FakeFlagBackend> {
        static FAKE: OnceLock<Arc<FakeFlagBackend>> = OnceLock::new();
        FAKE.get_or_init(|| {
            let fake = Arc::new(FakeFlagBackend::default());
            set_backend(Box::new(fake.clone()));
            register(&[&TEST_INT, &TEST_BOOL]);
            fake.set("cpp_defined_flag", "1500");
            fake
        })
    }

    #[test]
    fn define_and_read() {
        let fake = fake();
        let info = fake
            .defined()
            .into_iter()
            .find(|i| i.name == "test_rust_int")
            .unwrap();
        assert_eq!(info.type_name, "int32");
        assert_eq!(info.default_value, "7");
        assert_eq!(info.tags, vec!["runtime", "hidden"]);
        assert!(info.file.ends_with("lib.rs"));
        assert!(TEST_BOOL.get());
        assert_eq!(FlagRef::<i32>::new("cpp_defined_flag").get(), 1500);
    }

    #[test]
    fn subscribe_sees_changes() {
        let fake = fake();
        fake.set("test_rust_int", "7");
        let receiver = TEST_INT.subscribe();
        assert_eq!(*receiver.borrow(), 7);
        fake.set("test_rust_int", "11");
        assert_eq!(*receiver.borrow(), 11);
        assert_eq!(TEST_INT.get(), 11);
        // A second subscriber shares the channel.
        assert_eq!(*TEST_INT.subscribe().borrow(), 11);
    }

    #[test]
    #[should_panic(expected = "is not a int32")]
    fn type_mismatch_panics() {
        fake().set("not_an_int", "abc");
        FlagRef::<i32>::new("not_an_int").get();
    }

    #[test]
    fn value_formats() {
        assert_eq!(bool::parse_flag("1"), Some(true));
        assert_eq!(bool::parse_flag("yes"), None);
        assert_eq!(f64::parse_flag("0.5"), Some(0.5));
        assert_eq!(u64::parse_flag("-1"), None);
    }
}
