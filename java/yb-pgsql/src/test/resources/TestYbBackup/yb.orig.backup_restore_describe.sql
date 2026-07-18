------------------------------------------------
-- Test unique constraint on partitioned tables.
------------------------------------------------

select conname from pg_constraint where conrelid = 'part_uniq_const'::regclass::oid;

-- Test inheritance
SELECT tableoid::regclass, * from level0 ORDER by c1;
SELECT tableoid::regclass, * from level1_0 ORDER by c1;
SELECT tableoid::regclass, * from level1_1 ORDER by c1;

-- enum type sortorder
SELECT typname, enumlabel, pg_enum.oid, enumsortorder FROM pg_enum
    JOIN pg_type ON pg_enum.enumtypid = pg_type.oid ORDER BY typname, enumlabel ASC;


-- Test inheritance with different col order in child
-- Add a new row and read child to make sure docdb schema is compatible
INSERT INTO ch VALUES (2, '2_FOO', DEFAULT, 2.2, NULL, '2_fooch', 3.3);
SELECT * FROM ch ORDER BY parc1 DESC;
\d par
\d ch
-- Verify attislocal is set correctly
SELECT attname, attnum, attislocal FROM pg_attribute WHERE attrelid = 'ch'::regclass::oid AND attnum > 0 ORDER BY attnum;

-- Test inheritance with different col order and multiple parents
INSERT INTO inh2_ch values (3, '3_FOO', 3.3, 30, '3_CH_2', '3_CH_5', 3.3);
SELECT * FROM inh2_ch ORDER BY parc1 DESC;
\d inh2_ch
-- Verify attislocal is set correctly
SELECT attname, attnum, attislocal FROM pg_attribute WHERE attrelid = 'inh2_ch'::regclass::oid AND attnum > 0 ORDER BY attnum;
