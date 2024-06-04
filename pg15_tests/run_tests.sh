#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

# Input: lines formatted like
# <program> <descriptor> [<flaky>]
# - program:
#   - JAVA: descriptor is ./yb_build.sh --java-test <descriptor>
#   - SHELL: descriptor is pg15_tests/<descriptor>.sh
#   - else: descriptor is cxx test program
# - flaky: if any text is present, it means this test is <5% flaky
# Output: to build/latest/pg15_tests/results.tsv, formatted like
# <result> <datetime> <program> <descriptor> [<flaky_test_fail_rate>]

if [ -t 0 ]; then
  exec {input}< <(cat pg15_tests/passing_tests.tsv;
                  pg15_tests/get_flaky_test_specs.sh;
                  pg15_tests/get_shell_test_specs.sh;)
else
  exec {input}</dev/stdin
fi

found_failure=false
while read -r line; do
  # Parse spec.
  test_program=$(cut -f 1 <<<"$line")
  test_descriptor=$(cut -f 2 <<<"$line")
  test_flaky=$(cut -f 3 <<<"$line")

  case "$test_program" in
    JAVA)
      test_cmdline=(
        java_test
        "$test_descriptor"
      )
      ;;
    SHELL)
      [ -z "$test_flaky" ]
      [ -x pg15_tests/"$test_descriptor".sh ]
      test_cmdline=(
        pg15_tests/"$test_descriptor".sh
        "$@"
      )
      ;;
    *)
      test_cmdline=(
        cxx_test
        "$test_program"
        "$test_descriptor"
      )
      ;;
  esac

  if [ -z "$test_flaky" ]; then
    # Nonflaky.
    fail_rate=

    test_output_path="$test_result_dir"/"${test_descriptor//\//_}".txt
    # Run test, capturing out/err to file.
    set +e
    "${test_cmdline[@]}" |& tee "$test_output_path"
    result=$?
    set -e
  else
    # Flaky.

    test_output_path="$test_result_dir"/"${test_descriptor//\//_}"

    # Clean up from any old runs.
    rm -rf "$test_output_path"
    mkdir "$test_output_path"

    # Conditions for overall pass:
    # - Fails <= 3 times.
    # - Fail rate is <= 30%.
    # Examples:
    # - P
    # - F P P P
    # - F P P F P F P P P P
    max_fail_rate=0.3
    max_fails=3
    fails=0
    iter=0
    while [ "$fails" -le "$max_fails" ] \
          && { [ "$iter" -eq 0 ] \
               || [ "$(bc -l <<<"$fails / $iter > $max_fail_rate")" -eq 1 ]; }; do
      # Run test, capturing out/err to file.
      set +e
      "${test_cmdline[@]}" |& tee "$test_output_path"/"$iter".txt
      result=$?
      set -e

      if [ "$result" -ne 0 ]; then
        fails=$((fails + 1))
      fi
      iter=$((iter + 1))
    done
    # Overall result.
    if [ "$fails" -le "$max_fails" ] \
       && [ "$(bc -l <<<"$fails / $iter <= $max_fail_rate")" -eq 1 ]; then
      result=0
    else
      result=1
    fi
    fail_rate=$(bc -l <<<"scale=2; $fails / $iter")
  fi

  # Output test result into results.tsv.  If result is a failure, copy the
  # test_output_path with date suffix for preservation (so it doesn't get
  # overwritten).
  datetime=$(date -Iseconds)

  # In case of failure, persist failure output.
  if [ "$result" -ne 0 ]; then
    cp -r "$test_output_path" "$test_output_path"."$datetime"
  fi

  # Output tsv row: result, datetime, test program, test descriptor, fail rate
  echo -e "$result\t$datetime\t$test_program\t$test_descriptor\t$fail_rate" \
    | tee -a "$test_result_dir"/results.tsv

  if [ $result -ne 0 ]; then
    found_failure=true
  fi
done <&"$input"

if "$found_failure"; then
  exit 1
fi
