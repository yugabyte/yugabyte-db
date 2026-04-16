# Cross compiling `pgrx` (on Linux)

*Warning: this guide is still a work in progress!*

## Caveats

Note that guide is fairly preliminary and does not cover many cases, most notably:

1. This does not (yet) cover cross compiling with `cargo pgrx` (planned). Note that this means this documentation may only be useful to a small set of users.

2. Cross-compiling the `cshim` is not (yet?) supported. You should ensure that the `pgrx/cshim` cargo feature is disabled when you perform the cross-build.

3. This guide is assuming that you are cross compiling between `x86_64-unknown-linux-gnu` and `aarch64-unknown-linux-gnu` (either direction works).

    - Compiling between other architectures (but still Linux on both ends) will likely be similar, but are left as an exercise for the reader.
    - Compiling between other OSes is... considerably more difficult, and not officially supported at the moment (although it is possible with quite a bit of pain).
    - Non-`gnu` linux targets have not been investigated at all (this mostly applies to `-musl` targets like Alpine, as I doubt PG will work well under uclibc).

    Other cases are encouraged to investigate Docker, QEMU, and so on -- it's honestly probably the easier path for a lot of cases.

An additional caveat (one that is unrelated to the document's completeness) is that the cross-compilation process is highly specific to your Linux distribution, unfortunately. This guide covers Debian (where it's easy) and Fedora (where it's hard). The Debian steps should apply to any `apt`-using distribution, and most of the Fedora steps will apply to other distributions with minimal conversion.

# System reqirements

1. Postgres headers configured for the target (which we'll talk about shortly).
2. A cross compilation toolchain (which we'll also talk about shortly).
3. Everything you'd need to build things on the host (which we won't talk about, but install it).
4. A Rust toolchain for the target (which we also won't talk about).

## Getting PostgreSQL headers for the target
[getting-headers]: #getting-postgresql-headers-for-the-target

To generate the bindings for `pgrx-pg-sys` we need the PostgreSQL headers. Unfortunately, a few of these headers get generated when Postgres is compiled and contain non-portable information, which means that we can't just use the headers for the version of postgres you have installed. You need to get the server headers from a build PostgreSQL done for the cross-compile target. The version of PostgreSQL *must* match.

*Caveat: It is _critically important_ that you ensure the headers you get are for the correct version of Postgres. Failure to do so will _probably not_ be detected at build time, and will almost certainly result in an extension that fails _terribly_ at runtime. Do not mess this up!*

To minimize the chance for compatibility issues: The headers should be produced by a PostgreSQL build as close as possible to the one which will actually use the cross-compiled extension. That means they should be built with the same configuration flags, and ideally for the same Linux distribution (yes, really[^distro]). You probably also need to try to ensure that glibc and linux kernel versions match what you use for the cross-compile toolchain in the next step (an exact match would be ideal, but it is difficult on anything other than Debian).

[^distro]: Comments on PostgreSQL's mailing list [indicate](https://www.postgresql.org/message-id/1556.1486012839%40sss.pgh.pa.us) that they consider it fine if there are ABI differences between extensions built on (for example) Debian versus RHEL. In reality this seems unlikely as long as things like kernel and glibc versions are compatible (but don't blame me if that isn't true), but either way, it's an indication that you should go out of your way to ensure *everything* on the build machine and cross-compilation environment is *as similar as possible* to what it will be in the location where the extension will ultimately get installed.

One additional warning is that while currently `pgrx` doesn't emit bindings for headers which include (transitively or otherwise) headers provided by third-party dependencies, those do exist (PostgreSQL has headers which transitively include in headers from ICU, and from LLVM's C API). At the moment we don't need them, but future versions of PG could change that, so be aware. If those headers become necessary, it's very likely that the failure would be at compile time rather than runtime, so this is *mostly* something that doesn't need to be handled yet (but might be necessary when dealing with patched versions of postgres).

### Where to get the headers
[get-headers-options]: #where-to-get-the-headers

Anyway, there are a few ways you can go about getting these headers. This is not exhaustive, although some alternatives have good reasons for being left out (for example, attempting to cross-compile PostgreSQL itself to get headers is only a good idea if you're actually going to use that cross-compiled PostgreSQL):

1. If you built the version of PostgreSQL you're planning on using for the cross-compile target yourself (or a coworker you have access to did it), then use the headers from that build. They should be

    While you're at it, mark down the versions of glibc and the Linux kernel that they were built against, as that will be useful for figuring out the cross-compilation toolchain.

2. A simple approach that is unlikely to go wrong is to copy them off of an actual machine of that architecture running the same Postgres configuration you care about. This is a good option for cases like using [PL/Rust](https://github.com/tcdi/plrust)'s cross-compilation and replication support simultaneously, but it may not always be possible.

    Concretely, you want to use the headers from the folder displayed when you run `pg_config --includedir-server`. Make sure to keep track of the version output by `pg_config --version` (and make sure it matches what you expect).

3. If you know that you're planning on using Postgres installed from a package repository, you can grab the headers from there. This is a good option if you're cross-compiling an extension on the same version of the same Linux distribution, and intend to use the same package repositories to install Postgres in both cases.

    For example, on Debian something like `apt download postgresql-server-dev-$VERSION:arm64` (note that Debian uses `arm64` for `aarch64` targets, and `amd64` for `x86_64` targets) will produce a `postgresql-server-dev-$VERSION_..._arm64.deb` file in the current working directory, and extracting the headers from it (while out of scope of this guide) is hopefully straightforward. This requires that your distribution package binaries your chosen target architecture (`arm64` in the example above), which is not guaranteed (while Debian itself is quite solid about providing binaries for a several architectures, this is not true for every apt-based distribution).

    Similarly, the incantation on Fedora is something like `dnf download --forcearch aarch64 postgresql-server-devel`, which puts an a `.rpm` in the current working directory. RPM is somewhat more involved to extract (use `rpm2cpio` and `cpio` in conjunction), but entirely possible. Be very aware about the Postgres version these headers come from, since Fedora (for example) does not seem to have packages for multiple versions of PostgreSQL.

And so on, that should give you ideas.

Some dubious advice: In practice, it's possible that small differences (using a different distro's package, or using PG that was configured slightly differently) will not matter much for your extension. In some cases, it's even possible that using headers which were configured for a different architecture is won't cause issues for you -- it may even be the right choice in some situations (for example, if production builds aren't cross compiled, so the mismatch only exists during development I'd be included to say it's only a problem if it causes one).

That said, you should be well-aware of the risk when deviating from this, and I would try to get it right for anything you're going to use in production.

## Getting a cross-compilation toolchain
[get-toolchain]: #getting-a-cross-compilation-toolchain

First off, if you're on Debian (and probably other `apt`-using distributions) this should be pretty easy -- just `sudo apt install crossbuild-essential-arm64` if you want to cross-compile *to* aarch64 (*from* x86_64), and `sudo apt install crossbuild-essential-amd64` if you want to cross-compile *to* x86_64 (*from* aarch64). If that's you, you're done with this step, and can move on to the next section.

Otherwise, you're going to have to do this the hard way, which means providing a cross-compile toolchain. Unfortunately, there are some extra wrinkles, as we need bindings generated to be as identical as possible to the the ones that will be generated in non-cross settings.

In particular, this means you should try to ensure that the version of glibc and the linux headers for the cross-compilation toolchain are... reasonably close to reality.

FIXME finish rewriting the rest of this

# Distributions

Unfortunately, the cross-compilation process is quite distribution specific. We'll cover two cases:

1. Debian-based distributions, where this is not that bad.
2. Distributions where userspace cross-compilation is not directly supported (such as the Fedora-family). This is much more difficult, so if you have a choice you should not go this route.

## Debian

Of the mainstream distributions (that is, excluding things like NixOS which apparently are designed to make this easy) the easiest path available is likely to be on Debian-family systems. This is for two reasons:

1. The cross compilation tools can be installed via an easy package like `crossbuild-essential-arm64` (when targeting `aarch64`) or `crossbuild-essential-amd64` (when targeting `x86_64`)

2. The cross compilation sysroot is the same as the normal sysroot -- they're both `/`.

3. Many tools in the Rust ecosystem (the `bindgen` and `cc` crates) know where many things are located, out of the box.

And a few other aspects which are less critical (if you get the tools on Debian 11, then you know they'll run on any machine has a Debian 11 install -- no need to worry about glibc versions, for example).

### The Steps

On the steps on Debian-family are as follows:

1. Set up everything you'd need to perform non-cross builds.

2. Install a Rust toolchain for the target:
    - *`target=aarch64`*: `rustup target add aarch64-unknown-linux-gnu`.
    - *`target=x86_64`*: `rustup target add x86_64-unknown-linux-gnu`.

3. Install the `crossbuild-essential-<arch>` package for the architecture you are targeting
    - *`target=aarch64`*: `sudo apt install crossbuild-essential-arm64`.
    - *`target=x86_64`*: `sudo apt install crossbuild-essential-amd64`.

4. Set some relevant environment vars:
    - *`target=aarch64`*: `export CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER=aarch64-linux-gnu-gcc`.
    - *`target=x86_64 `*: `export CARGO_TARGET_X86_64_UNKNOWN_LINUX_GNU_LINKER=x86_64-linux-gnu-gcc`.

    Note: It's also possible to set these in your `.cargo/config.toml`, but note that they're distribution-specific (and on other distros, potentially machine-specific), so I would recommend against checking them into version control.
    ```toml
    # Replace `<arch>` with the target arch.
    [target.<arch>-linux-gnu-gcc]
    linker = "<arch>-linux-gnu-gcc"
    ```

5. Build your extension.
    - *`target=aarch64`*: `cargo build --target=aarch64-unknown-linux-gnu --release`.
    - *`target=x86_64 `*: `cargo build --target=x86_64-unknown-linux-gnu --release`.

This will produce a `.so` in `./target/<target>/release/lib$yourext.so`, which you can use.

> *TODO: this seems like it is not quite complete -- we may need things like this (when targeting `aarch64` from `x86_64`)? Needs some slightly further investigation for _why_, though, since most of this should be auto-detected (notably the target and isystem paths...)*
>
> ```sh
> export BINDGEN_EXTRA_CLANG_ARGS_aarch64-unknown-linux-gnu="-target aarch64-unknown-linux-gnu -isystem /usr/aarch64-linux-gnu/include/ -ccc-gcc-name aarch64-linux-gnu-gcc"
> ```

# Other Distributions

*Note: these steps are still somewhat experimental, and may be missing some pieces.*

The first few steps are the same as under debian.

1. Set up everything you'd need to perform non-cross builds.

2. Install a Rust toolchain for the target:
    - *`target=aarch64`*: `rustup target add aarch64-unknown-linux-gnu`.
    - *`target=x86_64`*: `rustup target add x86_64-unknown-linux-gnu`.

After this you need a cross compilation toolchain, which can be challenging.

## Get a cross-compilation toolchain

To cross compile, you need a toolchain. This is basically two parts:

- The cross-compile tools: suite of tools, scripts, libraries (and the like) which are compiled for the host architecture (so that you can run them) which are capable of building for the target. Specifically, this includes things like the compiler, linker, assembler, archiver, ... as well as any libraries they link to, and tools they invoke.

- A sysroot, which is basically an emulated unix filesystem root, with `<sysroot>/usr`, `<sysroot>/etc` and so-on. The important thing here is that it has libraries built for the target, headers configured for the target, and so on.

Pick well here, since getting a bad one may cause builds that succeed but fail at runtime.

An easy option for targeting `aarch64` (or several other architectures) from `x86_64` is to use one of the ones on <https://toolchains.bootlin.com/releases_aarch64.html> (not an endorsement: they're something I've used for development, I don't know how well-made they are, and they honestly seem kind of idiosyncratic. IOW, I'd want to do a lot more research before putting them into production).

Sadly, I don't have a good option for an easily downloaded x86_64 toolchain that has tools built for aarch64. I've been using a manually built one, which isn't covered in this guide (TODO?).

So, assuming you're getting one from the link above, you need to find a toolchain:
- Which uses glibc (not musl/uclibc), since we're compiling to a `-linux-gnu` Rust target.
- Contains a version of glibc which is:
    - Has a version of glibc which is no newer than the ones which will be used on any of the target machines.
    - But still new enough to contain any symbols you link to non-weakly (don't worry about this part unless it becomes a problem).
- Has linux headers which are similarly no newer than the version on any of the target machines. This *probably* doesn't matter for you, and it might not be a thing you have to worry about with toolchains from elsewhere (most don't contain kernel headers, since most applications don't need them to build).

If you can't find one with an old enough version of glibc, try to get as close as possible, and we'll just have to hope for the best -- there's a chance it will work still, it depends on what system functions you call in the compiled binary.

Anyway, once you have one of these you may need to put it somewhere specific -- not all of them are relocatable (the ones from `toolchains.bootlin.com` above are, however). An easy way to do this is by running `bin/aarch64-linux-gcc --print-sysroot`, and seeing if it prints the a path inside it's directory. If not, you may have to move things around so that the answer it gives is correct.

## Use the cross compilation toolchain

Continuing from above, I will assume (without loss of generality) that you're targeting aarch64, have a toolchain directory at `$toolchain_dir` and your sysroot is at `$sysroot_dir` -- try `$toolchain_dir/bin/aarch64-linux-gnu-gcc --print-sysroot`.

Anyway, set

- `CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER=$toolchain_dir/bin/aarch64-linux-gnu-gcc`
- `BINDGEN_EXTRA_CLANG_ARGS_aarch64-unknown-linux-gnu=--sysroot=\"$sysroot_dir\"`

You may also need:
- `CC_aarch64_unknown_linux_gnu=$toolchain_dir/bin/aarch64-linux-gnu-gcc`
- `AR_aarch64_unknown_linux_gnu=$toolchain_dir/bin/aarch64-linux-gnu-ar`
- `CXX_aarch64_unknown_linux_gnu=$toolchain_dir/bin/aarch64-linux-gnu-g++`
- `LD_aarch64_unknown_linux_gnu=$toolchain_dir/bin/aarch64-linux-gnu-ld`

And sometimes you may need to add `$toolchain_dir/bin` to path and set `CROSS_COMPILE=aarch64-linux-gnu-` can help. Sadly, this can break things depending on what's in your toolchain's path, or if you have a tool which doesn't check if it's actually a cross-compile before looking at the `CROSS_COMPILE` var.

TODO(thom): that's sometimes enough to complete a build but this is very WIP.

TODO(thom): flags to make the cshim build.
