LOAD 'pg_hint_plan';
SET pg_hint_plan.enable TO on;
SET pg_hint_plan.debug_print TO on;
SET client_min_messages TO LOG;
SET search_path TO public;

---- TODO 後で適切な位置に移動する
----
---- No. J-3-4 hint state output
----

-- No. J-3-4-7
/*+Set(enable_indexscan off)NestLoop("")Set(enable_bitmapscan off)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

----
---- No. A-5-1 hint format
----

-- No. A-5-1-1
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-1-2
/* +SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-1-3
--+SeqScan(t1)
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-1-4
--+SeqScan(t1)
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-1-5
-- +SeqScan(t1)
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-1-6
--SeqScan(t1)
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-1-7
/*+SeqScan(t1) /* nest comment */ */
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-1-8
/* +SeqScan(t1) /* nest comment */ */
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-1-9
/*SeqScan(t1) /* nest comment */ */
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

----
---- No. A-5-2 hint position
----

-- No. A-5-2-1
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-2-2
/* normal comment */
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-2-3
EXPLAIN (COSTS false) SELECT /*+SeqScan(t1)*/ * FROM s1.t1 WHERE t1.c1 = 1;

----
---- No. A-5-4 hint delimiter
----

EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;
-- No. A-5-4-1
-- No. A-5-4-2
-- No. A-5-4-3
-- No. A-5-4-4
-- No. A-5-4-5
-- No. A-5-4-6
-- No. A-5-4-7
/*+Set(enable_indexscan"off")Set(enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-4-8
/*+ Set(enable_indexscan"off")Set(enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-4-9
/*+Set(enable_indexscan"off")Set(enable_bitmapscan"off") */
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-4-10
/*+ Set (enable_indexscan"off") Set (enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-4-11
/*+Set ( enable_indexscan"off")Set ( enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-4-12
/*+Set(enable_indexscan"off" ) Set(enable_bitmapscan"off" ) */
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-4-13
/*+Set( enable_indexscan "off" )Set( enable_bitmapscan "off" )*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-4-14
/*+ Set ( enable_indexscan "off" ) Set ( enable_bitmapscan "off" ) */
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-4-15
/*+	Set(enable_indexscan"off")Set(enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-4-16
/*+Set(enable_indexscan"off")Set(enable_bitmapscan"off")	*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-4-17
/*+	Set	(enable_indexscan"off")	Set	(enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-4-18
/*+Set	(	enable_indexscan"off")Set	(	enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-4-19
/*+Set(enable_indexscan"off"	)	Set(enable_bitmapscan"off"	)	*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-4-20
/*+Set(	enable_indexscan	"off"	)Set(	enable_bitmapscan	"off"	)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-4-21
/*+	Set	(	enable_indexscan	"off"	)	Set	(	enable_bitmapscan	"off"	)	*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-4-22
/*+
Set(enable_indexscan"off")Set(enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-4-23
/*+Set(enable_indexscan"off")Set(enable_bitmapscan"off")
*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-4-24
/*+
Set
(enable_indexscan"off")
Set
(enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-4-25
/*+Set
(
enable_indexscan"off")Set
(
enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-4-26
/*+Set(enable_indexscan"off"
)
Set(enable_bitmapscan"off"
)
*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-4-27
/*+Set(
enable_indexscan
"off"
)Set(
enable_bitmapscan
"off"
)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-4-28
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

-- No. A-5-4-29
/*+ 	
	 Set(enable_indexscan"off")Set(enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-4-30
/*+Set(enable_indexscan"off")Set(enable_bitmapscan"off") 	
	 */
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-4-31
/*+ 	
	 Set 	
	 (enable_indexscan"off") 	
	 Set 	
	 (enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-4-32
/*+Set 	
	 ( 	
	 enable_indexscan"off")Set 	
	 ( 	
	 enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-4-33
/*+Set(enable_indexscan"off" 	
	 ) 	
	 Set(enable_bitmapscan"off" 	
	 ) 	
	 */
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-4-34
/*+Set( 	
	 enable_indexscan 	
	 "off" 	
	 )Set( 	
	 enable_bitmapscan 	
	 "off" 	
	 )*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-4-35
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
---- No. A-5-5 hint object pattern
---- No. A-7-2 message object pattern
----

-- No. A-5-5-1
-- No. A-7-2-1
/*+SeqScan(t)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 t WHERE t.c1 = 1;
/*+SeqScan(ttt)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 ttt WHERE ttt.c1 = 1;
/*+SeqScan("t")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 t WHERE t.c1 = 1;
/*+SeqScan("ttt")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 ttt WHERE ttt.c1 = 1;

-- No. A-5-5-2
-- No. A-7-2-2
/*+SeqScan(T)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "T" WHERE "T".c1 = 1;
/*+SeqScan(TTT)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "TTT" WHERE "TTT".c1 = 1;
/*+SeqScan("T")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "T" WHERE "T".c1 = 1;
/*+SeqScan("TTT")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "TTT" WHERE "TTT".c1 = 1;

-- No. A-5-5-3
-- No. A-7-2-3
/*+SeqScan(()*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "(" WHERE "(".c1 = 1;
/*+SeqScan(((()*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "(((" WHERE "(((".c1 = 1;
/*+SeqScan("(")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "(" WHERE "(".c1 = 1;
/*+SeqScan("(((")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "(((" WHERE "(((".c1 = 1;

-- No. A-5-5-4
-- No. A-7-2-4
/*+SeqScan())*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 ")" WHERE ")".c1 = 1;
/*+SeqScan(")")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 ")" WHERE ")".c1 = 1;
/*+SeqScan(")))")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 ")))" WHERE ")))".c1 = 1;

-- No. A-5-5-5
-- No. A-7-2-5
/*+SeqScan(")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 """" WHERE """".c1 = 1;
/*+SeqScan("""")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 """" WHERE """".c1 = 1;
/*+SeqScan("""""""")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 """""""" WHERE """""""".c1 = 1;

-- No. A-5-5-6
-- No. A-7-2-6
/*+SeqScan( )*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 " " WHERE " ".c1 = 1;
/*+SeqScan(" ")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 " " WHERE " ".c1 = 1;
/*+SeqScan("   ")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "   " WHERE "   ".c1 = 1;

-- No. A-5-5-7
-- No. A-7-2-7
/*+SeqScan(	)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "	" WHERE "	".c1 = 1;
/*+SeqScan("	")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "	" WHERE "	".c1 = 1;
/*+SeqScan("			")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "			" WHERE "			".c1 = 1;

-- No. A-5-5-8
-- No. A-7-2-8
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

-- No. A-5-5-9
-- No. A-7-2-9
/*+SeqScan(Set)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "Set" WHERE "Set".c1 = 1;
/*+SeqScan("Set")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "Set" WHERE "Set".c1 = 1;
/*+SeqScan("Set SeqScan Leading")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "Set SeqScan Leading" WHERE "Set SeqScan Leading".c1 = 1;

-- No. A-5-5-10
-- No. A-7-2-10
/*+SeqScan(あ)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 あ WHERE あ.c1 = 1;
/*+SeqScan(あいう)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 あいう WHERE あいう.c1 = 1;
/*+SeqScan("あ")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 あ WHERE あ.c1 = 1;
/*+SeqScan("あいう")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 あいう WHERE あいう.c1 = 1;

-- No. A-5-5-11
-- No. A-7-2-11
/*+SeqScan(/**/)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "/**/" WHERE "/**/".c1 = 1;
/*+SeqScan(/**//**//**/)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "/**//**//**/" WHERE "/**//**//**/".c1 = 1;

