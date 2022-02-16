-- ===================================================================
-- create FDW objects
-- ===================================================================

CREATE EXTENSION postgres_fdw;

CREATE SERVER testserver1 FOREIGN DATA WRAPPER postgres_fdw;
DO $d$
    BEGIN
        EXECUTE $$CREATE SERVER loopback FOREIGN DATA WRAPPER postgres_fdw
            OPTIONS (dbname '$$||current_database()||$$',
                     host '$$||current_setting('listen_addresses')||$$',
                     port '$$||current_setting('port')||$$'
            )$$;
        EXECUTE $$CREATE SERVER loopback2 FOREIGN DATA WRAPPER postgres_fdw
            OPTIONS (dbname '$$||current_database()||$$',
                     host '$$||current_setting('listen_addresses')||$$',
                     port '$$||current_setting('port')||$$'
            )$$;
    END;
$d$;

CREATE USER MAPPING FOR public SERVER testserver1
	OPTIONS (user 'value', password 'value');
CREATE USER MAPPING FOR CURRENT_USER SERVER loopback;
CREATE USER MAPPING FOR CURRENT_USER SERVER loopback2;

-- Test copy tuple routing
create table ctrtest (a int, b text) partition by list (a);
create table loct1 (a int check (a in (1)), b text);
create foreign table remp1 (a int check (a in (1)), b text) server loopback options (table_name 'loct1');
create table loct2 (a int check (a in (2)), b text);
create foreign table remp2 (b text, a int check (a in (2))) server loopback options (table_name 'loct2');
alter table ctrtest attach partition remp1 for values in (1);
alter table ctrtest attach partition remp2 for values in (2);

copy ctrtest from stdin;
1	foo
2	qux
\.

select tableoid::regclass, * FROM ctrtest;
select tableoid::regclass, * FROM remp1;
select tableoid::regclass, * FROM remp2;
