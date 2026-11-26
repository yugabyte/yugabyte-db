# rewrite_manip

Demonstrates PostgreSQL's `rewrite/rewriteManip.h` query tree manipulation functions exposed through `pgrx::pg_sys`.

## Functions

| Function | Description |
|----------|-------------|
| `demo_change_var_nodes(old_varno, new_varno)` | Remaps a Var node's range table reference from one index to another |
| `demo_offset_var_nodes(offset)` | Shifts a Var node's range table reference by a given offset |
| `demo_range_table_entry_used(varno, check_rt_index)` | Checks if a specific range table entry is referenced in a node tree |
| `demo_increment_var_sublevels_up(initial_level, delta)` | Increments the subquery nesting level of a Var node |

## Background

PostgreSQL's `rewriteManip.h` provides utilities for manipulating query trees during the rewrite phase. These functions operate on `Var` nodes — the internal representation of column references — and are essential for:

- Combining range tables when flattening subqueries
- Adjusting variable references after modifying FROM clauses
- Checking which range table entries are actually referenced

## Usage

```sql
-- Remap varno 1 to varno 5
SELECT rewrite_manip.demo_change_var_nodes(1, 5);
-- Returns: 5

-- Offset varno by 3 (1 + 3 = 4)
SELECT rewrite_manip.demo_offset_var_nodes(3);
-- Returns: 4

-- Check if rt_index 3 is used (varno=3, checking for 3)
SELECT rewrite_manip.demo_range_table_entry_used(3, 3);
-- Returns: true

-- Increment sublevels_up by 2
SELECT rewrite_manip.demo_increment_var_sublevels_up(0, 2);
-- Returns: 2
```

## Running Tests

```bash
cargo test --features "pg17 pg_test" -- --test-threads=1
```
