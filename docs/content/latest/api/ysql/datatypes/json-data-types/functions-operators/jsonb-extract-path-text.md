---
title: Concatenation (`||`)
linktitle: Concatenation (`||`)
summary: Concatenation: the `||` operator
description: Concatenation: the `||` operator
menu:
  latest:
    identifier: to-jsonb
    parent: functions-operators
isTocNested: true
showAsideToc: true
---




## _jsonb_extract_path_text()_ and _json_extract_path_text()_

Here is the signature for the `jsonb` variant:

```
input value        jsonb, VARIADIC text
return value       text
```

The result `jsonb_extract_path_text()` bears the same relationship to the result of its `jsonb_extract_path()`as does the result of the `#>>` operator to that of the `#>` operator. For the same reasons that there is no reason to prefer`jsonb_extract_path()` over `#>`, there is no reason to prefer `jsonb_extract_path_text()` over `#>>`.
