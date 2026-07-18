BEGIN;

CREATE EXTENSION anon CASCADE;

-- generalize_int4range
SELECT anon.generalize_int4range(42);
SELECT anon.generalize_int4range(42,3);
SELECT anon.generalize_int4range(NULL);
SELECT anon.generalize_int4range(NULL,3);
SELECT anon.generalize_int4range(NULL,NULL);

-- generalize_int8range
SELECT anon.generalize_int8range(4345646464646);
SELECT anon.generalize_int8range(4345646464646,10000000000);
SELECT anon.generalize_int8range(NULL);
SELECT anon.generalize_int8range(NULL,10000000000);
SELECT anon.generalize_int8range(NULL,NULL);

-- generalize_numrange
SELECT anon.generalize_numrange(0.36683);
SELECT anon.generalize_numrange(0.32378,1);
SELECT anon.generalize_numrange(NULL);
SELECT anon.generalize_numrange(NULL,4);
SELECT anon.generalize_numrange(NULL,NULL);

-- generalize_tsrange
SELECT anon.generalize_tsrange('19041107');
SELECT anon.generalize_tsrange(NULL);
SELECT anon.generalize_tsrange('19041107',NULL);
SELECT anon.generalize_tsrange(NULL,NULL);
SELECT anon.generalize_tsrange('19041107','microsecond');
SELECT anon.generalize_tsrange('19041107','millisecond');
SELECT anon.generalize_tsrange('19041107','second');
SELECT anon.generalize_tsrange('19041107','minute');
SELECT anon.generalize_tsrange('19041107','hour');
SELECT anon.generalize_tsrange('19041107','day');
SELECT anon.generalize_tsrange('19041107','week');
SELECT anon.generalize_tsrange('19041107','month');
SELECT anon.generalize_tsrange('19041107','year');
SELECT anon.generalize_tsrange('19041107','decade');
SELECT anon.generalize_tsrange('19041107','century');
SELECT anon.generalize_tsrange('19041107','millennium');

-- generalize_tstzrange
SELECT anon.generalize_tstzrange('19041107');
SELECT anon.generalize_tstzrange(NULL);
SELECT anon.generalize_tstzrange('19041107',NULL);
SELECT anon.generalize_tstzrange(NULL,NULL);
SELECT anon.generalize_tstzrange('19041107','microsecond');
SELECT anon.generalize_tstzrange('19041107','millisecond');
SELECT anon.generalize_tstzrange('19041107','second');
SELECT anon.generalize_tstzrange('19041107','minute');
SELECT anon.generalize_tstzrange('19041107','hour');
SELECT anon.generalize_tstzrange('19041107','day');
SELECT anon.generalize_tstzrange('19041107','week');
SELECT anon.generalize_tstzrange('19041107','month');
SELECT anon.generalize_tstzrange('19041107','year');
SELECT anon.generalize_tstzrange('19041107','decade');
SELECT anon.generalize_tstzrange('19041107','century');
SELECT anon.generalize_tstzrange('19041107','millennium');

-- generalize_daterange
SELECT anon.generalize_daterange('19041107');
SELECT anon.generalize_daterange(NULL);
SELECT anon.generalize_daterange('19041107',NULL);
SELECT anon.generalize_daterange(NULL,NULL);
SELECT anon.generalize_daterange('19041107','microsecond');
SELECT anon.generalize_daterange('19041107','millisecond');
SELECT anon.generalize_daterange('19041107','second');
SELECT anon.generalize_daterange('19041107','minute');
SELECT anon.generalize_daterange('19041107','hour');
SELECT anon.generalize_daterange('19041107','day');
SELECT anon.generalize_daterange('19041107','week');
SELECT anon.generalize_daterange('19041107','month');
SELECT anon.generalize_daterange('19041107','year');
SELECT anon.generalize_daterange('19041107','decade');
SELECT anon.generalize_daterange('19041107','century');
SELECT anon.generalize_daterange('19041107','millennium');



ROLLBACK;
