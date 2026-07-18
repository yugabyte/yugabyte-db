LOAD 'pg_hint_plan';
SET pg_hint_plan.enable_hint TO on;
SET pg_hint_plan.debug_print TO on;
SET client_min_messages TO LOG;
SET search_path TO public;

----
---- No.A-1-1 install
---- No.A-2-1 uninstall
----

-- No.A-1-1-3
CREATE EXTENSION pg_hint_plan;

-- No.A-1-2-3
DROP EXTENSION pg_hint_plan;

-- No.A-1-1-4
CREATE SCHEMA other_schema;
CREATE EXTENSION pg_hint_plan SCHEMA other_schema;

CREATE EXTENSION pg_hint_plan;
DROP SCHEMA other_schema;

----
---- No. A-5-1 comment pattern
----

-- No. A-5-1-1
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-1-2
/* +SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-1-3
/*SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-1-4
--+SeqScan(t1)
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-1-5
/* /*+SeqScan(t1)*/  */
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

----
---- No. A-5-2 hint position
----

-- No. A-5-2-1
EXPLAIN (COSTS false) SELECT c1 FROM s1.t1 WHERE t1.c1 = 1;
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT c1 FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-2-2
EXPLAIN (COSTS false) SELECT c1, c2 AS c_2 /*+SeqScan(t1)*/ FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-2-3
EXPLAIN (COSTS false) SELECT c1 AS "c1"/*+SeqScan(t1)*/ FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-2-4
EXPLAIN (COSTS false) SELECT * /*+SeqScan(t1)*/ FROM s1.t1 WHERE t1.c1 = 1;

----
---- No. A-6-1 hint's table definition
----

SET pg_hint_plan.enable_hint_table TO on;
-- No. A-6-1-1
\d hint_plan.hints

----
---- No. A-6-2 search condition
----
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;
-- No. A-6-2-1
INSERT INTO hint_plan.hints (norm_query_string, application_name, hints)
	VALUES (
	'EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = ?;',
	'',
	'SeqScan(t1)');
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-6-2-2
INSERT INTO hint_plan.hints (norm_query_string, application_name, hints)
	VALUES (
	'EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = ?;',
	'psql',
	'BitmapScan(t1)');
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;
TRUNCATE hint_plan.hints;

-- No. A-6-2-3
INSERT INTO hint_plan.hints (norm_query_string, application_name, hints)
	VALUES (
	'EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = ?;',
	'dummy_application_name',
	'SeqScan(t1)'
);
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;
TRUNCATE hint_plan.hints;

-- No. A-6-2-4
INSERT INTO hint_plan.hints (norm_query_string, application_name, hints)
	VALUES (
	'EXPLAIN (COSTS false) SELECT * FROM s1.t1;',
	'',
	'SeqScan(t1)'
);
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;
TRUNCATE hint_plan.hints;

----
---- No. A-6-3 number of constant
----

-- No. A-6-3-1
INSERT INTO hint_plan.hints (norm_query_string, application_name, hints)
	VALUES (
	'EXPLAIN (COSTS false) SELECT c1 FROM s1.t1;',
	'',
	'SeqScan(t1)'
);
EXPLAIN (COSTS false) SELECT c1 FROM s1.t1;
TRUNCATE hint_plan.hints;

-- No. A-6-3-2
INSERT INTO hint_plan.hints (norm_query_string, application_name, hints)
	VALUES (
	'EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = ?;',
	'',
	'SeqScan(t1)'
);
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;
TRUNCATE hint_plan.hints;

-- No. A-6-3-3
INSERT INTO hint_plan.hints (norm_query_string, application_name, hints)
	VALUES (
	'EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = ? OR t1.c1 = ?;',
	'',
	'SeqScan(t1)'
);
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1 OR t1.c1 = 0;
TRUNCATE hint_plan.hints;
SET pg_hint_plan.enable_hint_table TO off;

----
---- No. A-7-2 hint delimiter
----

EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;
-- No. A-7-2-1
-- No. A-7-2-2
-- No. A-7-2-3
-- No. A-7-2-4
-- No. A-7-2-5
-- No. A-7-2-6
-- No. A-7-2-7
/*+Set(enable_indexscan"off")Set(enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-2-8
/*+ Set(enable_indexscan"off")Set(enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-2-9
/*+Set(enable_indexscan"off")Set(enable_bitmapscan"off") */
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-2-10
/*+ Set (enable_indexscan"off") Set (enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-2-11
/*+Set ( enable_indexscan"off")Set ( enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-2-12
/*+Set(enable_indexscan"off" ) Set(enable_bitmapscan"off" ) */
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-2-13
/*+Set( enable_indexscan "off" )Set( enable_bitmapscan "off" )*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-2-14
/*+ Set ( enable_indexscan "off" ) Set ( enable_bitmapscan "off" ) */
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-2-15
/*+	Set(enable_indexscan"off")Set(enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-2-16
/*+Set(enable_indexscan"off")Set(enable_bitmapscan"off")	*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-2-17
/*+	Set	(enable_indexscan"off")	Set	(enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-2-18
/*+Set	(	enable_indexscan"off")Set	(	enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-2-19
/*+Set(enable_indexscan"off"	)	Set(enable_bitmapscan"off"	)	*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-2-20
/*+Set(	enable_indexscan	"off"	)Set(	enable_bitmapscan	"off"	)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-2-21
/*+	Set	(	enable_indexscan	"off"	)	Set	(	enable_bitmapscan	"off"	)	*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-2-22
/*+
Set(enable_indexscan"off")Set(enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-2-23
/*+Set(enable_indexscan"off")Set(enable_bitmapscan"off")
*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-2-24
/*+
Set
(enable_indexscan"off")
Set
(enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-2-25
/*+Set
(
enable_indexscan"off")Set
(
enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-2-26
/*+Set(enable_indexscan"off"
)
Set(enable_bitmapscan"off"
)
*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-2-27
/*+Set(
enable_indexscan
"off"
)Set(
enable_bitmapscan
"off"
)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-2-28
/*+
Set
(
enable_indexscan
"off"
)
Set
(
enable_bitmapscan
"off"
)
*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-2-29
/*+ 	
	 Set(enable_indexscan"off")Set(enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-2-30
/*+Set(enable_indexscan"off")Set(enable_bitmapscan"off") 	
	 */
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-2-31
/*+ 	
	 Set 	
	 (enable_indexscan"off") 	
	 Set 	
	 (enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-2-32
/*+Set 	
	 ( 	
	 enable_indexscan"off")Set 	
	 ( 	
	 enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-2-33
/*+Set(enable_indexscan"off" 	
	 ) 	
	 Set(enable_bitmapscan"off" 	
	 ) 	
	 */
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-2-34
/*+Set( 	
	 enable_indexscan 	
	 "off" 	
	 )Set( 	
	 enable_bitmapscan 	
	 "off" 	
	 )*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-2-35
/*+ 	
	 Set 	
	 ( 	
	 enable_indexscan 	
	 "off" 	
	 ) 	
	 Set 	
	 ( 	
	 enable_bitmapscan 	
	 "off" 	
	 ) 	
	 */
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

----
---- No. A-7-3 hint object pattern
---- No. A-9-2 message object pattern
----

-- No. A-7-3-1
-- No. A-9-2-1
/*+SeqScan(t)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 t WHERE t.c1 = 1;
/*+SeqScan(ttt)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 ttt WHERE ttt.c1 = 1;
/*+SeqScan("t")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 t WHERE t.c1 = 1;
/*+SeqScan("ttt")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 ttt WHERE ttt.c1 = 1;

-- No. A-7-3-2
-- No. A-9-2-2
/*+SeqScan(T)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "T" WHERE "T".c1 = 1;
/*+SeqScan(TTT)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "TTT" WHERE "TTT".c1 = 1;
/*+SeqScan("T")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "T" WHERE "T".c1 = 1;
/*+SeqScan("TTT")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "TTT" WHERE "TTT".c1 = 1;

-- No. A-7-3-3
-- No. A-9-2-3
/*+SeqScan(()*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "(" WHERE "(".c1 = 1;
/*+SeqScan("(")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "(" WHERE "(".c1 = 1;

-- No. A-7-3-4
-- No. A-9-2-4
/*+SeqScan())*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 ")" WHERE ")".c1 = 1;
/*+SeqScan(")")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 ")" WHERE ")".c1 = 1;
/*+SeqScan(")))")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 ")))" WHERE ")))".c1 = 1;

-- No. A-7-3-5
-- No. A-9-2-5
/*+SeqScan(")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 """" WHERE """".c1 = 1;
/*+SeqScan("""")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 """" WHERE """".c1 = 1;
/*+SeqScan("""""""")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 """""""" WHERE """""""".c1 = 1;

-- No. A-7-3-6
-- No. A-9-2-6
/*+SeqScan( )*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 " " WHERE " ".c1 = 1;
/*+SeqScan(" ")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 " " WHERE " ".c1 = 1;
/*+SeqScan("   ")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "   " WHERE "   ".c1 = 1;

-- No. A-7-3-7
-- No. A-9-2-7
/*+SeqScan(	)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "	" WHERE "	".c1 = 1;
/*+SeqScan("	")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "	" WHERE "	".c1 = 1;
/*+SeqScan("			")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "			" WHERE "			".c1 = 1;

-- No. A-7-3-8
-- No. A-9-2-8
/*+SeqScan(
)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "
" WHERE "
".c1 = 1;
/*+SeqScan("
")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "
" WHERE "
".c1 = 1;
/*+SeqScan("


")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "


" WHERE "


".c1 = 1;

-- No. A-7-3-9
-- No. A-9-2-9
/*+SeqScan(Set)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "Set" WHERE "Set".c1 = 1;
/*+SeqScan("Set")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "Set" WHERE "Set".c1 = 1;
/*+SeqScan("Set SeqScan Leading")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "Set SeqScan Leading" WHERE "Set SeqScan Leading".c1 = 1;

-- No. A-7-3-10
-- No. A-9-2-10
/*+SeqScan(あ)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 あ WHERE あ.c1 = 1;
/*+SeqScan(あいう)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 あいう WHERE あいう.c1 = 1;
/*+SeqScan("あ")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 あ WHERE あ.c1 = 1;
/*+SeqScan("あいう")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 あいう WHERE あいう.c1 = 1;

-- No. A-7-3-11
-- No. A-9-2-11
/*+SeqScan(/**/)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "/**/" WHERE "/**/".c1 = 1;
/*+SeqScan(/**//**//**/)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "/**//**//**/" WHERE "/**//**//**/".c1 = 1;