-- No. A-5-5-12
-- No. A-7-2-12
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

----
---- No. A-5-6 hint parse error
----

-- No. A-5-6-1
/*+Set(enable_indexscan off)Set enable_tidscan off)Set(enable_bitmapscan off)SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-6-2
/*+Set(enable_indexscan off)Set(enable_tidscan off Set(enable_bitmapscan off)SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-6-3
/*+Set(enable_indexscan off)Set(enable_tidscan "off)Set(enable_bitmapscan off)SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-6-4
/*+Set(enable_indexscan off)SeqScan("")Set(enable_bitmapscan off)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-6-5
/*+Set(enable_indexscan off)NoSet(enable_tidscan off)Set(enable_bitmapscan off)SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-6-6
/*+Set(enable_indexscan off)"Set"(enable_tidscan off)Set(enable_bitmapscan off)SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-5-6-7
/*+Set(enable_indexscan off)Set(enable_tidscan /* value */off)Set(enable_bitmapscan off)SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

----
---- No. A-6-1 original GUC parameter
----

-- No. A-6-1-1
SET ROLE super_user;
SET pg_hint_plan.debug_print TO off;
SHOW pg_hint_plan.enable;
SHOW pg_hint_plan.debug_print;
SHOW pg_hint_plan.parse_messages;
SET pg_hint_plan.enable TO off;
SET pg_hint_plan.debug_print TO on;
SET pg_hint_plan.parse_messages TO error;
SHOW pg_hint_plan.enable;
SHOW pg_hint_plan.debug_print;
SHOW pg_hint_plan.parse_messages;
RESET pg_hint_plan.enable;
RESET pg_hint_plan.debug_print;
RESET pg_hint_plan.parse_messages;
SHOW pg_hint_plan.enable;
SHOW pg_hint_plan.debug_print;
SHOW pg_hint_plan.parse_messages;

