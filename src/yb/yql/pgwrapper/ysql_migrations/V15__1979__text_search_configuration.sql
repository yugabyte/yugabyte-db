SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;

DO $$
DECLARE
  template_snowball_oid oid;
  proc_oids oid array;
  noop int;
BEGIN
  IF NOT EXISTS (
    SELECT FROM pg_catalog.pg_ts_template WHERE tmplname = 'snowball'
  ) THEN
    -- pg_proc
    --
    -- Unfortunately we can't put this into the CTE below because then
    -- yb_non_ddl_txn_for_sys_tables_allowed prevents us from resolving regproc reference in
    -- pg_ts_template
    WITH proc_oids_from_query (oid) AS (
      INSERT INTO pg_catalog.pg_proc (
        proname, pronamespace, proowner, prolang, procost, prorows, provariadic, protransform,
        prokind, prosecdef, proleakproof, proisstrict, proretset, provolatile, proparallel, pronargs,
        pronargdefaults, prorettype, proargtypes, proallargtypes, proargmodes, proargnames,
        proargdefaults, protrftypes, prosrc, probin, proconfig, proacl
      ) VALUES
        ('dsnowball_init', 11, 10, 13, 1, 0, 0, '-', 'f', false, false, true, false,
        'v', 'u', 1, 0, 2281, '2281', NULL, NULL, NULL,
        NULL, NULL, 'dsnowball_init', '$libdir/dict_snowball', NULL, NULL),
        ('dsnowball_lexize', 11, 10, 13, 1, 0, 0, '-', 'f', false, false, true, false,
        'v', 'u', 4, 0, 2281, '2281 2281 2281 2281', NULL, NULL, NULL,
        NULL, NULL, 'dsnowball_lexize', '$libdir/dict_snowball', NULL, NULL)
      RETURNING oid
    )
    SELECT array_agg(oid) FROM proc_oids_from_query INTO proc_oids;

    -- pg_ts_template
    INSERT INTO pg_catalog.pg_ts_template (
      tmplname, tmplnamespace, tmplinit, tmpllexize
    ) VALUES
      ('snowball', 11, 'dsnowball_init', 'dsnowball_lexize')
    RETURNING oid
    INTO template_snowball_oid;

    -- Use one huge CTE block to aggregate everything text search related.
    -- This is used because yb_non_ddl_txn_for_sys_tables_allowed doesn't let us use temp tables.
    WITH
      -- Raw text search config values without auto-generated OIDs.
      -- Note that Russian uses english_stem as ASCII dictionary (see
      -- src/postgres/src/backend/snowball/Makefile for detials).
      ts_raw_values (
        row_number, cfgname, configs_with_own_dict, configs_with_english_dict
      ) AS (
        VALUES
          (1,  'danish',     ARRAY[1, 2, 10, 11, 16, 17], ARRAY[]::int[]),
          (2,  'dutch',      ARRAY[1, 2, 10, 11, 16, 17], ARRAY[]::int[]),
          (3,  'english',    ARRAY[1, 2, 10, 11, 16, 17], ARRAY[]::int[]),
          (4,  'finnish',    ARRAY[1, 2, 10, 11, 16, 17], ARRAY[]::int[]),
          (5,  'french',     ARRAY[1, 2, 10, 11, 16, 17], ARRAY[]::int[]),
          (6,  'german',     ARRAY[1, 2, 10, 11, 16, 17], ARRAY[]::int[]),
          (7,  'hungarian',  ARRAY[1, 2, 10, 11, 16, 17], ARRAY[]::int[]),
          (8,  'italian',    ARRAY[1, 2, 10, 11, 16, 17], ARRAY[]::int[]),
          (9,  'norwegian',  ARRAY[1, 2, 10, 11, 16, 17], ARRAY[]::int[]),
          (10, 'portuguese', ARRAY[1, 2, 10, 11, 16, 17], ARRAY[]::int[]),
          (11, 'romanian',   ARRAY[1, 2, 10, 11, 16, 17], ARRAY[]::int[]),
          (12, 'russian',    ARRAY[2, 10, 17],            ARRAY[1, 11, 16]),
          (13, 'spanish',    ARRAY[1, 2, 10, 11, 16, 17], ARRAY[]::int[]),
          (14, 'swedish',    ARRAY[1, 2, 10, 11, 16, 17], ARRAY[]::int[]),
          (15, 'turkish',    ARRAY[1, 2, 10, 11, 16, 17], ARRAY[]::int[])
      ),

      -- pg_ts_config
      ts_config (ts_config_oid, cfgname) AS (
        INSERT INTO pg_catalog.pg_ts_config (
          cfgname, cfgnamespace, cfgowner, cfgparser
        ) SELECT cfgname, 11, 10, 3722 FROM ts_raw_values
        RETURNING oid, cfgname
      ),

      -- pg_ts_dict
      ts_dict_oids (ts_dict_oid) AS (
        INSERT INTO pg_catalog.pg_ts_dict (
          dictname, dictnamespace, dictowner, dicttemplate, dictinitoption
        )
        SELECT
          cfgname || '_stem',
          11,
          10,
          template_snowball_oid,
          CASE
            -- romanian does not have stopwords parameter value in dictinitoption
            WHEN cfgname = 'romanian' THEN 'language = ''' || cfgname || ''''
            ELSE 'language = ''' || cfgname || ''', stopwords = ''' || cfgname || ''''
          END
        FROM ts_raw_values
        RETURNING oid
      ),

      -- Aggregate everything we know about text seach configs
      ts_full (
        ts_config_oid, cfgname, ts_dict_oid, configs_with_own_dict, configs_with_english_dict) AS (
          SELECT
            ts_values_rn.ts_config_oid,
            ts_values_rn.cfgname,
            ts_dict_rn.ts_dict_oid,
            ts_raw_values.configs_with_own_dict,
            ts_raw_values.configs_with_english_dict
          FROM (
            SELECT
              ROW_NUMBER() OVER (ORDER BY ts_dict_oid) AS row_number,
              ts_dict_oid
            FROM ts_dict_oids
          ) ts_dict_rn
          INNER JOIN (
            SELECT
              ROW_NUMBER() OVER (ORDER BY ts_config_oid) AS row_number,
              ts_config_oid,
              cfgname
            FROM ts_config
          ) ts_values_rn ON ts_values_rn.row_number = ts_dict_rn.row_number
          INNER JOIN ts_raw_values ON ts_raw_values.row_number = ts_dict_rn.row_number
      ),

      ts_dict_english_oid AS (
        SELECT ts_dict_oid FROM ts_full WHERE cfgname = 'english'
      ),

      ts_configs (ts_config_oid, cfgname, tokentype, dict_oid_to_use) AS (
        SELECT
          ts_full.ts_config_oid,
          ts_full.cfgname,
          s.tokentype,
          CASE
            WHEN s.tokentype = ANY(ts_full.configs_with_own_dict)     THEN ts_full.ts_dict_oid
            WHEN s.tokentype = ANY(ts_full.configs_with_english_dict) THEN (TABLE ts_dict_english_oid)
            ELSE 3765::oid -- "simple" dictionary
          END
        FROM ts_full
        INNER JOIN (SELECT * FROM generate_series(1, 22)) s (tokentype)
        ON (s.tokentype <> 12 AND s.tokentype <> 13 AND s.tokentype <> 14)
      ),

      -- pg_ts_config_map
      _ignored_1 AS (
        INSERT INTO pg_catalog.pg_ts_config_map (
          mapcfg, maptokentype, mapseqno, mapdict
        ) SELECT ts_config_oid, tokentype, 1, dict_oid_to_use FROM ts_configs
      ),

      -- pg_description
      _ignored_2 AS (
        INSERT INTO pg_catalog.pg_description (
          objoid, classoid, objsubid, description
        ) SELECT objid, classoid, 0, description FROM (
          SELECT ts_dict_oid, 3600, 'snowball stemmer for ' || cfgname || ' language' FROM ts_full
          UNION ALL
          SELECT ts_config_oid, 3602, 'configuration for ' || cfgname || ' language' FROM ts_config
          UNION ALL
          VALUES (template_snowball_oid, 3764, 'snowball stemmer')
        ) t (objid, classoid, description)
      ),

      -- pg_depend
      _ignored_3 AS (
        INSERT INTO pg_catalog.pg_depend (
          classid, objid, objsubid, refclassid, refobjid, refobjsubid, deptype
        ) SELECT classid, objid, 0, refclassid, refobjid, 0, 'n' FROM (
          -- pg_ts_dict --> pg_ts_template
          SELECT 3600, ts_dict_oids.ts_dict_oid, 3764, template_snowball_oid FROM ts_dict_oids
          UNION ALL
          -- pg_ts_config --> pg_ts_dict
          SELECT 3602, ts_full.ts_config_oid, 3600, ts_dict_oid FROM ts_full
          UNION ALL
          -- pg_ts_config --> pg_ts_dict (russian)
          SELECT 3602, ts_config.ts_config_oid, 3600, (TABLE ts_dict_english_oid) FROM ts_config
          WHERE ts_config.cfgname = 'russian'
          UNION ALL
          -- pg_ts_template --> pg_proc
          SELECT 3764, template_snowball_oid, 1255, p.oid FROM unnest(proc_oids) p (oid)
        ) t (classid, objid, refclassid, refobjid)
      )
    SELECT 1 INTO noop;
  END IF;
END $$;
