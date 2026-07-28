CREATE OR REPLACE FUNCTION oracle.unistr(text)
RETURNS text
AS 'MODULE_PATHNAME','orafce_unistr'
LANGUAGE 'c';

do $$
BEGIN
  IF EXISTS(SELECT * FROM pg_settings WHERE name = 'server_version_num' AND setting::int >= 120000) THEN
    UPDATE pg_proc SET prosupport= 'varchar2_transform'::regproc::oid WHERE proname='varchar2';
  ELSE
    UPDATE pg_proc SET protransform= 'varchar2_transform'::regproc::oid WHERE proname='varchar2';
  END IF;
  INSERT INTO pg_depend (classid, objid, objsubid,
                         refclassid, refobjid, refobjsubid, deptype)
     VALUES('pg_proc'::regclass::oid, 'varchar2'::regproc::oid, 0,
            'pg_proc'::regclass::oid, 'varchar2_transform'::regproc::oid, 0, 'n');
END
$$;

do $$
BEGIN
  IF EXISTS(SELECT * FROM pg_settings WHERE name = 'server_version_num' AND setting::int >= 120000) THEN
    UPDATE pg_proc SET prosupport= 'nvarchar2_transform'::regproc::oid WHERE proname='nvarchar2';
  ELSE
    UPDATE pg_proc SET protransform= 'nvarchar2_transform'::regproc::oid WHERE proname='nvarchar2';
  END IF;
  INSERT INTO pg_depend (classid, objid, objsubid,
                         refclassid, refobjid, refobjsubid, deptype)
     VALUES('pg_proc'::regclass::oid, 'nvarchar2'::regproc::oid, 0,
            'pg_proc'::regclass::oid, 'nvarchar2_transform'::regproc::oid, 0, 'n');
END
$$;
