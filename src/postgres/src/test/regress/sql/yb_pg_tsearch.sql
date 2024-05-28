-- directory paths are passed to us in environment variables
\getenv abs_srcdir PG_ABS_SRCDIR

--
-- Sanity checks for text search catalogs
--
-- NB: we assume the oidjoins test will have caught any dangling links,
-- that is OID or REGPROC fields that are not zero and do not match some
-- row in the linked-to table.  However, if we want to enforce that a link
-- field can't be 0, we have to check it here.

-- Find unexpected zero link entries

SELECT oid, prsname
FROM pg_ts_parser
WHERE prsnamespace = 0 OR prsstart = 0 OR prstoken = 0 OR prsend = 0 OR
      -- prsheadline is optional
      prslextype = 0;

SELECT oid, dictname
FROM pg_ts_dict
WHERE dictnamespace = 0 OR dictowner = 0 OR dicttemplate = 0;

SELECT oid, tmplname
FROM pg_ts_template
WHERE tmplnamespace = 0 OR tmpllexize = 0;  -- tmplinit is optional

SELECT oid, cfgname
FROM pg_ts_config
WHERE cfgnamespace = 0 OR cfgowner = 0 OR cfgparser = 0;

SELECT mapcfg, maptokentype, mapseqno
FROM pg_ts_config_map
WHERE mapcfg = 0 OR mapdict = 0;

-- Look for pg_ts_config_map entries that aren't one of parser's token types
SELECT * FROM
  ( SELECT oid AS cfgid, (ts_token_type(cfgparser)).tokid AS tokid
    FROM pg_ts_config ) AS tt
RIGHT JOIN pg_ts_config_map AS m
    ON (tt.cfgid=m.mapcfg AND tt.tokid=m.maptokentype)
WHERE
    tt.cfgid IS NULL OR tt.tokid IS NULL;

-- Load some test data
CREATE TABLE test_tsvector(
	t text,
	a tsvector
);

\set filename :abs_srcdir '/data/tsearch.data'
COPY test_tsvector FROM :'filename';

ANALYZE test_tsvector;

-- test basic text search behavior without indexes, then with

SELECT count(*) FROM test_tsvector WHERE a @@ 'wr|qh';
SELECT count(*) FROM test_tsvector WHERE a @@ 'wr&qh';
SELECT count(*) FROM test_tsvector WHERE a @@ 'eq&yt';
SELECT count(*) FROM test_tsvector WHERE a @@ 'eq|yt';
SELECT count(*) FROM test_tsvector WHERE a @@ '(eq&yt)|(wr&qh)';
SELECT count(*) FROM test_tsvector WHERE a @@ '(eq|yt)&(wr|qh)';
SELECT count(*) FROM test_tsvector WHERE a @@ 'w:*|q:*';
SELECT count(*) FROM test_tsvector WHERE a @@ any ('{wr,qh}');
SELECT count(*) FROM test_tsvector WHERE a @@ 'no_such_lexeme';
SELECT count(*) FROM test_tsvector WHERE a @@ '!no_such_lexeme';

create index wowidx on test_tsvector using gist (a);

CREATE INDEX wowidx ON test_tsvector USING gin (a);

SET enable_seqscan=OFF;
-- YB note: yb_test_ybgin_disable_cost_factor setting is needed to really force
-- index scan even if it is detected to be not supported.
SET yb_test_ybgin_disable_cost_factor = 0.5;
-- GIN only supports bitmapscan, so no need to test plain indexscan
-- YB note: ybgin is the opposite: it supports indexscan, not bitmapscan

explain (costs off) SELECT count(*) FROM test_tsvector WHERE a @@ 'wr|qh';

SELECT count(*) FROM test_tsvector WHERE a @@ 'wr|qh';
SELECT count(*) FROM test_tsvector WHERE a @@ 'wr&qh';
SELECT count(*) FROM test_tsvector WHERE a @@ 'eq&yt';
SELECT count(*) FROM test_tsvector WHERE a @@ 'eq|yt';
SELECT count(*) FROM test_tsvector WHERE a @@ '(eq&yt)|(wr&qh)';
SELECT count(*) FROM test_tsvector WHERE a @@ '(eq|yt)&(wr|qh)';
SELECT count(*) FROM test_tsvector WHERE a @@ 'w:*|q:*';
SELECT count(*) FROM test_tsvector WHERE a @@ any ('{wr,qh}');
SELECT count(*) FROM test_tsvector WHERE a @@ 'no_such_lexeme';
SELECT count(*) FROM test_tsvector WHERE a @@ '!no_such_lexeme';

RESET yb_test_ybgin_disable_cost_factor;
RESET enable_seqscan;

INSERT INTO test_tsvector VALUES ('???', 'DFG:1A,2B,6C,10 FGH');
SELECT * FROM ts_stat('SELECT a FROM test_tsvector') ORDER BY ndoc DESC, nentry DESC, word LIMIT 10;
SELECT * FROM ts_stat('SELECT a FROM test_tsvector', 'AB') ORDER BY ndoc DESC, nentry DESC, word;

--dictionaries and to_tsvector

SELECT ts_lexize('english_stem', 'skies');
SELECT ts_lexize('english_stem', 'identity');

SELECT * FROM ts_token_type('default');

