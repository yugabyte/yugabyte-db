DO
$redefineview$
    BEGIN
        IF NOT EXISTS (SELECT TRUE
                       FROM pg_attribute
                       WHERE attrelid = 'pg_catalog.pg_locks'::regclass
                         AND attname = 'ybdetails'
                         AND NOT attisdropped) THEN
            CREATE OR REPLACE VIEW pg_catalog.pg_locks
                WITH (use_initdb_acl = true)
            AS
            SELECT l.locktype,
                   l.database,
                   l.relation,
                   null::int                  AS page,
                   null::smallint             AS tuple,
                   null::text                 AS virtualxid,
                   null::xid                  AS transactionid,
                   null::oid                  AS classid,
                   null::oid                  AS objid,
                   null::smallint             AS objsubid,
                   null::text                 AS virtualtransaction,
                   l.pid,
                   array_to_string(mode, ',') AS mode,
                   l.granted,
                   l.fastpath,
                   l.waitstart,
                   l.waitend,
                   jsonb_build_object('node', l.node,
                                      'transactionid', l.transaction_id,
                                      'subtransaction_id', l.subtransaction_id,
                                      'is_explicit', l.is_explicit,
                                      'tablet_id', l.tablet_id,
                                      'blocked_by', l.blocked_by,
                                      'keyrangedetails', jsonb_build_object(
                                              'cols', to_jsonb(l.hash_cols || l.range_cols),
                                              'attnum', l.attnum,
                                              'column_id', l.column_id,
                                              'multiple_rows_locked', l.multiple_rows_locked
                                          )
                       )                      AS ybdetails
            FROM pg_catalog.yb_lock_status(null, null) AS l;

        END IF;
    END;
$redefineview$;