-- No. A-6-1-2
SET ROLE normal_user;
SHOW pg_hint_plan.enable;
SHOW pg_hint_plan.debug_print;
SHOW pg_hint_plan.parse_messages;
SET pg_hint_plan.enable TO off;
SET pg_hint_plan.debug_print TO on;
SET pg_hint_plan.parse_messages TO error;
SHOW pg_hint_plan.enable;
SHOW pg_hint_plan.debug_print;
SHOW pg_hint_plan.parse_messages;
RESET pg_hint_plan.enable;
RESET pg_hint_plan.debug_print;
RESET pg_hint_plan.parse_messages;
SHOW pg_hint_plan.enable;
SHOW pg_hint_plan.debug_print;
SHOW pg_hint_plan.parse_messages;

RESET ROLE;

----
---- No. A-6-2 original GUC parameter pg_hint_plan.enable
----

-- No. A-6-2-1
SET pg_hint_plan.enable TO on;
SHOW pg_hint_plan.enable;
/*+Set(enable_indexscan off)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-6-2-2
SET pg_hint_plan.enable TO off;
SHOW pg_hint_plan.enable;
/*+Set(enable_indexscan off)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-6-2-3
SET pg_hint_plan.enable TO DEFAULT;
SHOW pg_hint_plan.enable;
/*+Set(enable_indexscan off)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-6-2-4
SET pg_hint_plan.enable TO enable;
SHOW pg_hint_plan.enable;

----
---- No. A-6-3 original GUC parameter pg_hint_plan.debug_print
----

-- No. A-6-3-1
SET pg_hint_plan.debug_print TO on;
SHOW pg_hint_plan.debug_print;
/*+Set(enable_indexscan off)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-6-3-2
SET pg_hint_plan.debug_print TO off;
SHOW pg_hint_plan.debug_print;
/*+Set(enable_indexscan off)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-6-3-3
SET pg_hint_plan.debug_print TO DEFAULT;
SHOW pg_hint_plan.debug_print;
/*+Set(enable_indexscan off)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-6-3-4
SET pg_hint_plan.debug_print TO enable;
SHOW pg_hint_plan.debug_print;

----
---- No. A-6-4 original GUC parameter pg_hint_plan.parse_messages
----

SET client_min_messages TO debug5;

-- No. A-6-4-1
SET pg_hint_plan.parse_messages TO debug5;
SHOW pg_hint_plan.parse_messages;
/*+Set*/SELECT 1;
SET client_min_messages TO debug4;
/*+Set*/SELECT 1;

-- No. A-6-4-2
SET pg_hint_plan.parse_messages TO debug4;
SHOW pg_hint_plan.parse_messages;
/*+Set*/SELECT 1;
SET client_min_messages TO debug3;
/*+Set*/SELECT 1;

-- No. A-6-4-3
SET pg_hint_plan.parse_messages TO debug3;
SHOW pg_hint_plan.parse_messages;
/*+Set*/SELECT 1;
SET client_min_messages TO debug2;
/*+Set*/SELECT 1;

-- No. A-6-4-4
SET pg_hint_plan.parse_messages TO debug2;
SHOW pg_hint_plan.parse_messages;
/*+Set*/SELECT 1;
SET client_min_messages TO debug1;
/*+Set*/SELECT 1;

-- No. A-6-4-5
SET pg_hint_plan.parse_messages TO debug1;
SHOW pg_hint_plan.parse_messages;
/*+Set*/SELECT 1;
SET client_min_messages TO log;
/*+Set*/SELECT 1;

-- No. A-6-4-6
SET pg_hint_plan.parse_messages TO log;
SHOW pg_hint_plan.parse_messages;
/*+Set*/SELECT 1;
SET client_min_messages TO info;
/*+Set*/SELECT 1;

-- No. A-6-4-7
SET pg_hint_plan.parse_messages TO info;
SHOW pg_hint_plan.parse_messages;
/*+Set*/SELECT 1;
SET client_min_messages TO notice;
/*+Set*/SELECT 1;

-- No. A-6-4-8
SET pg_hint_plan.parse_messages TO notice;
SHOW pg_hint_plan.parse_messages;
/*+Set*/SELECT 1;
SET client_min_messages TO warning;
/*+Set*/SELECT 1;

-- No. A-6-4-9
SET pg_hint_plan.parse_messages TO warning;
SHOW pg_hint_plan.parse_messages;
/*+Set*/SELECT 1;
SET client_min_messages TO error;
/*+Set*/SELECT 1;

-- No. A-6-4-10
SET pg_hint_plan.parse_messages TO error;
SHOW pg_hint_plan.parse_messages;
/*+Set*/SELECT 1;
SET client_min_messages TO fatal;
/*+Set*/SELECT 1;

