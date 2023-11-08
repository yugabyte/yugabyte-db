# This is sourced by each test.  It contains shared code, such as functions,
# variables.
set -euxo pipefail

# Assuming we are in the yugabyte-db repository, go to the base directory of
# the repository.
cd "$(git rev-parse --show-toplevel)"

# Pass --scb, --sp as desired.
build_cmd=(
  ./yb_build.sh
  "$@"
)
# TODO(#18234): after #18234, no need for this workaround because --sj can be
# passed to this script.
"${build_cmd[@]}"

data_dir=${YB_PG15_DATA_DIR:-/tmp/pg15_cluster_data}
ip_start=${YB_PG15_IP_START:-200}
export PGHOST=127.0.0.$ip_start
yb_ctl_cmd_pre=(
  bin/yb-ctl
  --data_dir "$data_dir"
)
yb_ctl_cmd_post=(
  --ip_start "$ip_start"
)

grep_in_java_test() {
  query=$1
  test_name=$2

  # Derive test_path using test_name.
  test_path_prefix=java/yb-pgsql/target/surefire-reports_org.yb.pgsql.
  # Two cases:
  # - TestPgRegressMisc#testPgRegressMiscSerial:
  #   ...TestPgRegressMisc__testPgRegressMiscSerial/org.yb.pgsql.TestPgRegressMisc-output.txt
  # - TestPgRegressPgMisc:
  #   ...TestPgRegressPgMisc/org.yb.pgsql.TestPgRegressPgMisc-output.txt
  if [[ "$test_name" == *#* ]]; then
    test_name_1=${test_name%#*}
    test_name_2=${test_name#*#}
    test_path=$test_path_prefix${test_name_1}__$test_name_2/org.yb.pgsql.$test_name_1-output.txt
  else
    test_path=$test_path_prefix$test_name/org.yb.pgsql.$test_name-output.txt
  fi

  grep -F "$query" "$test_path"
}

java_test() {
  test_name=$1
  expect_pass=${2:-true}

  # Return test-status XOR expect_pass-status
  # (https://stackoverflow.com/a/56703161).
  # TODO(#18234): after #18234, no need for this workaround because --sj can be
  # passed to this script.
  # shellcheck disable=SC2251
  ! "${build_cmd[@]}" --java-test "$test_name" --sj
  status_a=$?
  # shellcheck disable=SC2251
  ! "$expect_pass"
  status_b=$?
  if [ "$status_a" -ne "$status_b" ]; then
    return 1
  fi
}

yb_ctl() {
  "${yb_ctl_cmd_pre[@]}" "$@" "${yb_ctl_cmd_post[@]}"
}
