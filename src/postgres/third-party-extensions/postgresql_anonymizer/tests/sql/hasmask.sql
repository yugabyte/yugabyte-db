BEGIN;

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

SELECT anon.start_dynamic_masking();

CREATE ROLE "WOPR";
SECURITY LABEL FOR anon ON ROLE "WOPR" IS 'MASKED';

CREATE ROLE hal LOGIN;
SECURITY LABEL FOR anon ON ROLE hal IS 'MASKED';

CREATE ROLE jarvis;

-- FORCE update because COMMENT doesn't trigger the Event Trigger
SELECT anon.mask_update();

SELECT anon.hasmask('"WOPR"') IS TRUE;

SELECT anon.hasmask('hal') IS TRUE;

SELECT anon.hasmask('jarvis') IS FALSE;

SELECT anon.hasmask('postgres') IS FALSE;

SELECT anon.hasmask(NULL) IS FALSE;

-- Must return an error
SELECT anon.hasmask('does_not_exist');

ROLLBACK;

