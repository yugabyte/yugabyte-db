#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

# Modified slightly due to D29444.
java_test TestPgRegressTypesGeo false
# yb_pg_point should pass.
grep_in_java_test \
  "failed tests: [yb_pg_geometry, yb_pg_line, yb_pg_polygon]" \
  TestPgRegressTypesGeo
# yb_pg_geometry should only have a small failure.
diff <(diff build/latest/postgres_build/src/test/regress/{expected,results}/yb_pg_geometry.out || true) - <<EOT
132c132
<         | (0,0)      | [(0,0),(6,6)]                 | (-0,0)
---
>         | (0,0)      | [(0,0),(6,6)]                 | (0,0)
EOT
