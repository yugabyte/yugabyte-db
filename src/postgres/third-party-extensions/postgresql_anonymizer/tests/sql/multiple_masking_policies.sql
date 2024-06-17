-- This cant be done in a transaction
ALTER SYSTEM SET anon.masking_policies = 'anon, rgpd';

BEGIN;

CREATE EXTENSION anon CASCADE;

SAVEPOINT init;

-- Error
SET anon.masking_policies = '';
ROLLBACK TO init;

-- Error
SET anon.masking_policies = ',,,,';
ROLLBACK TO init;

SELECT anon.init_masking_policies();

SELECT COUNT(*)=2 FROM pg_seclabels WHERE provider='gdpr';

CREATE ROLE zoe;
SECURITY LABEL FOR gdpr ON ROLE zoe IS 'MASKED';

SELECT COUNT(*)=3 FROM pg_seclabels WHERE provider='gdpr';

ROLLBACK;

ALTER SYSTEM RESET anon.masking_policies;