-- No. A-7-3-12
-- No. A-9-2-12
/*+SeqScan("tT()"" 	
Set/**/あ")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "tT()"" 	
Set/**/あ" WHERE "tT()"" 	
Set/**/あ".c1 = 1;
--"

/*+SeqScan("tT()"" 	
Setあ")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "tT()"" 	
Setあ" WHERE "tT()"" 	
Setあ".c1 = 1;

-- No. A-7-3-13
-- No. A-9-2-13
/*+SeqScan(a123456789b123456789c123456789d123456789e123456789f123)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "123456789012345678901234567890123456789012345678901234" WHERE "123456789012345678901234567890123456789012345678901234".c1 = 1;

----
---- No. A-7-4 hint parse error
----

-- No. A-7-4-1
/*+Set(enable_indexscan off)Set enable_tidscan off)Set(enable_bitmapscan off)SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-4-2
/*+Set(enable_indexscan off)Set(enable_tidscan off Set(enable_bitmapscan off)SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-4-3
/*+Set(enable_indexscan off)Set(enable_tidscan "off)Set(enable_bitmapscan off)SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-4-4
/*+Set(enable_indexscan off)SeqScan("")Set(enable_bitmapscan off)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-4-5
/*+Set(enable_indexscan off)NoSet(enable_tidscan off)Set(enable_bitmapscan off)SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-4-6
/*+Set(enable_indexscan off)"Set"(enable_tidscan off)Set(enable_bitmapscan off)SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-4-7
/*+Set(enable_indexscan off)Set(enable_tidscan /* value */off)Set(enable_bitmapscan off)SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

----
---- No. A-8-1 original GUC parameter
----
---- Don't test postgresql itself.
-- No. A-8-1-1
-- SET ROLE regress_super_user;
-- SET pg_hint_plan.debug_print TO off;
-- SHOW pg_hint_plan.enable_hint;
-- SHOW pg_hint_plan.debug_print;
-- SHOW pg_hint_plan.parse_messages;
-- SET pg_hint_plan.enable_hint TO off;
-- SET pg_hint_plan.debug_print TO on;
-- SET pg_hint_plan.parse_messages TO error;
-- SHOW pg_hint_plan.enable_hint;
-- SHOW pg_hint_plan.debug_print;
-- SHOW pg_hint_plan.parse_messages;
-- RESET pg_hint_plan.enable_hint;
-- RESET pg_hint_plan.debug_print;
-- RESET pg_hint_plan.parse_messages;
-- SHOW pg_hint_plan.enable_hint;
-- SHOW pg_hint_plan.debug_print;
-- SHOW pg_hint_plan.parse_messages;
-- 
-- -- No. A-8-1-2
-- SET ROLE regress_normal_user;
-- SHOW pg_hint_plan.enable_hint;
-- SHOW pg_hint_plan.debug_print;
-- SHOW pg_hint_plan.parse_messages;
-- SET pg_hint_plan.enable_hint TO off;
-- SET pg_hint_plan.debug_print TO on;
-- SET pg_hint_plan.parse_messages TO error;
-- SHOW pg_hint_plan.enable_hint;
-- SHOW pg_hint_plan.debug_print;
-- SHOW pg_hint_plan.parse_messages;
-- RESET pg_hint_plan.enable_hint;
-- RESET pg_hint_plan.debug_print;
-- RESET pg_hint_plan.parse_messages;
-- SHOW pg_hint_plan.enable_hint;
-- SHOW pg_hint_plan.debug_print;
-- SHOW pg_hint_plan.parse_messages;
-- 
-- RESET ROLE;

----
---- No. A-8-2 original GUC parameter pg_hint_plan.enable_hint
----

-- No. A-8-2-1
SET pg_hint_plan.debug_print TO off;
SET pg_hint_plan.enable_hint TO on;
SHOW pg_hint_plan.enable_hint;
/*+Set(enable_indexscan off)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-8-2-2
SET pg_hint_plan.enable_hint TO off;
SHOW pg_hint_plan.enable_hint;
/*+Set(enable_indexscan off)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-8-2-3
-- Don't test PostgreSQL itself.
-- SET pg_hint_plan.enable_hint TO DEFAULT;
-- SHOW pg_hint_plan.enable_hint;
-- /*+Set(enable_indexscan off)*/
-- EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-8-2-4
-- Don't test PostgreSQL itself
-- SET pg_hint_plan.enable_hint TO enable;
-- SHOW pg_hint_plan.enable_hint;

----
---- No. A-8-3 original GUC parameter pg_hint_plan.debug_print
----

