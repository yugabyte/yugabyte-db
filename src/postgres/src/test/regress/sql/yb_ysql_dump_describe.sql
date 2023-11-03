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