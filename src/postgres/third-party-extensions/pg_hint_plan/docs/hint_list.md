(hint-list)=
# Hint list

The available hints are listed below.

| Group | Format | Description |
|:------|:-------|:------------|
| Scan method | `SeqScan(table)`| Forces sequential scan on the table. |
| | `TidScan(table)` | Forces TID scan on the table. |
| | `IndexScan(table[ index...])` | Forces index scan on the table.  Restricts to specified indexes if any. |
| | `IndexOnlyScan(table[ index...])` | Forces index-only scan on the table.  Restricts to specified indexes if any.  Index scan may be used if index-only scan is not available. |
| | `BitmapScan(table[ index...])`| Forces bitmap scan on the table.  Restricts to specified indexes if any. |
| | `IndexScanRegexp(table[ POSIX Regexp...])`<br>`IndexOnlyScanRegexp(table[ POSIX Regexp...])`<br>`BitmapScanRegexp(table[ POSIX Regexp...])` | Forces index scan, index-only scan (For PostgreSQL 9.2 and later) or bitmap scan on the table.  Restricts to indexes that matches the specified POSIX regular expression pattern. |
| | `NoSeqScan(table)`| Forces to *not* do sequential scan on the table. |
| | `NoTidScan(table)`| Forces to *not* do TID scan on the table.|
| | `NoIndexScan(table)`| Forces to *not* do index scan and index-only scan on the table. |
| | `NoIndexOnlyScan(table)`| Forces to *not* do index only scan on the table. |
| | `NoBitmapScan(table)` | Forces to *not* do bitmap scan on the table. |
| Join method| `NestLoop(table table[ table...])` | Forces nested loop for the joins on the tables specified. |
| | `HashJoin(table table[ table...])`| Forces hash join for the joins on the tables specified. |
| | `MergeJoin(table table[ table...])` | Forces merge join for the joins on the tables specified. |
| | `NoNestLoop(table table[ table...])`| Forces to *not* do nested loop for the joins on the tables specified. |
| | `NoHashJoin(table table[ table...])`| Forces to *not* do hash join for the joins on the tables specified. |
| | `NoMergeJoin(table table[ table...])` | Forces to *not* do merge join for the joins on the tables specified. |
| Join order | `Leading(table table[ table...])` | Forces join order as specified. |
| | `Leading(<join pair>)`| Forces join order and directions as specified.  A join pair is a pair of tables and/or other join pairs enclosed by parentheses, which can make a nested structure. |
| Behavior control on Join | `Memoize(table table[ table...])` | Allows the topmost join of a join among the specified tables to Memoize the inner result. Not enforced. |
| | `NoMemoize(table table[ table...])` | Inhibits the topmost join of a join among the specified tables from Memoizing the inner result. |
| Row number correction | `Rows(table table[ table...] correction)` | Corrects row number of a result of the joins on the tables specified.  The available correction methods are absolute (#<n>), addition (+<n>), subtract (-<n>) and multiplication (*<n>).  <n> should be a string that strtod() can understand. |
| Parallel query configuration | `Parallel(table <# of workers> [soft\|hard])` | Enforces or inhibits parallel execution of the specified table.  <# of workers> is the desired number of parallel workers, where zero means inhibiting parallel execution.  If the third parameter is soft (default), it just changes max\_parallel\_workers\_per\_gather and leaves everything else to the planner.  Hard enforces the specified number of workers. |
| GUC | `Set(GUC-param value)` | Sets GUC parameter to the value defined while planner is running. |
