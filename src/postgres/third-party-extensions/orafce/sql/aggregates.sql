SET search_path TO public, oracle;

-- Tests for the aggregate listagg
SELECT listagg(i::text) from generate_series(1,3) g(i);
SELECT listagg(i::text, ',') from generate_series(1,3) g(i);
SELECT coalesce(listagg(i::text), '<NULL>') from (SELECT ''::text) g(i);
SELECT coalesce(listagg(i::text), '<NULL>') from generate_series(1,0) g(i);
SELECT wm_concat(i::text) from generate_series(1,3) g(i);

-- Tests for the aggregate median( real | double )
CREATE FUNCTION checkMedianRealOdd()  RETURNS real AS $$
DECLARE
 med real;

BEGIN
	CREATE TABLE median_test (salary real);
        INSERT INTO median_test VALUES (4500);
        INSERT INTO median_test VALUES (NULL);
        INSERT INTO median_test VALUES (2100);
        INSERT INTO median_test VALUES (3600);
        INSERT INTO median_test VALUES (4000);
        SELECT into med median(salary) from median_test;
        DROP TABLE median_test;
        return med;
        
END;
$$ LANGUAGE plpgsql;

CREATE FUNCTION checkMedianRealEven() RETURNS real AS $$
DECLARE
 med real;

BEGIN
        CREATE TABLE median_test (salary real);
        INSERT INTO median_test VALUES (4500);
        INSERT INTO median_test VALUES (1500);
        INSERT INTO median_test VALUES (2100);
        INSERT INTO median_test VALUES (3600);
        INSERT INTO median_test VALUES (1000);
        INSERT INTO median_test VALUES (4000);
        select into med median(salary) from median_test;
        DROP TABLE median_test;
        return med;
END;
$$ LANGUAGE plpgsql;

CREATE FUNCTION checkMedianDoubleOdd() RETURNS double precision AS $$
DECLARE 
  med double precision;
BEGIN
        CREATE TABLE median_test (salary double precision);
        INSERT INTO median_test VALUES (4500);
        INSERT INTO median_test VALUES (1500);
        INSERT INTO median_test VALUES (2100);
        INSERT INTO median_test VALUES (3600);
        INSERT INTO median_test VALUES (4000);
        select into med median(salary) from median_test;
        DROP TABLE median_test;
        return med;
END;
$$ LANGUAGE plpgsql;

CREATE FUNCTION checkMedianDoubleEven() RETURNS double precision AS $$
DECLARE
 med double precision;

BEGIN
        CREATE TABLE median_test (salary double precision);
        INSERT INTO median_test VALUES (4500);
        INSERT INTO median_test VALUES (1500);
        INSERT INTO median_test VALUES (2100);
        INSERT INTO median_test VALUES (3600);
        INSERT INTO median_test VALUES (4000);
        INSERT INTO median_test VALUES (1000);
        select into med median(salary) from median_test;
        DROP TABLE median_test;
        return med;
END;
$$ LANGUAGE plpgsql;

SELECT checkMedianRealOdd();
SELECT checkMedianRealEven();
SELECT checkMedianDoubleOdd();
SELECT checkMedianDoubleEven();

DROP FUNCTION checkMedianRealOdd();
DROP FUNCTION checkMedianRealEven();
DROP FUNCTION checkMedianDoubleOdd();
DROP FUNCTION checkMedianDoubleEven();


