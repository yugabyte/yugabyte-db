---
title: jsonb_extract_path_text()
linkTitle: jsonb_extract_path_text() 
summary: jsonb_extract_path_text() and json_extract_path_text()
description: jsonb_extract_path_text()  and json_extract_path_text()
menu:
  latest:
    identifier: jsonb-extract-path-text
    parent: functions-operators
    weight: 140
isTocNested: true
showAsideToc: true
---

Here is the signature for the `jsonb` variant:

```
input value        jsonb, VARIADIC text
return value       text
```

The result `jsonb_extract_path_text()` bears the same relationship to the result of its `jsonb_extract_path()`as does the result of the `#>>` operator to that of the `#>` operator. For the same reasons that there is no reason to prefer`jsonb_extract_path()` over `#>`, there is no reason to prefer `jsonb_extract_path_text()` over `#>>`.
