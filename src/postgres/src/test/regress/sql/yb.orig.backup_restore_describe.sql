------------------------------------------------
-- Test unique constraint on partitioned tables.
------------------------------------------------

select conname from pg_constraint where conrelid = 'part_uniq_const'::regclass::oid;

-- Test inheritance
SELECT tableoid::regclass, * from level0 ORDER by c1;
SELECT tableoid::regclass, * from level1_0 ORDER by c1;
SELECT tableoid::regclass, * from level1_1 ORDER by c1;
