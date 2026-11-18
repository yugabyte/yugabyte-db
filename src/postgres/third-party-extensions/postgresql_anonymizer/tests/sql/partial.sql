BEGIN;

CREATE EXTENSION IF NOT EXISTS anon CASCADE;

-- TEST 1 : partial
SELECT anon.partial('abcdefgh',1,'xxxxxx',1) = 'axxxxxxh';

SELECT anon.partial(NULL,1,'xxxxxx',1) IS NULL;

SELECT anon.partial('1234567890',3,'*',3) = '123*890';

SELECT anon.partial('dnfjdnvljdnvjsdn',1,'ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ',0) = 'dZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ';

-- TEST 2 : partial_email

SELECT anon.partial_email('daamien@gmail.com') = 'da******@gm******.com';

SELECT anon.partial_email('big@ben.co.uk') = 'bi******@be******.uk';

SELECT anon.partial_email(NULL) IS NULL;


ROLLBACK;
