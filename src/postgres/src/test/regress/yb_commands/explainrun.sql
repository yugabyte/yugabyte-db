-- A helper script to explain and run a query.  Use in regress tests by
-- defining :explain, :query, and optionally :hint#.
\set explain1 ':explain :hint1 :query;'
\set explain2 ':explain1 :explain :hint2 :query;'
\set explain3 ':explain2 :explain :hint3 :query;'
\set explain4 ':explain3 :explain :hint4 :query;'
\set explain5 ':explain4 :explain :hint5 :query;'
\set explain1run1 ':explain1; :hint1 :query;'
\set explain2run2 ':explain2; :hint1 :query; :hint2 :query;'
\set explain3run3 ':explain3; :hint1 :query; :hint2 :query; :hint3 :query;'
\set explain4run4 ':explain4; :hint1 :query; :hint2 :query; :hint3 :query; :hint4 :query;'
\set explain5run5 ':explain5; :hint1 :query; :hint2 :query; :hint3 :query; :hint4 :query; :hint5 :query;'

-- Default to no hints.
\set hint1 ''
\set hint2 ''
\set hint3 ''
\set hint4 ''
\set hint5 ''
