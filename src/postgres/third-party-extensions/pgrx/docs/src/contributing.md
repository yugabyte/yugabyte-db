# Contributing

- [PGRX Internals](./contributing/pgrx-internal.md)
- [Releases](./contributing/release.md)

## Dev Environment Setup

System Requirements:
- Rustup (or equivalent toolchain manager like Nix). Users may be able to use distro toolchains, but you won't get far without a proper Rust toolchain manager.
- Otherwise, same as the user requirements.

If you want to be ready to open a PR, you will want to run
```bash
git clone --branch develop "https://github.com/pgcentralfoundation/pgrx"
cd pgrx
```
That will put you in a cloned repository with the *develop* branch opened,
which is the one you will be opening pull requests against in most cases.

After cloning the repository, mostly you can use similar flows as in the README.
However, if there are any differences in `cargo pgrx` since the last release, then
the first and most drastic difference in the developer environment vs. the
user environment is that you will have to run

```bash
cargo install cargo-pgrx --path ./cargo-pgrx \
    --force \
    --locked # This forces usage of the repo's lockfile
cargo pgrx init # This might take a while. Consider getting a drink.
```

## Pull Requests (PRs)

- Pull requests for new code or bugfixes should be submitted against develop
- All pull requests against develop will be squashed on merge
- Tests are *expected* to pass before merging
- PGRX tests PRs on rustfmt so please run `cargo fmt` before you submit
- Diffs in Cargo.lock should be checked in
- HOWEVER, diffs in the bindgen in `pgrx-pg-sys/src/pg*.rs` should **not** be checked in (this is a release task)

### Small Diffs? Big PRs?

In general, it is better to land smaller diffs in pull requests, for making them easy to review.
However, the pgrx repo is no stranger to "big PRs". This is often because a smaller PR may be
unjustified or nonfunctional on its own.

Further, large cleanup diffs that merely move around code, format it, or apply lints should be
landed independently, to allow use of `.git-blame-ignore-revs`. Even these, however, are best if
they are restrained to a single crate, or a logically-connected set, if they involve altering
anything that could conceivably affect the code's functionality.

### Adding Dependencies

If a new crate dependency is required for a pull request, and it can't or should not be marked optional and behind some kind of feature flag, then it should have its reason for being used stated in the Cargo.toml it is added to. This can be "as a member of a category", in the case of e.g. error handling:

```toml
# error handling and logging
eyre = "0.6.8"
thiserror = "1.0"
tracing = "0.1.34"
tracing-error = "0.2.0"
```

It can be as notes for the individual dependencies:
```toml
once_cell = "1.10.0" # polyfill until std::lazy::OnceCell stabilizes
```

Or it can be both:

```toml
# exposed in public API
atomic-traits = "0.3.0" # PgAtomic and shmem init
bitflags = "1.3.2" # BackgroundWorker
bitvec = "1.0" # processing array nullbitmaps
```

You do not need exceptional justification notes in your PR to justify a new dependency as your code will, in most cases, self-evidently justify the use of the dependency. PGRX uses the normal Rust approach of using dependencies based on their ability to improve correctness and make features possible. It does not reimplement things already available in the Rust ecosystem unless the addition is trivial (do not add custom derives to save 5~10 lines of code in one site) or the ecosystem crates are not compatible with Postgres (unfortunately common for Postgres data types).

## Releases

On a new PGRX release, *develop* will be merged to *master* via merge commit.
<!-- it's somewhat ambiguous whether we do this for stable or also "release candidate" releases -->

### Release Candidates AKA Betas
PGRX prefers using `x.y.z-{alpha,beta}.n` format for naming release candidates,
starting at `alpha.0` if the new release candidate does not seem "feature complete",
or at `beta.0` if it is not expected to need new feature work. Remember that `beta` will supersede `alpha` in versions for users who don't pin a version.

Publishing PGRX is somewhat fraught, as all the crates really are intended to be published together as a single unit. There's no way to do a "dry run" of publishing multiple crates. Thus, it may be a good idea, when going from `m.n.o` to `m.n.p`, to preferentially publish `m.n.p-beta.0` instead of `m.n.p`, even if you are reasonably confident that **nothing** will happen.

### Checklist
Do this *in order*:
- [ ] Inform other maintainers of intent to publish a release
- [ ] Assign an appropriate value to `NEW_RELEASE_VERSION`
- [ ] Draft release notes for `${NEW_RELEASE_VERSION}`
- [ ] Run `./update-versions.sh "${NEW_RELEASE_VERSION}"`
    - This will update the visible bindings of `pgrx-pg-sys/src/pg*.rs`
    - The visible bindings are for reference, [docs][pgrx@docs.rs], and tools
    - Actual users of the library rebuild the bindings from scratch
- [ ] Run `./upgrade-deps.sh`
- [ ] Push the resulting diffs to *develop*
- [ ] Run `./publish.sh` to push the new version to [pgrx@crates.io]
    - If there was an error during publishing:
    - [ ] fix the error source
    - [ ] push any resulting diffs to *develop*
    - [ ] increment the patch version
    - [ ] try again

**Your work isn't done yet** just because it's on [crates.io], you also need to:
- [ ] *Switch* to using **merge commits** in this PR
- [ ] Merge
- [ ] Publish release notes
- [ ] Celebrate

## Licensing

You agree that all code you submit in pull requests to https://github.com/pgcentralfoundation/pgrx/pulls
is offered according to the MIT License, thus may be freely relicensed and sublicensed,
and that you are satisfied with the existing copyright notice as of opening your PR.

[crates.io]: https://crates.io
[pgrx@crates.io]: https://crates.io/crates/pgrx
[pgrx@docs.rs]: https://docs.rs/pgrx/latest/pgrx