-- No. A-8-3-1
SET pg_hint_plan.enable_hint TO on;
SHOW pg_hint_plan.enable_hint;
SET pg_hint_plan.debug_print TO on;
SHOW pg_hint_plan.debug_print;
/*+Set(enable_indexscan off)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-8-3-2
SET pg_hint_plan.debug_print TO off;
SHOW pg_hint_plan.debug_print;
/*+Set(enable_indexscan off)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-8-3-3
SET pg_hint_plan.debug_print TO DEFAULT;
SHOW pg_hint_plan.debug_print;
/*+Set(enable_indexscan off)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-8-3-4
SET pg_hint_plan.debug_print TO enable;
SHOW pg_hint_plan.debug_print;

----
---- No. A-8-4 original GUC parameter pg_hint_plan.parse_messages
----

SET client_min_messages TO debug5;

-- No. A-8-4-1
SET pg_hint_plan.parse_messages TO debug5;
SHOW pg_hint_plan.parse_messages;
/*+Set*/SELECT 1;
SET client_min_messages TO debug4;
/*+Set*/SELECT 1;

-- No. A-8-4-2
SET pg_hint_plan.parse_messages TO debug4;
SHOW pg_hint_plan.parse_messages;
/*+Set*/SELECT 1;
SET client_min_messages TO debug3;
/*+Set*/SELECT 1;

-- No. A-8-4-3
SET pg_hint_plan.parse_messages TO debug3;
SHOW pg_hint_plan.parse_messages;
/*+Set*/SELECT 1;
SET client_min_messages TO debug2;
/*+Set*/SELECT 1;

-- No. A-8-4-4
SET pg_hint_plan.parse_messages TO debug2;
SHOW pg_hint_plan.parse_messages;
/*+Set*/SELECT 1;
SET client_min_messages TO debug1;
/*+Set*/SELECT 1;

-- No. A-8-4-5
SET pg_hint_plan.parse_messages TO debug1;
SHOW pg_hint_plan.parse_messages;
/*+Set*/SELECT 1;
SET client_min_messages TO log;
/*+Set*/SELECT 1;

-- No. A-8-4-6
SET pg_hint_plan.parse_messages TO log;
SHOW pg_hint_plan.parse_messages;
/*+Set*/SELECT 1;
SET client_min_messages TO info;
/*+Set*/SELECT 1;

-- No. A-8-4-7
SET pg_hint_plan.parse_messages TO info;
SHOW pg_hint_plan.parse_messages;
/*+Set*/SELECT 1;
SET client_min_messages TO notice;
/*+Set*/SELECT 1;

-- No. A-8-4-8
SET pg_hint_plan.parse_messages TO notice;
SHOW pg_hint_plan.parse_messages;
/*+Set*/SELECT 1;
SET client_min_messages TO warning;
/*+Set*/SELECT 1;

-- No. A-8-4-9
SET pg_hint_plan.parse_messages TO warning;
SHOW pg_hint_plan.parse_messages;
/*+Set*/SELECT 1;
SET client_min_messages TO error;
/*+Set*/SELECT 1;

-- No. A-8-4-10
SET pg_hint_plan.parse_messages TO error;
SHOW pg_hint_plan.parse_messages;
/*+Set*/SELECT 1;
SET client_min_messages TO error;
/*+Set*/SELECT 1;

-- No. A-8-4-11
RESET client_min_messages;
SET pg_hint_plan.parse_messages TO DEFAULT;
SHOW pg_hint_plan.parse_messages;
/*+Set*/SELECT 1;

-- No. A-8-4-12
SET pg_hint_plan.parse_messages TO fatal;
SHOW pg_hint_plan.parse_messages;

-- No. A-8-4-13
SET pg_hint_plan.parse_messages TO panic;
SHOW pg_hint_plan.parse_messages;

-- No. A-8-4-14
SET pg_hint_plan.parse_messages TO on;
SHOW pg_hint_plan.parse_messages;

----
---- No. A-8-5 original GUC parameter pg_hint_plan.enable_hint_table
----

INSERT INTO hint_plan.hints (norm_query_string, application_name, hints)
	VALUES (
	'EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = ?;',
	'',
	'SeqScan(t1)');

-- No. A-8-5-1
SET pg_hint_plan.enable_hint_table TO on;
SHOW pg_hint_plan.enable_hint_table;
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-8-5-2
SET pg_hint_plan.enable_hint_table TO off;
SHOW pg_hint_plan.enable_hint_table;
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-8-5-3
SET pg_hint_plan.enable_hint_table TO DEFAULT;
SHOW pg_hint_plan.enable_hint_table;
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-8-5-4
SET pg_hint_plan.enable_hint_table TO enable;
SHOW pg_hint_plan.enable_hint_table;

TRUNCATE hint_plan.hints;

----
---- No. A-9-1 parse error message output
----

-- No. A-9-1-1
/*+"Set"(enable_indexscan on)*/SELECT 1;
/*+Set()(enable_indexscan on)*/SELECT 1;
/*+Set(enable_indexscan on*/SELECT 1;

----
---- No. A-9-3 hint state output
----

SET pg_hint_plan.debug_print TO on;
SET client_min_messages TO LOG;

