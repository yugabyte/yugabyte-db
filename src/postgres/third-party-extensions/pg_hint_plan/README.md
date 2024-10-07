# pg\_hint\_plan 1.5

`pg_hint_plan` makes it possible to tweak PostgreSQL execution plans using
so-called "hints" in SQL comments, like `/*+ SeqScan(a) */`.

PostgreSQL uses a cost-based optimizer, which utilizes data statistics, not
static rules. The planner (optimizer) esitimates costs of each possible
execution plans for a SQL statement then the execution plan with the lowest
cost finally be executed. The planner does its best to select the best best
execution plan, but is not always perfect, since it doesn't count some
properties of the data, for example, correlation between columns.

For more details, please see the various documentations available in the
**docs/** directory:

1. [Description](docs/description.md)
1. [The hint table](docs/hint_table.md)
1. [Installation](docs/installation.md)
1. [Unistallation](docs/uninstallation.md)
1. [Details in hinting](docs/hint_details.md)
1. [Errors](docs/errors.md)
1. [Functional limitations](docs/functional_limitations.md)
1. [Requirements](docs/requirements.md)
1. [Hints list](docs/hint_list.md)

* * * * *

Copyright (c) 2012-2023, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