SELECT * FROM ts_parse('default', '345 qwe@efd.r '' http://www.com/ http://aew.werc.ewr/?ad=qwe&dw 1aew.werc.ewr/?ad=qwe&dw 2aew.werc.ewr http://3aew.werc.ewr/?ad=qwe&dw http://4aew.werc.ewr http://5aew.werc.ewr:8100/?  ad=qwe&dw 6aew.werc.ewr:8100/?ad=qwe&dw 7aew.werc.ewr:8100/?ad=qwe&dw=%20%32 +4.0e-10 qwe qwe qwqwe 234.435 455 5.005 teodor@stack.net teodor@123-stack.net 123_teodor@stack.net 123-teodor@stack.net qwe-wer asdf <fr>qwer jf sdjk<we hjwer <werrwe> ewr1> ewri2 <a href="qwe<qwe>">
/usr/local/fff /awdf/dwqe/4325 rewt/ewr wefjn /wqe-324/ewr gist.h gist.h.c gist.c. readline 4.2 4.2. 4.2, readline-4.2 readline-4.2. 234
<i <b> wow  < jqw <> qwerty');

SELECT to_tsvector('english', '345 qwe@efd.r '' http://www.com/ http://aew.werc.ewr/?ad=qwe&dw 1aew.werc.ewr/?ad=qwe&dw 2aew.werc.ewr http://3aew.werc.ewr/?ad=qwe&dw http://4aew.werc.ewr http://5aew.werc.ewr:8100/?  ad=qwe&dw 6aew.werc.ewr:8100/?ad=qwe&dw 7aew.werc.ewr:8100/?ad=qwe&dw=%20%32 +4.0e-10 qwe qwe qwqwe 234.435 455 5.005 teodor@stack.net teodor@123-stack.net 123_teodor@stack.net 123-teodor@stack.net qwe-wer asdf <fr>qwer jf sdjk<we hjwer <werrwe> ewr1> ewri2 <a href="qwe<qwe>">
/usr/local/fff /awdf/dwqe/4325 rewt/ewr wefjn /wqe-324/ewr gist.h gist.h.c gist.c. readline 4.2 4.2. 4.2, readline-4.2 readline-4.2. 234
<i <b> wow  < jqw <> qwerty');

-- to_tsquery

SELECT to_tsquery('english', 'qwe & sKies ');
SELECT to_tsquery('simple', 'qwe & sKies ');

SELECT ts_headline('simple', '1 2 3 1 3'::text, '1 <-> 3', 'MaxWords=2, MinWords=1');
SELECT ts_headline('simple', '1 2 3 1 3'::text, '1 & 3', 'MaxWords=4, MinWords=1');
SELECT ts_headline('simple', '1 2 3 1 3'::text, '1 <-> 3', 'MaxWords=4, MinWords=1');

--test GUC
SET default_text_search_config=simple;

SELECT to_tsvector('SKIES My booKs');
SELECT plainto_tsquery('SKIES My booKs');
SELECT to_tsquery('SKIES & My | booKs');

SET default_text_search_config=english;

SELECT to_tsvector('SKIES My booKs');

-- test finding items in GIN's pending list
create temp table pendtest (ts tsvector);
create index pendtest_idx on pendtest using gin(ts);
insert into pendtest values (to_tsvector('Lore ipsam'));
insert into pendtest values (to_tsvector('Lore ipsum'));
select * from pendtest where 'ipsu:*'::tsquery @@ ts;
select * from pendtest where 'ipsa:*'::tsquery @@ ts;
select * from pendtest where 'ips:*'::tsquery @@ ts;
select * from pendtest where 'ipt:*'::tsquery @@ ts;
select * from pendtest where 'ipi:*'::tsquery @@ ts;

-- test websearch_to_tsquery function
select websearch_to_tsquery('simple', 'I have a fat:*ABCD cat');
select websearch_to_tsquery('simple', 'orange:**AABBCCDD');
select websearch_to_tsquery('simple', 'fat:A!cat:B|rat:C<');
select websearch_to_tsquery('simple', 'fat:A : cat:B');

select websearch_to_tsquery('simple', 'fat*rat');
select websearch_to_tsquery('simple', 'fat-rat');
select websearch_to_tsquery('simple', 'fat_rat');

-- weights are completely ignored
select websearch_to_tsquery('simple', 'abc : def');
select websearch_to_tsquery('simple', 'abc:def');
select websearch_to_tsquery('simple', 'a:::b');
select websearch_to_tsquery('simple', 'abc:d');
select websearch_to_tsquery('simple', ':');

-- these operators are ignored
select websearch_to_tsquery('simple', 'abc & def');
select websearch_to_tsquery('simple', 'abc | def');
select websearch_to_tsquery('simple', 'abc <-> def');
select websearch_to_tsquery('simple', 'abc (pg or class)');

-- test OR operator
select websearch_to_tsquery('simple', 'cat or rat');
select websearch_to_tsquery('simple', 'cat OR rat');
select websearch_to_tsquery('simple', 'cat "OR" rat');
select websearch_to_tsquery('simple', 'cat OR');
select websearch_to_tsquery('simple', 'OR rat');
select websearch_to_tsquery('simple', '"fat cat OR rat"');
select websearch_to_tsquery('simple', 'fat (cat OR rat');
select websearch_to_tsquery('simple', 'or OR or');

-- OR is an operator here ...
select websearch_to_tsquery('simple', '"fat cat"or"fat rat"');
select websearch_to_tsquery('simple', 'fat or(rat');
select websearch_to_tsquery('simple', 'fat or)rat');
select websearch_to_tsquery('simple', 'fat or&rat');
select websearch_to_tsquery('simple', 'fat or|rat');
select websearch_to_tsquery('simple', 'fat or!rat');
select websearch_to_tsquery('simple', 'fat or<rat');
select websearch_to_tsquery('simple', 'fat or>rat');
select websearch_to_tsquery('simple', 'fat or ');

-- ... but not here
select websearch_to_tsquery('simple', 'abc orange');
select websearch_to_tsquery('simple', 'abc OR1234');
select websearch_to_tsquery('simple', 'abc or-abc');
select websearch_to_tsquery('simple', 'abc OR_abc');