-- No. A-9-3-1
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-9-3-2
/*+SeqScan(no_table)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-9-3-3
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1 AND t1.ctid = '(1,1)';
/*+TidScan(t1)BitmapScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1 AND t1.ctid = '(1,1)';
/*+TidScan(t1)BitmapScan(t1)IndexScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1 AND t1.ctid = '(1,1)';
/*+TidScan(t1)BitmapScan(t1)IndexScan(t1)SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1 AND t1.ctid = '(1,1)';

-- No. A-9-3-4
/*+Set(enable_indexscan enable)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

----
---- No. A-10-1 hint state output
----

PREPARE p1 AS SELECT * FROM s1.t1 WHERE t1.c1 = 1;
EXPLAIN (COSTS false) EXECUTE p1;
DEALLOCATE p1;

PREPARE p1 AS SELECT * FROM s1.t1 WHERE t1.c1 < $1;
EXPLAIN (COSTS false) EXECUTE p1 (1000);
EXPLAIN (COSTS false) EXECUTE p1 (1000);
EXPLAIN (COSTS false) EXECUTE p1 (1000);
EXPLAIN (COSTS false) EXECUTE p1 (1000);
EXPLAIN (COSTS false) EXECUTE p1 (1000);
EXPLAIN (COSTS false) EXECUTE p1 (1000);
DEALLOCATE p1;

-- No. A-10-1-1
-- No. A-10-1-2
/*+SeqScan(t1)*/
PREPARE p1 AS SELECT * FROM s1.t1 WHERE t1.c1 = 1;
/*+BitmapScan(t1)*/
EXPLAIN (COSTS false) EXECUTE p1;
UPDATE pg_catalog.pg_class SET relpages = relpages WHERE relname = 't1';
/*+BitmapScan(t1)*/
EXPLAIN (COSTS false) EXECUTE p1;
DEALLOCATE p1;

/*+BitmapScan(t1)*/
PREPARE p1 AS SELECT * FROM s1.t1 WHERE t1.c1 < $1;
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) EXECUTE p1 (1000);
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) EXECUTE p1 (1000);
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) EXECUTE p1 (1000);
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) EXECUTE p1 (1000);
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) EXECUTE p1 (1000);
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) EXECUTE p1 (1000);
UPDATE pg_catalog.pg_class SET relpages = relpages WHERE relname = 't1';
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) EXECUTE p1 (1000);
DEALLOCATE p1;

-- No. A-10-1-3
-- No. A-10-1-4
/*+SeqScan(t1)*/
PREPARE p1 AS SELECT * FROM s1.t1 WHERE t1.c1 = 1;
EXPLAIN (COSTS false) EXECUTE p1;
UPDATE pg_catalog.pg_class SET relpages = relpages WHERE relname = 't1';
EXPLAIN (COSTS false) EXECUTE p1;
DEALLOCATE p1;

/*+BitmapScan(t1)*/
PREPARE p1 AS SELECT * FROM s1.t1 WHERE t1.c1 < $1;
EXPLAIN (COSTS false) EXECUTE p1 (1000);
EXPLAIN (COSTS false) EXECUTE p1 (1000);
EXPLAIN (COSTS false) EXECUTE p1 (1000);
EXPLAIN (COSTS false) EXECUTE p1 (1000);
EXPLAIN (COSTS false) EXECUTE p1 (1000);
EXPLAIN (COSTS false) EXECUTE p1 (1000);
UPDATE pg_catalog.pg_class SET relpages = relpages WHERE relname = 't1';
EXPLAIN (COSTS false) EXECUTE p1 (1000);
DEALLOCATE p1;

-- No. A-10-1-5
-- No. A-10-1-6
PREPARE p1 AS SELECT * FROM s1.t1 WHERE t1.c1 = 1;
/*+BitmapScan(t1)*/
EXPLAIN (COSTS false) EXECUTE p1;
UPDATE pg_catalog.pg_class SET relpages = relpages WHERE relname = 't1';
/*+BitmapScan(t1)*/
EXPLAIN (COSTS false) EXECUTE p1;
DEALLOCATE p1;

PREPARE p1 AS SELECT * FROM s1.t1 WHERE t1.c1 < $1;
/*+BitmapScan(t1)*/
EXPLAIN (COSTS false) EXECUTE p1 (1000);
EXPLAIN (COSTS false) EXECUTE p1 (1000);
EXPLAIN (COSTS false) EXECUTE p1 (1000);
EXPLAIN (COSTS false) EXECUTE p1 (1000);
EXPLAIN (COSTS false) EXECUTE p1 (1000);
EXPLAIN (COSTS false) EXECUTE p1 (1000);
UPDATE pg_catalog.pg_class SET relpages = relpages WHERE relname = 't1';
/*+BitmapScan(t1)*/
EXPLAIN (COSTS false) EXECUTE p1 (1000);
DEALLOCATE p1;

-- No. A-10-1-9
-- No. A-10-1-10
/*+SeqScan(t1)*/
PREPARE p1 AS SELECT * FROM s1.t1 WHERE t1.c1 = 1;
/*+BitmapScan(t1)*/
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1;
UPDATE pg_catalog.pg_class SET relpages = relpages WHERE relname = 't1';
/*+BitmapScan(t1)*/
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1;
DEALLOCATE p1;

