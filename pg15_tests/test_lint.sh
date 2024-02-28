#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

cd pg15_tests

# All tests should start with "test_".
diff <(find . -name '*.sh' | grep -v '^./test_' | sort) - <<EOT
./common.sh
./run_flaky_tests.sh
./run_passing_tests.sh
./run_shell_tests.sh
./run_test_n_times.sh
EOT

# flaky_tests.tsv and passing_tests.tsv.
find . -name '*.tsv' \
  | while read -r tsv; do
  # Check sorted and no duplicates.
  LC_ALL=C sort -cu "$tsv"
  # Check no spaces (should be tabs).
  if grep -q ' ' "$tsv"; then
    echo "Bad space in $tsv"
    exit 1
  fi
done

find . -name '*.sh' \
  | grep '^./test_' \
  | while read -r test_file; do
  # All tests should be executable.
  [ -x "$test_file" ]
  # All tests should start with the same three lines.
  diff <(head -3 "$test_file") - <<EOT
#!/usr/bin/env bash
source "\${BASH_SOURCE[0]%/*}"/common.sh

EOT
  # All tests besides this one should not contain "/ysqlsh", which suggests
  # running the ysqlsh executable.  Instead, they should use the ysqlsh helper
  # function defined in common.sh.
  [ "$test_file" == ./"${BASH_SOURCE[0]##*/}" ] || \
    ! grep "/ysqlsh" "$test_file"
done
