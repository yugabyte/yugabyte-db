# Synopsis

`pg_hint_plan` makes it possible to tweak PostgreSQL execution plans using
so-called "hints" in SQL comments, like `/*+ SeqScan(a) */`.

PostgreSQL uses a cost-based optimizer, which utilizes data statistics, not
static rules. The planner (optimizer) esitimates costs of each possible
execution plans for a SQL statement then the execution plan with the lowest
cost finally be executed. The planner does its best to select the best
execution plan, but is not always perfect, since it doesn't count some
properties of the data, for example, correlation between columns.
