# pg\_hint\_plan 1.9

`pg_hint_plan` makes it possible to tweak PostgreSQL execution plans using
so-called "hints" in SQL comments, like `/*+ SeqScan(a) */`.

PostgreSQL uses a cost-based optimizer, that uses data statistics, not static
rules.  The planner (optimizer) estimates costs of each possible execution
plans for a SQL statement, then executes the plan with the lowest cost.
The planner does its best to select the best execution plan, but it is far
from perfect, since it may not count some data properties, like correlation
between columns.

Copyright and license information can be found in the files COPYRIGHT and
COPYRIGHT.postgresql.

For more details, please see the various documentations available in the
**docs/** directory:

1. [Description](docs/description.md)
2. [The hint table](docs/hint_table.md)
3. [Installation](docs/installation.md)
4. [Uninstallation](docs/uninstallation.md)
5. [Details in hinting](docs/hint_details.md)
6. [Errors](docs/errors.md)
7. [Functional limitations](docs/functional_limitations.md)
8. [Requirements](docs/requirements.md)
9. [Hints list](docs/hint_list.md)
