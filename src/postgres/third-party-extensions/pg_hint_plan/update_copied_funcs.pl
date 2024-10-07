#! /usr/bin/perl

use strict;

my $srcpath;
my @sources = (
	'src/backend/optimizer/path/allpaths.c',
	'src/backend/optimizer/path/joinrels.c');
my %defs =
  ('core.c'
   => {protos => [],
	   funcs => ['set_plain_rel_pathlist',
				 'standard_join_search',
				 'create_plain_partial_paths',
				 'join_search_one_level',
				 'make_rels_by_clause_joins',
				 'make_rels_by_clauseless_joins',
				 'join_is_legal',
				 'has_join_restriction',
				 'restriction_is_constant_false',
				 'build_child_join_sjinfo',
				 'get_matching_part_pairs',
				 'compute_partition_bounds',
				 'try_partitionwise_join'],
	   head => core_c_head()},
   'make_join_rel.c'
   => {protos => [],
	   funcs => ['make_join_rel',
				 'populate_joinrel_with_paths'],
	   head => make_join_rel_head()});

open (my $in, '-|', "objdump -W `which postgres`") || die "failed to objdump";
while (<$in>)
{
	if (/DW_AT_comp_dir .*: (.*\/)src\/backend\//)
	{
		$srcpath = $1;
		last;
	}
}
close($in);

die "source path not found" if (! defined $srcpath);
#printf("Source path = %s\n", $srcpath);

my %protos;
my %funcs;
my %func_is_static;
my %func_source;

for my $fname (@sources)
{
	my $f = $srcpath.$fname;
	my $source;

	open ($in, '<', $f) || die "failed to open $f: $!";
	while (<$in>)
	{
		$source .= $_;
	}

	## Collect static prototypes

	while ($source =~ /\n(static [^\(\)\{\}]*?(\w+)(\([^\{\);]+?\);))/gsm)
	{
		#	print "Prototype found: $2\n";
		$protos{$2} = $1;
	}

	## Collect function bodies

	while ($source =~ /(\n\/\*\n.+?\*\/\n(static )?(.+?)\n(.+?) *\(.*?\)\n\{.+?\n\}\n)/gsm)
	{
		$funcs{$4} = $1;
		$func_is_static{$4} = (defined $2);
		$func_source{$4} = $fname;

		  #	printf("Function found: %s$4\n", $func_is_static{$4} ? "static " : "");
	}

	close($in);
}


# Generate files
for my $fname (keys %defs)
{
	my %d = %{$defs{$fname}};

	my @protonames = @{$d{'protos'}};
	my @funcnames = @{$d{'funcs'}};
	my $head = $d{'head'};

	print "Generate $fname.\n";
	open (my $out, '>', $fname) || die "could not open $fname: $!";

	print $out $head;

	for (@protonames)
	{
		print " Prototype: $_\n";
		print $out "\n";
		die "Prototype for $_ not found" if (! defined $protos{$_});
		print $out $protos{$_};
	}

	for (@funcnames)
	{
		printf(" %s function: $_@%s\n",
			   $func_is_static{$_}?"static":"public", $func_source{$_});
		print $out "\n";
		die "Function body for $_ not found" if (! defined $funcs{$_});
		print $out $funcs{$_};
	}

	close($out);
}

# modify make_join_rel.c
patch_make_join_rel();

sub core_c_head()
{
	return << "EOS";
/*-------------------------------------------------------------------------
 *
 * core.c
 *	  Routines copied from PostgreSQL core distribution.
 *
 * The main purpose of this files is having access to static functions in core.
 * Another purpose is tweaking functions behavior by replacing part of them by
 * macro definitions. See at the end of pg_hint_plan.c for details. Anyway,
 * this file *must* contain required functions without making any change.
 *
 * This file contains the following functions from corresponding files.
 *
 * src/backend/optimizer/path/allpaths.c
 *
 *  public functions:
 *     standard_join_search(): This funcion is not static. The reason for
 *        including this function is make_rels_by_clause_joins. In order to
 *        avoid generating apparently unwanted join combination, we decided to
 *        change the behavior of make_join_rel, which is called under this
 *        function.
 *
 *	static functions:
 *	   set_plain_rel_pathlist()
 *	   create_plain_partial_paths()
 *
 * src/backend/optimizer/path/joinrels.c
 *
 *	public functions:
 *     join_search_one_level(): We have to modify this to call my definition of
 * 		    make_rels_by_clause_joins.
 *
 *	static functions:
 *     make_rels_by_clause_joins()
 *     make_rels_by_clauseless_joins()
 *     join_is_legal()
 *     has_join_restriction()
 *     restriction_is_constant_false()
 *     build_child_join_sjinfo()
 *     get_matching_part_pairs()
 *     compute_partition_bounds()
 *     try_partitionwise_join()
 *
 * Portions Copyright (c) 1996-2023, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */

#include "access/tsmapi.h"
#include "catalog/pg_operator.h"
#include "foreign/fdwapi.h"
EOS
}

sub make_join_rel_head
{
	return << "EOS";
/*-------------------------------------------------------------------------
 *
 * make_join_rel.c
 *	  Routines copied from PostgreSQL core distribution with some
 *	  modifications.
 *
 * src/backend/optimizer/path/joinrels.c
 *
 * This file contains the following functions from corresponding files.
 *
 *	static functions:
 *     make_join_rel()
 *     populate_joinrel_with_paths()
 *
 * Portions Copyright (c) 2013-2023, NIPPON TELEGRAPH AND TELEPHONE CORPORATION
 * Portions Copyright (c) 1996-2023, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *-------------------------------------------------------------------------
 */

/*
 * adjust_rows: tweak estimated row numbers according to the hint.
 */
static double
adjust_rows(double rows, RowsHint *hint)
{
	double		result = 0.0;	/* keep compiler quiet */

	if (hint->value_type == RVT_ABSOLUTE)
		result = hint->rows;
	else if (hint->value_type == RVT_ADD)
		result = rows + hint->rows;
	else if (hint->value_type == RVT_SUB)
		result =  rows - hint->rows;
	else if (hint->value_type == RVT_MULTI)
		result = rows * hint->rows;
	else
		Assert(false);	/* unrecognized rows value type */

	hint->base.state = HINT_STATE_USED;
	if (result < 1.0)
		ereport(WARNING,
				(errmsg("Force estimate to be at least one row, to avoid possible divide-by-zero when interpolating costs : %s",
					hint->base.hint_str)));
	result = clamp_row_est(result);
	elog(DEBUG1, "adjusted rows %d to %d", (int) rows, (int) result);

	return result;
}
EOS
}


sub patch_make_join_rel
{
	open(my $out, '|-', 'patch') || die "failed to open pipe: $!";

	print $out <<"EOS";
diff --git b/make_join_rel.c a/make_join_rel.c
index 0e7b99f..287e7f1 100644
--- b/make_join_rel.c
+++ a/make_join_rel.c
@@ -126,6 +126,84 @@ make_join_rel(PlannerInfo *root, RelOptInfo *rel1, RelOptInfo *rel2)
 	joinrel = build_join_rel(root, joinrelids, rel1, rel2, sjinfo,
 							 &restrictlist);

+	/* !!! START: HERE IS THE PART WHICH IS ADDED FOR PG_HINT_PLAN !!! */
+	{
+		RowsHint   *rows_hint = NULL;
+		int			i;
+		RowsHint   *justforme = NULL;
+		RowsHint   *domultiply = NULL;
+
+		/* Search for applicable rows hint for this join node */
+		for (i = 0; i < current_hint_state->num_hints[HINT_TYPE_ROWS]; i++)
+		{
+			rows_hint = current_hint_state->rows_hints[i];
+
+			/*
+			 * Skip this rows_hint if it is invalid from the first or it
+			 * doesn't target any join rels.
+			 */
+			if (!rows_hint->joinrelids ||
+				rows_hint->base.state == HINT_STATE_ERROR)
+				continue;
+
+			if (bms_equal(joinrelids, rows_hint->joinrelids))
+			{
+				/*
+				 * This joinrel is just the target of this rows_hint, so tweak
+				 * rows estimation according to the hint.
+				 */
+				justforme = rows_hint;
+			}
+			else if (!(bms_is_subset(rows_hint->joinrelids, rel1->relids) ||
+					   bms_is_subset(rows_hint->joinrelids, rel2->relids)) &&
+					 bms_is_subset(rows_hint->joinrelids, joinrelids) &&
+					 rows_hint->value_type == RVT_MULTI)
+			{
+				/*
+				 * If the rows_hint's target relids is not a subset of both of
+				 * component rels and is a subset of this joinrel, ths hint's
+				 * targets spread over both component rels. This menas that
+				 * this hint has been never applied so far and this joinrel is
+				 * the first (and only) chance to fire in current join tree.
+				 * Only the multiplication hint has the cumulative nature so we
+				 * apply only RVT_MULTI in this way.
+				 */
+				domultiply = rows_hint;
+			}
+		}
+
+		if (justforme)
+		{
+			/*
+			 * If a hint just for me is found, no other adjust method is
+			 * useles, but this cannot be more than twice becuase this joinrel
+			 * is already adjusted by this hint.
+			 */
+			if (justforme->base.state == HINT_STATE_NOTUSED)
+				joinrel->rows = adjust_rows(joinrel->rows, justforme);
+		}
+		else
+		{
+			if (domultiply)
+			{
+				/*
+				 * If we have multiple routes up to this joinrel which are not
+				 * applicable this hint, this multiply hint will applied more
+				 * than twice. But there's no means to know of that,
+				 * re-estimate the row number of this joinrel always just
+				 * before applying the hint. This is a bit different from
+				 * normal planner behavior but it doesn't harm so much.
+				 */
+				set_joinrel_size_estimates(root, joinrel, rel1, rel2, sjinfo,
+										   restrictlist);
+
+				joinrel->rows = adjust_rows(joinrel->rows, domultiply);
+			}
+
+		}
+	}
+	/* !!! END: HERE IS THE PART WHICH IS ADDED FOR PG_HINT_PLAN !!! */
+
 	/*
 	 * If we've already proven this join is empty, we needn't consider any
 	 * more paths for it.
EOS
}
