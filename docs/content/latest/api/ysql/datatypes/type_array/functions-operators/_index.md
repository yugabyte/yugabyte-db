---
title: SQL functions and operators for arrays
linkTitle: Functions and operators
headerTitle: Builtin SQL functions and operators for arrays
description: Bla bla
image: /images/section_icons/api/ysql.png
menu:
  latest:
    identifier: array-functions-operators
    parent: api-ysql-datatypes-array
    weight: 30
isTocNested: false
showAsideToc: false
---
Most of the functions and operators listed here can use an array of any dimensionality. But, among these, three functions work only on a one-dimensional array. This property is called out by the third column, _"1-d only?_ in the tables—and the restricted status is indicated by  _"1-d"_ in that function's row. When the field is black, there is no dimensionality restriction.

## Functions for creating arrays from scratch 

The `array[]` constructor and the three functions create an array from scratch, using—when required—non-array inputs.

| Function or operator | Description | 1-d only? |
| ---- | ---- | ---- |
| [array[]](./../array-constructor/) | The array[] constructor creates an array from a list of SQL expressions. Such an expression can itself use the `array[]` constructor. | |
| [array_fill()](./array-fill/) | Returns a new "blank canvas" array of the specified shape with all cells set to the same specified value. | |
| [array_agg()](./array-agg-unnest/#array-agg) | Returns an array (of an implied _"row"_ type) from a SQL subquery. | |
| [string_to_array()](./string-to-array/) | Returns a text[] array by splitting the input string into substrings using the specied delimiter character(s). Optionally allows a specified substring to be interpreted as NULL. | |

## Functions for reporting the geometric properties of an array

| Function | Description | 1-d only? |
| ---- | ---- | ---- |
| [array_ndims()](./properties/#array-ndims) | Returns the dimensionality of the specified array. | |
| [array_lower()](./properties/#array-lower) | Returns the lower bound of the specified array along the specified dimension. | |
| [array_upper()](./properties/#array-upper) | Returns the upper bound of the specified array along the specified dimension. | |
| [array_length()](./properties/#array-length) | Returns the length of the specified array along the specified dimension. | |
| [cardinality()](./properties/#cardinality) | Returns the total number of values in the specified array. | || [array_dims()](./properties/#array-dims) | Returns a text representation of the same information as “array_lower()" and “array_length()" return, for all dimension in a single text value. | |
| [array_dims()](./properties/#array-dims) | Returns a text representation of the same information as `array_lower()` and `array_length()` return, for all dimensions, in a single text value. | |

## Functions to find a value in an array

| Function | Description | 1-d only? |
| ---- | ---- | ---- |
| [array_position()](./array-position/#array-position) | Returns the index, in the supplied array, of the specified value. Optionally starts searching at the specified index. | 1-d |
| [array_positions()](./array-position/#array-positions) | Returns the indexes, in the supplied array, of all occurrences the specified value. | 1-d |

## Operators for comparing two arrays

These operators require that the [LHS and RHS](https://en.wikipedia.org/wiki/Sides_of_an_equation) arrays have the same datatype.

| Operator | Description | 1-d only? |
| ---- | ---- | ---- |
| [=](./comparison/#the-and-operator) | Returns `true` if the LHS and RHS arrays are equal. | |
| [<>](./comparison/#the-and-operator) | Returns `true` if the LHS and RHS arrays are not equal. | |
| [>](./comparison/#the-and-operators) | Returns `true` if the LHS array is greater than the RHS array. | |
| [>=](./comparison/#the-and-operators) | Returns `true` if the LHS array is greater than or equal to the RHS array. | |
| [<=](./comparison/#the-and-operators) | Returns `true` if the LHS array is less than or equal to the RHS array. | |
| [<](./comparison/#the-and-operators) | Returns `true` if the LHS array is less than the RHS array. | |
| [@>](./comparison/#the-and-operators-1) | Returns `true` if the LHS array contains the RHS array—i.e. if every distinct value in the RHS array is found among the LHS array's distinct values. | |
| [<@](./comparison/#the-and-operators-1) | Returns `true` if the LHS array is contained by the RHS array—i.e. if every distinct value in the LHS array is found among the RHS array's distinct values. | |
| [&&](./comparison/#the-operator) | Returns `true` if the LHS and RHS arrays overlap — i.e. if they have at least one value in common. | |


## The slice operator

| Operator | Description | 1-d only? |
| ---- | ---- | ---- |
|[[lb1:ub1]...[lbN:ubN]](./slice-operator/) | Returns a new array whose length is defined by specifying the slice's lower and upper bound along each dimnension. These spefified slicing bounds must not excede the source array's bounds. The new array has the same dimensionality as the source array and its lower bound is 1 on each axis. | |

## Functions and operators for concatenating an array with an array or with an element

These functions require that the two arrays have the same datatype and compatible dimensionality.

| Function or operator | Description | 1-d only? |
| ---- | ---- | ---- |
| [&#124;&#124;](./concatenation/#the-operator) | Returns the concatenation of any number of compatible `anyarray` and `anyelement` values. | |
| [array_cat()](./concatenation/#array-cat) | Returns the concatenation of two compatible `anyarray`  values. | |
| [array_append()](./concatenation/#array-append) | Returns an array that results from appending a scalar value to (i.e. _after_) an array value. | |
| [array_prepend()](./concatenation/#array-prepend) | Returns an array that results from prepending a scalar value to (i.e. _before_) an array value. | |

## Functions and operators to change values in an existing array

| Function or operator | Description | 1-d only? |
| ---- | ---- | ---- |
| [array_replace()](./replace-a-value/#array-replace) | Returns a new array where every occurrence of the specified value in the input array has been replaced by the specified new value. | |
| [arr[idx_1]...[idx_N] := val](./replace-a-value/#setting-an-array-value-directly) | Update a value in an existing array "in place". | |
| [array_remove()](./array-remove) | Returns a new array where _every_ occurrence of the specified value has been removed from the specified input array. | 1-d |

## Function to convert an array to a text value

| Function | Description | 1-d only? |
| ---- | ---- | ---- |
| [array_to_string()](./array-to-string) | Bla. | |

## Table function to transform an array into a "SETOF anyelement"

| Function | Description | 1-d only? |
| ---- | ---- | ---- |
| [unnest()](./array-agg-unnest/#unnest) | Use in the "from" clause of a "select" statement". The simple overload accepts a single "anyarray" value and returns a "SETOF anyelement". The exotic overload accepts a variadic list of "anyarray" values and returns a "SETOF" with many columns where each in turn has the output of the corresponding simple overload. | |
