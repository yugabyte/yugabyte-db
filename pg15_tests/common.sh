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
_yb_ctl_cmd_pre=(
  bin/yb-ctl
  --data_dir "$data_dir"
)
_yb_ctl_cmd_post=(
  --ip_start "$ip_start"
)
test_result_dir=build/latest/pg15_tests
# This assumes latest symlink is already present/correct.
mkdir -p "$test_result_dir"

# Output test result into results_file.  If result is a failure, copy the
# test_output_path with date suffix for preservation (so it doesn't get
# overwritten).
handle_test_result() {
  test_output_path=$1
  test_descriptor=$2
  result=$3
  results_file=$4
  datetime=$(date -Iseconds)

  # In case of failure, persist failure output.
  if [ "$result" -ne 0 ]; then
    cp "$test_output_path" "$test_output_path"."$datetime"
  fi

  # Output tsv row: date, test, exit code
  echo -e "$datetime\t$test_descriptor\t$result" | tee -a "$test_result_dir/$results_file"
}

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

# Assume subcommand is first argument, node index (if applicable) is second
# argument.
yb_ctl() {
  subcommand=$1
  case "$subcommand" in
    create|restart|start|wipe_restart)
      "${_yb_ctl_cmd_pre[@]}" "$@" "${_yb_ctl_cmd_post[@]}"
      # In case of rf > 1, wait for ysqlsh using the first node.
      _wait_for_ysqlsh
      ;;
    restart_node|start_node)
      # yb-ctl bug: thinks there's an issue if some tservers don't exist.
      "${_yb_ctl_cmd_pre[@]}" "$@" "${_yb_ctl_cmd_post[@]}" || true

      # Wait for ysqlsh only if a tserver was started.
      node_idx=$2
      if [[ "$*" != *' --master'* ]]; then
        _wait_for_ysqlsh "$node_idx"
      fi
      ;;
    destroy|stop_node)
      "${_yb_ctl_cmd_pre[@]}" "$@"
      ;;
    *)
      # Unimplemented.
      return 1
      ;;
  esac
}

# wipe_restart doesn't work when changing rf, so use destroy + create as an
# alternative.
yb_ctl_destroy_create() {
  yb_ctl destroy
  yb_ctl create "$@"
}

# yb-ctl can return before ysqlsh is able to connect, so use this function to
# wait until ysqlsh is ready for use.
_wait_for_ysqlsh() {
  for _ in {1..30}; do
    if ysqlsh "$@" -c ';'; then
      return 0
    fi
    sleep 1
  done
  return 1
}

# If first argument is an index, consume it and treat it as the node to connect
# to.  Remaining args are passed through to ysqlsh.
ysqlsh() {
  host_args=()
  if [ $# -gt 0 ]; then
    maybe_idx=$1
    if grep -Eq '^[0-9]$' <<<"$maybe_idx"; then
      host_args=(
        -h
        127.0.0.$((ip_start + maybe_idx - 1))
      )
      shift
    fi
  fi

  # -X: ignore psqlrc
  # -v "ON_ERROR_STOP=1": on error, return bad exit code
  # "$@": user-supplied extra args
  bin/ysqlsh -X -v "ON_ERROR_STOP=1" "${host_args[@]}" "$@"
}
