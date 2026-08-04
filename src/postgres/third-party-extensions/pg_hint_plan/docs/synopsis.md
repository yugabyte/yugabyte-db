# Synopsis

`pg_hint_plan` makes it possible to tweak PostgreSQL execution plans using
"hints" in SQL comments, as of `/*+ SeqScan(a) */`.

PostgreSQL uses a cost-based optimizer, which utilizes data statistics, not
static rules.  The planner (optimizer) estimates costs of each possible
execution plans for a SQL statement then the execution plan with the lowest
cost is executed.  The planner does its best to select the best execution
plan, but is not always perfect, since it doesn't take into account some of
the data properties or correlations between columns.
