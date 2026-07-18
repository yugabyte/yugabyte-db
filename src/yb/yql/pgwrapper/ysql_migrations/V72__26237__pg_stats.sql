BEGIN;
  SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

DO $$
BEGIN
  IF NOT EXISTS (
    SELECT TRUE FROM pg_attribute
    WHERE attrelid = 'pg_catalog.pg_stats'::regclass
          AND attname = 'range_bounds_histogram'
          AND NOT attisdropped
  ) THEN
-- ATRewriteCatalogs does utilize allowSystemTableMods, which causes it to fail with error
-- "column "range_length_histogram" has pseudo-type anyarray".
-- So instead we drop and recreate the view.
    DROP VIEW pg_catalog.pg_stats;
    CREATE VIEW pg_catalog.pg_stats WITH (use_initdb_acl = true, security_barrier) AS
        SELECT
            nspname AS schemaname,
            relname AS tablename,
            attname AS attname,
            stainherit AS inherited,
            stanullfrac AS null_frac,
            stawidth AS avg_width,
            stadistinct AS n_distinct,
            CASE
                WHEN stakind1 = 1 THEN stavalues1
                WHEN stakind2 = 1 THEN stavalues2
                WHEN stakind3 = 1 THEN stavalues3
                WHEN stakind4 = 1 THEN stavalues4
                WHEN stakind5 = 1 THEN stavalues5
            END AS most_common_vals,
            CASE
                WHEN stakind1 = 1 THEN stanumbers1
                WHEN stakind2 = 1 THEN stanumbers2
                WHEN stakind3 = 1 THEN stanumbers3
                WHEN stakind4 = 1 THEN stanumbers4
                WHEN stakind5 = 1 THEN stanumbers5
            END AS most_common_freqs,
            CASE
                WHEN stakind1 = 2 THEN stavalues1
                WHEN stakind2 = 2 THEN stavalues2
                WHEN stakind3 = 2 THEN stavalues3
                WHEN stakind4 = 2 THEN stavalues4
                WHEN stakind5 = 2 THEN stavalues5
            END AS histogram_bounds,
            CASE
                WHEN stakind1 = 3 THEN stanumbers1[1]
                WHEN stakind2 = 3 THEN stanumbers2[1]
                WHEN stakind3 = 3 THEN stanumbers3[1]
                WHEN stakind4 = 3 THEN stanumbers4[1]
                WHEN stakind5 = 3 THEN stanumbers5[1]
            END AS correlation,
            CASE
                WHEN stakind1 = 4 THEN stavalues1
                WHEN stakind2 = 4 THEN stavalues2
                WHEN stakind3 = 4 THEN stavalues3
                WHEN stakind4 = 4 THEN stavalues4
                WHEN stakind5 = 4 THEN stavalues5
            END AS most_common_elems,
            CASE
                WHEN stakind1 = 4 THEN stanumbers1
                WHEN stakind2 = 4 THEN stanumbers2
                WHEN stakind3 = 4 THEN stanumbers3
                WHEN stakind4 = 4 THEN stanumbers4
                WHEN stakind5 = 4 THEN stanumbers5
            END AS most_common_elem_freqs,
            CASE
                WHEN stakind1 = 5 THEN stanumbers1
                WHEN stakind2 = 5 THEN stanumbers2
                WHEN stakind3 = 5 THEN stanumbers3
                WHEN stakind4 = 5 THEN stanumbers4
                WHEN stakind5 = 5 THEN stanumbers5
            END AS elem_count_histogram,
            CASE
                WHEN stakind1 = 6 THEN stavalues1
                WHEN stakind2 = 6 THEN stavalues2
                WHEN stakind3 = 6 THEN stavalues3
                WHEN stakind4 = 6 THEN stavalues4
                WHEN stakind5 = 6 THEN stavalues5
            END AS range_length_histogram,
            CASE
                WHEN stakind1 = 6 THEN stanumbers1[1]
                WHEN stakind2 = 6 THEN stanumbers2[1]
                WHEN stakind3 = 6 THEN stanumbers3[1]
                WHEN stakind4 = 6 THEN stanumbers4[1]
                WHEN stakind5 = 6 THEN stanumbers5[1]
            END AS range_empty_frac,
            CASE
                WHEN stakind1 = 7 THEN stavalues1
                WHEN stakind2 = 7 THEN stavalues2
                WHEN stakind3 = 7 THEN stavalues3
                WHEN stakind4 = 7 THEN stavalues4
                WHEN stakind5 = 7 THEN stavalues5
                END AS range_bounds_histogram
        FROM pg_statistic s JOIN pg_class c ON (c.oid = s.starelid)
            JOIN pg_attribute a ON (c.oid = attrelid AND attnum = s.staattnum)
            LEFT JOIN pg_namespace n ON (n.oid = c.relnamespace)
        WHERE NOT attisdropped
        AND has_column_privilege(c.oid, a.attnum, 'select')
        AND (c.relrowsecurity = false OR NOT row_security_active(c.oid));
  END IF;
END $$;

COMMIT;