/*+BitmapScan(t1)*/
PREPARE p1 AS SELECT * FROM s1.t1 WHERE t1.c1 < $1;
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1 (1000);
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1 (1000);
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1 (1000);
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1 (1000);
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1 (1000);
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1 (1000);
UPDATE pg_catalog.pg_class SET relpages = relpages WHERE relname = 't1';
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1 (1000);
DEALLOCATE p1;

-- No. A-10-1-11
-- No. A-10-1-12
/*+SeqScan(t1)*/
PREPARE p1 AS SELECT * FROM s1.t1 WHERE t1.c1 = 1;
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1;
UPDATE pg_catalog.pg_class SET relpages = relpages WHERE relname = 't1';
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1;
DEALLOCATE p1;

/*+BitmapScan(t1)*/
PREPARE p1 AS SELECT * FROM s1.t1 WHERE t1.c1 < $1;
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1 (1000);
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1 (1000);
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1 (1000);
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1 (1000);
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1 (1000);
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1 (1000);
UPDATE pg_catalog.pg_class SET relpages = relpages WHERE relname = 't1';
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1 (1000);
DEALLOCATE p1;

-- No. A-10-1-13
-- No. A-10-1-14
PREPARE p1 AS SELECT * FROM s1.t1 WHERE t1.c1 = 1;
/*+BitmapScan(t1)*/
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1;
UPDATE pg_catalog.pg_class SET relpages = relpages WHERE relname = 't1';
/*+BitmapScan(t1)*/
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1;
DEALLOCATE p1;

PREPARE p1 AS SELECT * FROM s1.t1 WHERE t1.c1 < $1;
/*+BitmapScan(t1)*/
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1 (1000);
/*+BitmapScan(t1)*/
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1 (1000);
/*+BitmapScan(t1)*/
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1 (1000);
/*+BitmapScan(t1)*/
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1 (1000);
/*+BitmapScan(t1)*/
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1 (1000);
/*+BitmapScan(t1)*/
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1 (1000);
UPDATE pg_catalog.pg_class SET relpages = relpages WHERE relname = 't1';
/*+BitmapScan(t1)*/
EXPLAIN (COSTS false) CREATE TABLE test AS EXECUTE p1 (1000);
DEALLOCATE p1;

----
---- No. A-10-4 EXECUTE statement name error
----

-- No. A-10-4-1
EXECUTE p1;
SHOW pg_hint_plan.debug_print;

----
---- No. A-11-5 EXECUTE statement name error
----

-- No. A-11-5-1
SELECT pg_stat_statements_reset();
SELECT * FROM s1.t1 WHERE t1.c1 = 1;
/*+Set(enable_seqscan off)*/ SELECT * FROM s1.t1 WHERE t1.c1 = 1;
/*+SeqScan(t1)*/ SELECT * FROM s1.t1 WHERE t1.c1 = 1;
SELECT s.query, s.calls
  FROM public.pg_stat_statements s
  JOIN pg_catalog.pg_database d
    ON (s.dbid = d.oid)
 ORDER BY 1;

----
---- No. A-12-1 reset of global variable of core at the error
---- No. A-12-2 reset of global variable of original at the error
----

EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
/*+Set(enable_seqscan off)Set(geqo_threshold 100)SeqScan(t1)MergeJoin(t1 t2)NestLoop(t1 t1)*/
PREPARE p1 AS SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
EXPLAIN (COSTS false) EXECUTE p1;

