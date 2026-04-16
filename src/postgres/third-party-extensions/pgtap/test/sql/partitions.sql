\unset ECHO
\i test/setup.sql

SELECT plan(102);
--SELECT * FROM no_plan();

-- This will be rolled back. :-)
SET client_min_messages = warning;

-- Create inherited tables (not partitions).
CREATE TABLE public.base(id INT PRIMARY KEY);
CREATE TABLE public.sub(id INT PRIMARY KEY) INHERITS (public.base);

-- Create a partitioned table with two partitions.
CREATE TABLE public.parted(id INT NOT NULL) PARTITION BY RANGE (id);
CREATE TABLE public.part1 PARTITION OF public.parted FOR VALUES FROM (1) TO (10);
CREATE TABLE public.part2 PARTITION OF public.parted FOR VALUES FROM (11) TO (20);

-- Create partitions outside of search path.
CREATE SCHEMA hide;
CREATE TABLE hide.hidden_parted(id INT NOT NULL) PARTITION BY RANGE (id);
CREATE TABLE hide.hidden_part1 PARTITION OF hide.hidden_parted FOR VALUES FROM (1) TO (10);
CREATE TABLE hide.hidden_part2 PARTITION OF hide.hidden_parted FOR VALUES FROM (11) TO (20);

-- Put a partition for the public table in the hidden schema.
CREATE TABLE hide.part3 PARTITION OF public.parted FOR VALUES FROM (21) TO (30);

-- Put a partition for the hidden table in the public schema.
CREATE TABLE public.not_hidden_part3 PARTITION OF hide.hidden_parted
   FOR VALUES FROM (21) TO (30);

RESET client_min_messages;

/****************************************************************************/
-- Test is_partition_of().
SELECT * FROM check_test(
    is_partition_of( 'public', 'part1', 'public', 'parted', 'whatevs' ),
    true,
    'is_partition_of( csch, ctab, psch, ptab, desc )',
    'whatevs',
    ''
);

SELECT * FROM check_test(
    is_partition_of( 'public', 'part1', 'public', 'parted' ),
    true,
    'is_partition_of( csch, ctab, psch, ptab )',
    'Table public.part1 should be a partition of public.parted',
    ''
);

SELECT * FROM check_test(
    is_partition_of( 'part1', 'parted', 'whatevs' ),
    true,
    'is_partition_of( ctab, ptab, desc )',
    'whatevs',
    ''
);

SELECT * FROM check_test(
    is_partition_of( 'part1', 'parted' ),
    true,
    'is_partition_of( ctab, ptab )',
    'Table part1 should be a partition of parted',
    ''
);

-- is_partition_of() should fail for inherited but not partitioned tables.
SELECT * FROM check_test(
    is_partition_of( 'public', 'sub', 'public', 'base', 'whatevs' ),
    false,
    'is_partition_of( csch, non-part ctab, psch, non-part ptab, desc )',
    'whatevs',
    ''
);

SELECT * FROM check_test(
    is_partition_of( 'sub', 'base', 'whatevs' ),
    false,
    'is_partition_of( non-part ctab, non-part ptab, desc )',
    'whatevs',
    ''
);

-- is_partition_of() should fail for parted table and non-part sub.
SELECT * FROM check_test(
    is_partition_of( 'public', 'sub', 'public', 'parted', 'whatevs' ),
    false,
    'is_partition_of( csch, non-part ctab, psch, ptab, desc )',
    'whatevs',
    ''
);

SELECT * FROM check_test(
    is_partition_of( 'sub', 'parted', 'whatevs' ),
    false,
    'is_partition_of( non-part ctab, ptab, desc )',
    'whatevs',
    ''
);

-- is_partition_of() should fail for partition sub but wrong base.
SELECT * FROM check_test(
    is_partition_of( 'public', 'part1', 'public', 'base', 'whatevs' ),
    false,
    'is_partition_of( csch, ctab, psch, non-part ptab, desc )',
    'whatevs',
    ''
);

SELECT * FROM check_test(
    is_partition_of( 'part1', 'base', 'whatevs' ),
    false,
    'is_partition_of( ctab, non-part ptab, desc )',
    'whatevs',
    ''
);

-- Should find tables outside search path for explicit schema.
SELECT * FROM check_test(
    is_partition_of( 'hide', 'hidden_part1', 'hide', 'hidden_parted', 'whatevs' ),
    true,
    'is_partition_of( priv csch, ctab, priv psch, ptab, desc )',
    'whatevs',
    ''
);

-- But not when the schema is not specified.
SELECT * FROM check_test(
    is_partition_of( 'hidden_part1', 'hidden_parted', 'whatevs' ),
    false,
    'is_partition_of( priv ctab, priv ptab, desc )',
    'whatevs',
    ''
);

-- Should find explicit hidden table for public partition.
SELECT * FROM check_test(
    is_partition_of( 'hide', 'part3', 'public', 'parted', 'whatevs' ),
    true,
    'is_partition_of( priv csch, ctab, psch, ptab, desc )',
    'whatevs',
    ''
);

-- But still not when schemas not specified.
SELECT * FROM check_test(
    is_partition_of( 'part3', 'hidden', 'whatevs' ),
    false,
    'is_partition_of( priv ctab, ptab, desc )',
    'whatevs',
    ''
);

