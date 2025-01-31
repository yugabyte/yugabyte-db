---
title: Built-in functions and operators [YSQL]
headerTitle: Built-in functions and operators
linkTitle: Built-in functions and operators
description: YSQL supports all PostgreSQL-compatible built-in functions and operators.
image: /images/section_icons/api/subsection.png
menu:
  preview_api:
    identifier: api-ysql-exprs
    parent: api-ysql
    weight: 60
type: indexpage
---

YSQL supports all PostgreSQL-compatible built-in functions and operators. The following are the currently documented ones.

| Statement | Description |
|-----------|-------------|
| [yb_hash_code()](func_yb_hash_code) | Returns the partition hash code for a given set of expressions |
| [gen_random_uuid()](func_gen_random_uuid) | Returns a random UUID |
| [Sequence functions](sequence_functions/) | Functions operating on sequences |
| [Geo-partitioning helper functions](./geo_partitioning_helper_functions/) | Detailed list of geo-partitioning helper functions |
| [JSON functions and operators](../datatypes/type_json/functions-operators/) | Detailed list of JSON-specific functions and operators |
| [Array functions and operators](../datatypes/type_array/functions-operators/) | Detailed list of array-specific functions and operators |
| [Aggregate functions](./aggregate_functions/) | Detailed list of YSQL aggregate functions |
| [Window functions](./window_functions/) | Detailed list of YSQL window functions |
| [Date-time operators](../datatypes/type_datetime/operators/) | List of operators for the date and time data types |
| [General-purpose date-functions](../datatypes/type_datetime/functions/) | List of general purpose functions for the date and time data types |
| [Date-time formatting functions](../datatypes/type_datetime/formatting-functions/) | List of formatting functions for the date and time data types |
