-- This test cannot be run in a single transaction
-- This test must be run on a database named 'contrib_regression'

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

-- INIT

SET anon.maskschema TO 'foo';

SELECT anon.start_dynamic_masking();

CREATE ROLE skynet LOGIN PASSWORD 'x';

SECURITY LABEL FOR anon ON ROLE skynet IS 'MASKED';

SELECT anon.mask_update();

-- search_path must be 'foo,public'
\! PGPASSWORD=x psql contrib_regression -U skynet -c 'SHOW search_path;'


CREATE ROLE hal LOGIN PASSWORD 'x';

SECURITY LABEL FOR anon ON ROLE hal IS 'MASKED';

SELECT anon.mask_update();

-- search_path must be 'foo,public'
\! PGPASSWORD=x psql contrib_regression -U hal -c 'SHOW search_path;'

-- STOP

SELECT anon.stop_dynamic_masking();

-- REMOVE MASKS
SELECT anon.remove_masks_for_all_roles();
SELECT COUNT(*)=0 FROM anon.pg_masked_roles WHERE hasmask;

--  CLEAN

DROP EXTENSION anon CASCADE;

REASSIGN OWNED BY skynet TO postgres;
DROP OWNED BY skynet CASCADE;
DROP ROLE skynet;

REASSIGN OWNED BY hal TO postgres;
DROP OWNED BY hal CASCADE;
DROP ROLE hal;
