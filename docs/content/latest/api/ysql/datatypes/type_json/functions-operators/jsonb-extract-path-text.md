---
title: jsonb_extract_path_text() and json_extract_path_text() [JSON]
headerTitle: jsonb_extract_path_text() and json_extract_path_text()
linkTitle: jsonb_extract_path_text() and json_extract_path_text()
description: Provide identical functionality to the "#>>" operator.
menu:
  latest:
    identifier: jsonb-extract-path-text
    parent: functions-operators
    weight: 140
isTocNested: true
showAsideToc: true
---

**Purpose:** Provide the identical functionality to the `#>>` operator.

**Signature** For the `jsonb` variant:

```
input value:       jsonb, VARIADIC text
return value:      text
```

**Notes:** The result of `jsonb_extract_path_text()` bears the same relationship to the result of its `jsonb_extract_path()` counterpart as does the result of the `#>>` operator to that of its `#>` counterpart. For the same reason that there is no reason to prefer`jsonb_extract_path()` over `#>`, there is no reason to prefer `jsonb_extract_path_text()` over `#>>`.
