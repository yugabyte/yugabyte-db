LOAD 'pg_hint_plan';
SET pg_hint_plan.enable TO on;
SET pg_hint_plan.debug_print TO on;
SET client_min_messages TO LOG;
SET search_path TO public;

--
-- No. 5-1 hint format
--

-- No. 5-1-1
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-1-2
/* +SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-1-3
--+SeqScan(t1)
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-1-4
--+SeqScan(t1)
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-1-5
-- +SeqScan(t1)
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-1-6
--SeqScan(t1)
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-1-7
/*+SeqScan(t1) /* nest comment */ */
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-1-8
/* +SeqScan(t1) /* nest comment */ */
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-1-9
/*SeqScan(t1) /* nest comment */ */
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

--
-- No. 5-2 hint position
--

-- No. 5-2-1
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-2-2
/* normal comment */
/*+SeqScan(t1)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-2-3
EXPLAIN (COSTS false) SELECT /*+SeqScan(t1)*/ * FROM s1.t1 WHERE t1.c1 = 1;

--
-- No. 5-4 hint delimiter
--

EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;
-- No. 5-4-1
-- No. 5-4-2
-- No. 5-4-3
-- No. 5-4-4
-- No. 5-4-5
-- No. 5-4-6
-- No. 5-4-7
/*+Set(enable_indexscan"off")Set(enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-4-8
/*+ Set(enable_indexscan"off")Set(enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-4-9
/*+Set(enable_indexscan"off")Set(enable_bitmapscan"off") */
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-4-10
/*+ Set (enable_indexscan"off") Set (enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-4-11
/*+Set ( enable_indexscan"off")Set ( enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-4-12
/*+Set(enable_indexscan"off" ) Set(enable_bitmapscan"off" ) */
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-4-13
/*+Set( enable_indexscan "off" )Set( enable_bitmapscan "off" )*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-4-14
/*+ Set ( enable_indexscan "off" ) Set ( enable_bitmapscan "off" ) */
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-4-15
/*+	Set(enable_indexscan"off")Set(enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-4-16
/*+Set(enable_indexscan"off")Set(enable_bitmapscan"off")	*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-4-17
/*+	Set	(enable_indexscan"off")	Set	(enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-4-18
/*+Set	(	enable_indexscan"off")Set	(	enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-4-19
/*+Set(enable_indexscan"off"	)	Set(enable_bitmapscan"off"	)	*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-4-20
/*+Set(	enable_indexscan	"off"	)Set(	enable_bitmapscan	"off"	)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-4-21
/*+	Set	(	enable_indexscan	"off"	)	Set	(	enable_bitmapscan	"off"	)	*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-4-22
/*+
Set(enable_indexscan"off")Set(enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-4-23
/*+Set(enable_indexscan"off")Set(enable_bitmapscan"off")
*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-4-24
/*+
Set
(enable_indexscan"off")
Set
(enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-4-25
/*+Set
(
enable_indexscan"off")Set
(
enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-4-26
/*+Set(enable_indexscan"off"
)
Set(enable_bitmapscan"off"
)
*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-4-27
/*+Set(
enable_indexscan
"off"
)Set(
enable_bitmapscan
"off"
)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-4-28
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

-- No. 5-4-29
/*+ 	
	 Set(enable_indexscan"off")Set(enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-4-30
/*+Set(enable_indexscan"off")Set(enable_bitmapscan"off") 	
	 */
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-4-31
/*+ 	
	 Set 	
	 (enable_indexscan"off") 	
	 Set 	
	 (enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-4-32
/*+Set 	
	 ( 	
	 enable_indexscan"off")Set 	
	 ( 	
	 enable_bitmapscan"off")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-4-33
/*+Set(enable_indexscan"off" 	
	 ) 	
	 Set(enable_bitmapscan"off" 	
	 ) 	
	 */
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-4-34
/*+Set( 	
	 enable_indexscan 	
	 "off" 	
	 )Set( 	
	 enable_bitmapscan 	
	 "off" 	
	 )*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 WHERE t1.c1 = 1;

-- No. 5-4-35
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

--
-- No. 5-5 hint object pattern
--

-- No. 5-5-1
/*+SeqScan(t)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 t WHERE t.c1 = 1;
/*+SeqScan(ttt)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 ttt WHERE ttt.c1 = 1;

-- No. 5-5-2
/*+SeqScan(T)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "T" WHERE "T".c1 = 1;
/*+SeqScan(TTT)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "TTT" WHERE "TTT".c1 = 1;

-- No. 5-5-3
/*+SeqScan(()*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "(" WHERE "(".c1 = 1;
/*+SeqScan(((()*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "(((" WHERE "(((".c1 = 1;

-- No. 5-5-4
/*+SeqScan())*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 ")" WHERE ")".c1 = 1;
/*+SeqScan(")")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 ")" WHERE ")".c1 = 1;
/*+SeqScan(")))")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 ")))" WHERE ")))".c1 = 1;

-- No. 5-5-5
/*+SeqScan(")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 """" WHERE """".c1 = 1;
/*+SeqScan("""")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 """" WHERE """".c1 = 1;
/*+SeqScan("""""""")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 """""""" WHERE """""""".c1 = 1;

-- No. 5-5-6
/*+SeqScan( )*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 " " WHERE " ".c1 = 1;
/*+SeqScan(" ")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 " " WHERE " ".c1 = 1;
/*+SeqScan("   ")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "   " WHERE "   ".c1 = 1;

-- No. 5-5-7
/*+SeqScan(	)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "	" WHERE "	".c1 = 1;
/*+SeqScan("	")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "	" WHERE "	".c1 = 1;
/*+SeqScan("			")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "			" WHERE "			".c1 = 1;

-- No. 5-5-8
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

-- No. 5-5-9
/*+SeqScan(Set)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "Set" WHERE "Set".c1 = 1;
/*+SeqScan("Set SeqScan Leading")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "Set SeqScan Leading" WHERE "Set SeqScan Leading".c1 = 1;

-- No. 5-5-10
/*+SeqScan(あ)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 あ WHERE あ.c1 = 1;
/*+SeqScan(あいう)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 あいう WHERE あいう.c1 = 1;

-- No. 5-5-11
/*+SeqScan(/**/)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "/**/" WHERE "/**/".c1 = 1;
/*+SeqScan(/**//**//**/)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "/**//**//**/" WHERE "/**//**//**/".c1 = 1;

-- No. 5-5-12
/*+SeqScan("tT()"" 	
Set/**/あ")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "tT()"" 	
Set/**/あ" WHERE "tT()"" 	
Set/**/あ".c1 = 1;

/*+SeqScan("tT()"" 	
Setあ")*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1 "tT()"" 	
Setあ" WHERE "tT()"" 	
Setあ".c1 = 1;

