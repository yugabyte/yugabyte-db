SELECT jsonb_agg(res)
    FROM
      (
      SELECT t.child_table as "tableName", t.child_schema as "schemaName", pt.tablespace as "tablespace", t.parent_table as "parentTable",
             t.parent_schema as "parentSchema", pt_parent.tablespace as "parentTablespace"
        FROM 
        (SELECT nmsp_parent.nspname AS parent_schema, parent.relname AS parent_table,
               nmsp_child.nspname AS child_schema, child.relname AS child_table 
          FROM pg_inherits 
          JOIN pg_class parent 
            ON pg_inherits.inhparent = parent.oid 
          JOIN pg_class child 
            ON pg_inherits.inhrelid = child.oid 
          JOIN pg_namespace nmsp_parent  
            ON nmsp_parent.oid  = parent.relnamespace 
          JOIN pg_namespace nmsp_child 
            ON nmsp_child.oid   = child.relnamespace) AS t 
          JOIN pg_tables pt 
            ON pt.schemaname = t.child_schema 
            AND pt.tablename = t.child_table 
          JOIN pg_tables pt_parent 
            ON pt_parent.schemaname = t.parent_schema 
            AND pt_parent.tablename = t.parent_table) AS res