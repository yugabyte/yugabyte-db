---
title: string_to_array()
linkTitle: string_to_array()
headerTitle: string_to_array()
description: Bla bla
menu:
  latest:
    identifier: string-to-array
    parent: array-functions-operators
isTocNested: false
showAsideToc: false
---

To_Do Try this:

```postgresql
select string_to_array('xx~^~yy~^~zz', '~^~', 'yy');
select pg_typeof(string_to_array('xx~^~yy~^~zz', '~^~', 'yy')); -->> text[]
```



