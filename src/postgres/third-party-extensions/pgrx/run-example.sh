function exec_example() {
  CARGO_TARGET_DIR="$(pwd)/target" cargo test --manifest-path="${1}"/Cargo.toml  --features "pg${PG_VER:-14}" --no-default-features
}
if [ $1 = "all" ]; then
  for example in pgrx-examples/*; do
      if [ -d $example ]; then
        exec_example "$example"
      fi
  done
else
  exec_example "pgrx-examples/$1"
fi