-- No. A-12-1-1
-- No. A-12-2-1
SELECT name, setting FROM settings;
SET pg_hint_plan.parse_messages TO error;
/*+Set(enable_seqscan off)Set(geqo_threshold 100)SeqScan(t1)MergeJoin(t1 t2)NestLoop(t1 t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
SELECT name, setting FROM settings;
/*+Set(enable_seqscan off)Set(geqo_threshold 100)SeqScan(t1)MergeJoin(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;

-- No. A-12-1-2
-- No. A-12-2-2
SELECT name, setting FROM settings;
SET pg_hint_plan.parse_messages TO error;
/*+Set(enable_seqscan off)Set(geqo_threshold 100)SeqScan(t1)MergeJoin(t1 t2)NestLoop(t1 t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
SELECT name, setting FROM settings;
EXPLAIN (COSTS false) EXECUTE p1;

-- No. A-12-1-3
-- No. A-12-2-3
SELECT name, setting FROM settings;
SET pg_hint_plan.parse_messages TO error;
EXPLAIN (COSTS false) EXECUTE p2;
/*+Set(enable_seqscan off)Set(geqo_threshold 100)SeqScan(t1)MergeJoin(t1 t2)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
EXPLAIN (COSTS false) EXECUTE p1;
SELECT name, setting FROM settings;

-- No. A-12-1-4
-- No. A-12-2-4
SELECT name, setting FROM settings;
SET pg_hint_plan.parse_messages TO error;
EXPLAIN (COSTS false) EXECUTE p2;
EXPLAIN (COSTS false) EXECUTE p1;
SELECT name, setting FROM settings;

DEALLOCATE p1;
SET pg_hint_plan.parse_messages TO LOG;

----
---- No. A-12-3 effective range of the hint
----

EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;

-- No. A-12-3-1
SET enable_indexscan TO off;
SET enable_mergejoin TO off;
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
SELECT name, setting FROM settings;
/*+Set(enable_indexscan on)Set(geqo_threshold 100)IndexScan(t2)MergeJoin(t1 t2)Leading(t2 t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
SELECT name, setting FROM settings;
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;

-- No. A-12-3-2
SET enable_indexscan TO off;
SET enable_mergejoin TO off;
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
SELECT name, setting FROM settings;
BEGIN;
/*+Set(enable_indexscan on)Set(geqo_threshold 100)IndexScan(t2)MergeJoin(t1 t2)Leading(t2 t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
COMMIT;
BEGIN;
SELECT name, setting FROM settings;
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
COMMIT;

-- No. A-12-3-3
SET enable_indexscan TO off;
SET enable_mergejoin TO off;
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
SELECT name, setting FROM settings;
/*+Set(enable_indexscan on)Set(geqo_threshold 100)IndexScan(t2)MergeJoin(t1 t2)Leading(t2 t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;
\connect
SET enable_indexscan TO off;
SET enable_mergejoin TO off;
LOAD 'pg_hint_plan';
SELECT name, setting FROM settings;
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;

SET pg_hint_plan.enable_hint TO on;
SET pg_hint_plan.debug_print TO on;
SET client_min_messages TO LOG;
SET search_path TO public;
RESET enable_indexscan;
RESET enable_mergejoin;

----
---- No. A-13 call planner recursively
----

CREATE OR REPLACE FUNCTION nested_planner(cnt int) RETURNS int AS $$
DECLARE
    new_cnt int;
BEGIN
    RAISE NOTICE 'nested_planner(%)', cnt;

    /* 再帰終了の判断 */
    IF cnt <= 1 THEN
        RETURN 0;
    END IF;

	SELECT /*+ IndexScan(t_1) */ nested_planner(cnt - 1) INTO new_cnt
	  FROM s1.t1 t_1
	  JOIN s1.t2 t_2 ON (t_1.c1 = t_2.c1)
	 ORDER BY t_1.c1 LIMIT 1;

    RETURN new_cnt;
END;
$$ LANGUAGE plpgsql IMMUTABLE;

----
---- No. A-13-2 use hint of main query
----

--No.13-2-1
EXPLAIN (COSTS false) SELECT nested_planner(1) FROM s1.t1 t_1 ORDER BY t_1.c1;
/*+SeqScan(t_1)*/
EXPLAIN (COSTS false) SELECT nested_planner(1) FROM s1.t1 t_1 ORDER BY t_1.c1;

----
---- No. A-13-3 output number of times of debugging log
----

--No.13-3-1
EXPLAIN (COSTS false) SELECT nested_planner(1) FROM s1.t1 t_1 ORDER BY t_1.c1;
/*+SeqScan(t_2)*/
EXPLAIN (COSTS false) SELECT nested_planner(1) FROM s1.t1 t_1 ORDER BY t_1.c1;

--No.13-3-2
EXPLAIN (COSTS false) SELECT nested_planner(2) FROM s1.t1 t_1 ORDER BY t_1.c1;
/*+SeqScan(t_2)*/
EXPLAIN (COSTS false) SELECT nested_planner(2) FROM s1.t1 t_1 ORDER BY t_1.c1;

--No.13-3-3
--
-- Redefine not to use cached plan
--
CREATE OR REPLACE FUNCTION nested_planner(cnt int) RETURNS int AS $$
DECLARE
    new_cnt int;
BEGIN
    RAISE NOTICE 'nested_planner(%)', cnt;

    /* 再帰終了の判断 */
    IF cnt <= 1 THEN
        RETURN 0;
    END IF;

	SELECT /*+ IndexScan(t_1) */ nested_planner(cnt - 1) INTO new_cnt
	  FROM s1.t1 t_1
	  JOIN s1.t2 t_2 ON (t_1.c1 = t_2.c1)
	 ORDER BY t_1.c1 LIMIT 1;

    RETURN new_cnt;
END;
$$ LANGUAGE plpgsql IMMUTABLE;

-- The function called at the bottom desn't use a hint, the immediate
-- caller level should restore its own hint. So, the first LOG from
-- pg_hint_plan should use the IndexScan(t_1) hint
EXPLAIN (COSTS false) SELECT nested_planner(5) FROM s1.t1 t_1 ORDER BY t_1.c1;

-- The top level uses SeqScan(t_1), but the function should use only
-- the hint in the function.
/*+SeqScan(t_1) SeqScan(t_2)*/
EXPLAIN (COSTS false) SELECT nested_planner(5) FROM s1.t1 t_1 ORDER BY t_1.c1;

----
---- No. A-13-4 output of debugging log on hint status
----
CREATE OR REPLACE FUNCTION recall_planner() RETURNS int AS $$
	SELECT /*+ IndexScan(t_1) */t_1.c1
	  FROM s1.t1 t_1
	  JOIN s1.t2 t_2 ON (t_1.c1 = t_2.c1)
	 ORDER BY t_1.c1 LIMIT 1;
