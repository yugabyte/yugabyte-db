---
title: array_remove()
linkTitle: array_remove()
headerTitle: array_remove()
description: array_remove()
menu:
  latest:
    identifier: array-remove
    parent: array-functions-operators
isTocNested: false
showAsideToc: false
---

**Purpose:** Return a new array where _every_ occurrence of the specified value has been removed from the specified input array.

**Signature:**
```
input value:       anyarray, anyelement
return value:      anyarray
```
**Note:** This function requires the array from which values are to be removed is one-dimensional. This restriction is easily understood in light of the fact that arrays are rectilinear—in other words, the geometry of an array whose dimensionality is two or more is fixed at creation time. For examples illustrating this rule, see [`array_fill()`](.././array-fill).

**Example:**
```postgresql
create table t(k int primary key, arr int[]);
insert into t(k, arr)
values (1, '{1, 2, 2, 2, 5, 6}'::int[]);

select arr as "old value of arr" from t where k = 1;

update t
set arr = array_remove(arr, 2)
where k = 1;

select arr as "new value of arr" from t where k = 1;
```
This is the result of the two queries:
```
 old value of arr 
------------------
 {1,2,2,2,5,6}

 new value of arr 
------------------
 {1,5,6}
```

The example suffers from the problem that [GitHub Issue #4296](https://github.com/yugabyte/yugabyte-db/issues/4296) tracks. (This same issue also affects the concatenation operator and the `array_replace()` function.) If you run this example as presented, then the `UPDATE` statement causes an error that causes the client session to terminate. But, when you restart it, an attempt to execute the _"new value of arr"_ query causes an error.

This section will be updated when the issue is fixed. You can sidestep the problem, for demonstration purposes, simply by creating `table t` without a primary key constraint. But this violates proper practice. Here is a viable workaround. Simply use this `UPDATE` statement instead of the one shown above:

```postgresql
with v as (
  select array_remove(arr, 2) as new_arr from t where k = 1)
update t
set arr = (select new_arr from v)
where k = 1;
```
