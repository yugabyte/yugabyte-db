-- tables
 \d
-- tablespaces
 \db
-- users
 \du
-- namespaces
 \dn
-- tablegroups
 \dgr+
-- tablegroups & tables
 \dgrt+
-- alter with add constraint using index
 \d p1
 \d p2
 SELECT yb_get_range_split_clause('c1'::regclass);
 SELECT num_tablets FROM yb_table_properties('c2'::regclass);
 -- indexes
 select * from pg_indexes where schemaname != 'pg_catalog';
 -- inheritance
\d level0
\d level1_0
\d level1_1
\d level2_0
\d level2_1
SELECT tableoid::regclass, * from level0 ORDER by c1;
SELECT tableoid::regclass, * from level1_0 ORDER by c1;
SELECT tableoid::regclass, * from level1_1 ORDER by c1;
