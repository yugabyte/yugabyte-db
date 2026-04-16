#!/usr/bin/env bash

echo "---- setup rustc ----"
if [[ $(type rustup) ]]; then
  echo "rustup already installed"
else
  curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs --output rustup-init.sh
  chmod +x rustup-init.sh
  ./rustup-init.sh -y
fi
# we want nightly to be always-available for some tests, even if we don't default to it
rustup update nightly
rustup update "${RUST_TOOLCHAIN:-stable}"
rustup default "${RUST_TOOLCHAIN:-stable}"
# only needed for cross-compile tests but we want consistent rust configuration
rustup target add aarch64-unknown-linux-gnu

# output full rustc version data
rustc --version --verbose
# this determines what our cargo version is, so don't ask