-- Should find public partition for hidden base.
SELECT * FROM check_test(
    is_partition_of( 'public', 'not_hidden_part3', 'hide', 'hidden_parted', 'whatevs' ),
    true,
    'is_partition_of( csch, ctab, priv psch, ptab, desc )',
    'whatevs',
    ''
);

-- But not if no schemas are specified.
SELECT * FROM check_test(
    is_partition_of( 'not_hidden_part3', 'hidden_parted', 'whatevs' ),
    false,
    'is_partition_of( ctab, priv ptab, desc )',
    'whatevs',
    ''
);

-- And of course, it should not work for nonexistent partitions.
SELECT * FROM check_test(
    is_partition_of( 'public', 'nonesuch', 'public', 'nothing', 'whatevs' ),
    false,
    'is_partition_of( csch, non-ctab, psch, non-ptab, desc )',
    'whatevs',
    ''
);

SELECT * FROM check_test(
    is_partition_of( 'nonesuch', 'nothing', 'whatevs' ),
    false,
    'is_partition_of( non-ctab, non-ptab, desc )',
    'whatevs',
    ''
);

SELECT * FROM check_test(
    is_partition_of( 'public', 'part1', 'public', 'nothing', 'whatevs' ),
    false,
    'is_partition_of( csch, ctab, psch, non-ptab, desc )',
    'whatevs',
    ''
);

SELECT * FROM check_test(
    is_partition_of( 'nonesuch', 'part1', 'whatevs' ),
    false,
    'is_partition_of( ctab, non-ptab, desc )',
    'whatevs',
    ''
);

SELECT * FROM check_test(
    is_partition_of( 'public', 'nonesuch', 'public', 'parted', 'whatevs' ),
    false,
    'is_partition_of( csch, non-ctab, psch, ptab, desc )',
    'whatevs',
    ''
);

SELECT * FROM check_test(
    is_partition_of( 'nonesuch', 'parted', 'whatevs' ),
    false,
    'is_partition_of( non-ctab, ptab, desc )',
    'whatevs',
    ''
);

/****************************************************************************/
-- Test partitions_are().
SELECT * FROM check_test(
    partitions_are( 'public', 'parted', '{part1,part2,hide.part3}', 'hi' ),
    true,
    'partitions_are( sch, tab, parts, desc )',
    'hi',
    ''
);

SELECT * FROM check_test(
    partitions_are( 'public', 'parted', '{part1,part2,hide.part3}'::name[] ),
    true,
    'partitions_are( sch, tab, parts )',
    'Table public.parted should have the correct partitions',
    ''
);

SELECT * FROM check_test(
    partitions_are( 'parted', '{part1,part2,hide.part3}'::name[], 'hi' ),
    true,
    'partitions_are( tab, parts, desc )',
    'hi',
    ''
);

SELECT * FROM check_test(
    partitions_are( 'parted', '{part1,part2,hide.part3}' ),
    true,
    'partitions_are( tab, parts )',
    'Table parted should have the correct partitions',
    ''
);

-- Test diagnostics.
SELECT * FROM check_test(
    partitions_are( 'public', 'parted', '{part1,part2of2,hide.part3}', 'hi' ),
    false,
    'partitions_are( sch, tab, bad parts, desc )',
    'hi',
    '    Extra partitions:
        part2
    Missing partitions:
        part2of2'
);

SELECT * FROM check_test(
    partitions_are( 'parted', '{part1,part2of2,hide.part3}'::name[], 'hi' ),
    false,
    'partitions_are( tab, bad parts, desc )',
    'hi',
    '    Extra partitions:
        part2
    Missing partitions:
        part2of2'
);

-- Test with the hidden schema.
SELECT * FROM check_test(
    partitions_are(
        'hide', 'hidden_parted',
        '{hide.hidden_part1,hide.hidden_part2,not_hidden_part3}',
        'hi'
    ),
    true,
    'partitions_are( hidden sch, tab, parts, desc )',
    'hi',
    ''
);

-- Should fail for partitioned table outside search path.
SELECT * FROM check_test(
    partitions_are(
        'hidden_parted',
        '{hide.hidden_part1,hide.hidden_part2,not_hidden_part3}'::name[],
        'hi'
    ),
    false,
    'partitions_are( hidden tab, parts, desc )',
    'hi',
    '    Missing partitions:
        "hide.hidden_part1"
        "hide.hidden_part2"
        not_hidden_part3'
);

-- Should not work for unpartitioned but inherited table
SELECT * FROM check_test(
    partitions_are( 'public', 'base', '{sub}', 'hi' ),
    false,
    'partitions_are( sch, non-parted tab, inherited tab, desc )',
    'hi',
    '    Missing partitions:
        sub'
);

SELECT * FROM check_test(
    partitions_are( 'base', '{sub}'::name[], 'hi' ),
    false,
    'partitions_are( non-parted tab, inherited tab, desc )',
    'hi',
    '    Missing partitions:
        sub'
);

-- Should not work for non-existent table.
SELECT * FROM check_test(
    partitions_are( 'public', 'nonesuch', '{part1}', 'hi' ),
    false,
    'partitions_are( sch, non-existent tab, parts, desc )',
    'hi',
    '    Missing partitions:
        part1'
);

SELECT * FROM check_test(
    partitions_are( 'nonesuch', '{part1}'::name[], 'hi' ),
    false,
    'partitions_are( non-existent tab, parts, desc )',
    'hi',
    '    Missing partitions:
        part1'
);

/****************************************************************************/
-- Finish the tests and clean up.
SELECT * FROM finish();
ROLLBACK;