$$ LANGUAGE SQL IMMUTABLE;

--No.13-4-1
-- recall_planner() is reduced to constant while planning using the
-- hint defined in the function. Then the outer query is planned based
-- on the following hint. pg_hint_plan shows the log for the function
-- but the resulting explain output doesn't contain the corresponding
-- plan.
/*+HashJoin(t_1 t_2)*/
EXPLAIN (COSTS false)
 SELECT recall_planner() FROM s1.t1 t_1
   JOIN s1.t2 t_2 ON (t_1.c1 = t_2.c1)
  ORDER BY t_1.c1;

--No.13-4-2
--See description for No.13-4-1
/*+HashJoin(st_1 st_2)*/
EXPLAIN (COSTS false)
 SELECT recall_planner() FROM s1.t1 st_1
   JOIN s1.t2 st_2 ON (st_1.c1 = st_2.c1)
  ORDER BY st_1.c1;

--No.13-4-3
--See description for No.13-4-1
/*+HashJoin(t_1 t_2)*/
EXPLAIN (COSTS false)
 SELECT recall_planner() FROM s1.t1 st_1
   JOIN s1.t2 st_2 ON (st_1.c1 = st_2.c1)
  ORDER BY st_1.c1;

--No.13-4-4
--See description for No.13-4-1
/*+HashJoin(st_1 st_2)*/
EXPLAIN (COSTS false)
 SELECT recall_planner() FROM s1.t1 t_1
   JOIN s1.t2 t_2 ON (t_1.c1 = t_2.c1)
  ORDER BY t_1.c1;

--No.13-4-5
-- See description for No.13-4-1. No joins in ths plan, so
-- pg_hint_plan doesn't complain on the wrongly written error hint.
/*+HashJoin(t_1 t_1)*/
EXPLAIN (COSTS false)
 SELECT recall_planner() FROM s1.t1 t_1
  ORDER BY t_1.c1;

--No.13-4-6
CREATE OR REPLACE FUNCTION recall_planner_one_t() RETURNS int AS $$
	SELECT /*+ IndexScan(t_1) */t_1.c1
	  FROM s1.t1 t_1
	 ORDER BY t_1.c1 LIMIT 1;
$$ LANGUAGE SQL IMMUTABLE;

EXPLAIN (COSTS false)
 SELECT recall_planner_one_t() FROM s1.t1 t_1
   JOIN s1.t2 t_2 ON (t_1.c1 = t_2.c1)
  ORDER BY t_1.c1;
/*+HashJoin(t_1 t_1)*/
EXPLAIN (COSTS false)
 SELECT recall_planner_one_t() FROM s1.t1 t_1
   JOIN s1.t2 t_2 ON (t_1.c1 = t_2.c1)
  ORDER BY t_1.c1;

DROP FUNCTION recall_planner_one_t(int);

--No.13-4-7
-- See description for No.13-4-1. Complains on the wrongly written hint.
/*+HashJoin(t_1 t_1)*/
EXPLAIN (COSTS false)
 SELECT recall_planner() FROM s1.t1 t_1
   JOIN s1.t2 t_2 ON (t_1.c1 = t_2.c1)
  ORDER BY t_1.c1;

--No.13-4-8
/*+MergeJoin(t_1 t_2)HashJoin(t_1 t_2)*/
EXPLAIN (COSTS false)
 SELECT recall_planner() FROM s1.t1 t_1
   JOIN s1.t2 t_2 ON (t_1.c1 = t_2.c1)
  ORDER BY t_1.c1;

--No.14-1-1 plancache invalidation
CREATE TABLE s1.tpc AS SELECT a FROM generate_series(0, 999) a;
CREATE INDEX ON s1.tpc(a);
PREPARE p1 AS SELECT * FROM s1.tpc WHERE a < 999;
/*+ IndexScan(tpc) */PREPARE p2 AS SELECT * FROM s1.tpc WHERE a < 999;
/*+ SeqScan(tpc) */PREPARE p3(int) AS SELECT * FROM s1.tpc WHERE a = $1;
EXPLAIN (COSTS false) EXECUTE p1;
EXPLAIN (COSTS false) EXECUTE p2;
EXPLAIN (COSTS false) EXECUTE p3(500);
-- The DROP invalidates the plan caches
DROP TABLE s1.tpc;
CREATE TABLE s1.tpc AS SELECT a FROM generate_series(0, 999) a;
CREATE INDEX ON s1.tpc(a);
EXPLAIN (COSTS false) EXECUTE p1;
EXPLAIN (COSTS false) EXECUTE p2;
EXPLAIN (COSTS false) EXECUTE p3(500);
DEALLOCATE p1;
DEALLOCATE p2;
DEALLOCATE p3;
DROP TABLE s1.tpc;

--No.14-1-2 PREPARE query with array parameters
PREPARE test_query(numeric[]) AS
  /*+ MergeJoin(t1 t2) */ WITH test AS
    (SELECT 1 AS x)
  SELECT t1.* FROM test t1, test t2
    WHERE t1.x = ANY($1) AND t1.x = t2.x;
EXPLAIN (COSTS false) EXECUTE test_query(array[1,2,3]);
