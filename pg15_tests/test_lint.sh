#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

cd pg15_tests

# All tests should start with "test_".
diff <(find . -name '*.sh' | grep -v '^./test_' | sort) - <<EOT
./common.sh
./run_all_tests.sh
./run_test_n_times.sh
EOT

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
done