-- No. A-6-4-11
RESET client_min_messages;
SET pg_hint_plan.parse_messages TO DEFAULT;
SHOW pg_hint_plan.parse_messages;
/*+Set*/SELECT 1;

-- No. A-6-4-12
SET pg_hint_plan.parse_messages TO fatal;
SHOW pg_hint_plan.parse_messages;

-- No. A-6-4-13
SET pg_hint_plan.parse_messages TO panic;
SHOW pg_hint_plan.parse_messages;

-- No. A-6-4-14
SET pg_hint_plan.parse_messages TO on;
SHOW pg_hint_plan.parse_messages;

----
---- No. A-7-1 parse error message output
----

-- No. A-7-1-1
/*+"Set"(enable_indexscan on)*/SELECT 1;
/*+Set()(enable_indexscan on)*/SELECT 1;
/*+Set(enable_indexscan on*/SELECT 1;

----
---- No. A-7-3 hint state output
----

SET pg_hint_plan.debug_print TO on;
SET client_min_messages TO LOG;

-- No. A-7-3-1
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-3-2
/*+SeqScan(no_table)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. A-7-3-3
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1 AND t1.ctid = '(1,1)';
/*+TidScan(t1)BitmapScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1 AND t1.ctid = '(1,1)';
/*+TidScan(t1)BitmapScan(t1)IndexScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1 AND t1.ctid = '(1,1)';
/*+TidScan(t1)BitmapScan(t1)IndexScan(t1)SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1 AND t1.ctid = '(1,1)';

-- No. A-7-3-4
/*+Set(enable_indexscan enable)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

----
---- No. A-8-1 hint state output
----

PREPARE p1 AS SELECT * FROM s1.t1 WHERE t1.c1 = 1;
EXPLAIN (COSTS false) EXECUTE p1;
DEALLOCATE p1;

PREPARE p1 AS SELECT * FROM s1.t1 WHERE t1.c1 < $1;
EXPLAIN (COSTS false) EXECUTE p1 (1);
DEALLOCATE p1;

-- No. A-8-1-1
-- No. A-8-1-2
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
EXPLAIN (COSTS false) EXECUTE p1 (1);
UPDATE pg_catalog.pg_class SET relpages = relpages WHERE relname = 't1';
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) EXECUTE p1 (1);
DEALLOCATE p1;

-- No. A-8-1-3
-- No. A-8-1-4
/*+SeqScan(t1)*/
PREPARE p1 AS SELECT * FROM s1.t1 WHERE t1.c1 = 1;
EXPLAIN (COSTS false) EXECUTE p1;
UPDATE pg_catalog.pg_class SET relpages = relpages WHERE relname = 't1';
EXPLAIN (COSTS false) EXECUTE p1;
DEALLOCATE p1;

/*+BitmapScan(t1)*/
PREPARE p1 AS SELECT * FROM s1.t1 WHERE t1.c1 < $1;
EXPLAIN (COSTS false) EXECUTE p1 (1);
UPDATE pg_catalog.pg_class SET relpages = relpages WHERE relname = 't1';
EXPLAIN (COSTS false) EXECUTE p1 (1);
DEALLOCATE p1;

-- No. A-8-1-5
-- No. A-8-1-6
PREPARE p1 AS SELECT * FROM s1.t1 WHERE t1.c1 = 1;
/*+BitmapScan(t1)*/
EXPLAIN (COSTS false) EXECUTE p1;
UPDATE pg_catalog.pg_class SET relpages = relpages WHERE relname = 't1';
/*+BitmapScan(t1)*/
EXPLAIN (COSTS false) EXECUTE p1;
DEALLOCATE p1;

PREPARE p1 AS SELECT * FROM s1.t1 WHERE t1.c1 < $1;
/*+BitmapScan(t1)*/
EXPLAIN (COSTS false) EXECUTE p1 (1);
UPDATE pg_catalog.pg_class SET relpages = relpages WHERE relname = 't1';
/*+BitmapScan(t1)*/
EXPLAIN (COSTS false) EXECUTE p1 (1);
DEALLOCATE p1;

----
---- No. A-8-4 EXECUTE statement name error
----

-- No. A-8-4-1
EXECUTE p1;
SHOW pg_hint_plan.debug_print;

----
---- No. A-9-5 EXECUTE statement name error
----

-- No. A-9-5-1
CREATE EXTENSION pg_stat_statements;
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
LOAD 'pg_hint_plan';
SELECT name, setting FROM settings;
EXPLAIN (COSTS false) SELECT * FROM s1.t1, s1.t2 WHERE t1.c1 = t2.c1;

SET pg_hint_plan.enable TO on;
SET pg_hint_plan.debug_print TO on;
SET client_min_messages TO LOG;
SET search_path TO public;

