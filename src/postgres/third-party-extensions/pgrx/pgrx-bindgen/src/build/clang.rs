use super::target_env_tracked;
use bindgen::ClangVersion;
use clang_sys::support::Clang as ClangSys;
use std::{ffi::OsStr, path::PathBuf};
use walkdir::{DirEntry, WalkDir};

/// pgrx's bindgen needs to detect include paths, to keep code building,
/// but the way rust-bindgen does it breaks on Postgres 16 due to code like
/// ```c
/// #include <emmintrin.h>
/// ```
/// This will pull in builtin headers, but rust-bindgen uses a $CLANG_PATH lookup from clang-sys
/// which is not guaranteed to find the clang that uses the $LIBCLANG_PATH that bindgen intends.
///
/// Returns the set of paths to include.
pub(crate) fn detect_include_paths_for(
    preferred_clang: Option<&std::path::Path>,
) -> (bool, Vec<PathBuf>) {
    if target_env_tracked("PGRX_BINDGEN_NO_DETECT_INCLUDES").is_some() {
        return (false, vec![]);
    }

    // By asking bindgen for the version, we force it to pull an appropriate libclang,
    // allowing users to override it however they would usually override bindgen.
    let clang_major = match bindgen::clang_version() {
        ClangVersion { parsed: Some((major, _)), full } => {
            eprintln!("Bindgen found {full}");
            major
        }
        ClangVersion { full, .. } => {
            // If bindgen doesn't know what version it has, bail and hope for the best.
            eprintln!("Bindgen failed to parse clang version: {full}");
            return (true, vec![]);
        }
    };

    // If Postgres is configured --with-llvm, then it may have recorded a CLANG to use
    // Ask if there's a clang at the path that Postgres would use for JIT purposes.
    // Unfortunately, the responses from clang-sys include clangs from far-off paths,
    // so we can only use clangs that match bindgen's libclang major version.
    if let Some(ClangSys { path, version: Some(v), c_search_paths, .. }) =
        ClangSys::find(preferred_clang, &[])
    {
        if Some(&*path) == preferred_clang && v.Major as u32 == clang_major {
            return (false, c_search_paths.unwrap_or_default());
        }
    }

    // Oh no, still here?
    // Let's go behind bindgen's back to get libclang's path
    let libclang_path =
        clang_sys::get_library().expect("libclang should have been loaded?").path().to_owned();
    eprintln!("found libclang at {}", libclang_path.display());
    // libclang will probably be in a dynamic library directory,
    // which means it will probably be adjacent to its headers, e.g.
    // - "/usr/lib/libclang-${CLANG_MAJOR}.so.${CLANG_MAJOR}.${CLANG_MINOR}"
    // - "/usr/lib/clang/${CLANG_MAJOR}/include"
    let clang_major_fmt = clang_major.to_string();
    let mut paths = vec![];
    // by adjacent, that does not mean it is always immediately so, e.g.
    // - "/usr/lib/x86_64-linux-gnu/libclang-${CLANG_MAJOR}.so.${CLANG_MAJOR}.${CLANG_MINOR}.${CLANG_SUBMINOR}"
    // - "/usr/lib/clang/${CLANG_MAJOR}/include"
    // or
    // - "/usr/lib64/libclang-${CLANG_MAJOR}.so.${CLANG_MAJOR}.${CLANG_MINOR}.${CLANG_SUBMINOR}"
    // - "/usr/lib/clang/${CLANG_MAJOR}/include"
    // so, crawl back up the ancestral tree
    for ancestor in libclang_path.ancestors() {
        paths = WalkDir::new(ancestor)
            .min_depth(1)
            .max_depth(6)
            .sort_by_file_name()
            .into_iter()
            // On Unix-y systems this will be like "/usr/lib/clang/$CLANG_MAJOR/include"
            // so don't even descend if the directory doesn't have one of those parts
            .filter_entry(|entry| {
                !is_hidden(entry) && {
                    entry_contains(entry, "clang")
                        || entry_contains(entry, "include")
                        || entry_contains(entry, &clang_major_fmt)
                        // we always want to descend from a lib dir, but only one step
                        // as we don't really want to search all of /usr/lib's subdirs
                        || os_str_contains(entry.file_name(), "lib")
                }
            })
            .filter_map(|e| e.ok()) // be discreet
            // We now need something that looks like it actually satisfies all our constraints
            .filter(|entry| {
                entry_contains(entry, &clang_major_fmt)
                    && entry_contains(entry, "clang")
                    && entry_contains(entry, "include")
            })
            // we need to pull the actual directories that include the SIMD headers
            .filter(|entry| {
                os_str_contains(entry.file_name(), "emmintrin.h")
                    || os_str_contains(entry.file_name(), "arm_neon.h")
            })
            .filter_map(|entry| {
                let mut pbuf = entry.into_path();
                if pbuf.pop() && pbuf.is_dir() && os_str_contains(pbuf.file_name()?, "include") {
                    Some(pbuf)
                } else {
                    None
                }
            })
            .collect::<Vec<_>>();

        if !paths.is_empty() {
            paths.sort();
            paths.dedup();
            break;
        }
    }
    // If we have anything better to recommend, don't autodetect!
    let autodetect = paths.is_empty();
    eprintln!("Found include dirs {paths:?}");
    (autodetect, paths)
}

fn is_hidden(entry: &DirEntry) -> bool {
    entry.file_name().to_str().map(|s| s.starts_with('.')).unwrap_or(false)
}

fn entry_contains(entry: &DirEntry, needle: &str) -> bool {
    entry.path().components().any(|part| os_str_contains(part.as_os_str(), needle))
}

fn os_str_contains(os_s: &OsStr, needle: &str) -> bool {
    os_s.to_str().filter(|part| part.contains(needle)).is_some()
}
