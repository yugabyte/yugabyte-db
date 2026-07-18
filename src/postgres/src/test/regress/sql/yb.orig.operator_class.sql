-- Create user type
CREATE TYPE user_type AS (
  i INT,
  v VARCHAR
);

SELECT typname FROM pg_type WHERE typname LIKE '%user_type';

CREATE TABLE user_table (id bigserial PRIMARY KEY, val user_type);

INSERT INTO user_table(val) VALUES ((1, 'aaaa')::user_type);
INSERT INTO user_table(val) VALUES ((10, 'aa')::user_type);
INSERT INTO user_table(val) VALUES ((2, 'b')::user_type);

-- Default ordering will be based on first base type ie. (i INT)
SELECT * FROM user_table ORDER BY val;

-- Create custom operators and comparison function for user_type
CREATE FUNCTION ut_lt(user_type, user_type) RETURNS boolean
AS $$BEGIN return $1.v < $2.v; END$$
LANGUAGE plpgsql immutable;

CREATE FUNCTION ut_lte(user_type, user_type) RETURNS boolean
AS $$BEGIN return $1.v <= $2.v; END$$
LANGUAGE plpgsql immutable;

CREATE FUNCTION ut_eq(user_type, user_type) RETURNS boolean
AS $$BEGIN return $1.v = $2.v; END$$
LANGUAGE plpgsql immutable;

CREATE FUNCTION ut_gt(user_type, user_type) RETURNS boolean
AS $$BEGIN return $1.v > $2.v; END$$
LANGUAGE plpgsql immutable;

CREATE FUNCTION ut_gte(user_type, user_type) RETURNS boolean
AS $$BEGIN return $1.v >= $2.v; END$$
LANGUAGE plpgsql immutable;

CREATE OPERATOR < (procedure = ut_lt,
                   leftarg = user_type, rightarg = user_type);

CREATE OPERATOR <= (procedure = ut_lte,
                    leftarg = user_type, rightarg = user_type);

CREATE OPERATOR = (procedure = ut_eq,
                   leftarg = user_type, rightarg = user_type);

CREATE OPERATOR > (procedure = ut_gt,
                   leftarg = user_type, rightarg = user_type);

CREATE OPERATOR >= (procedure = ut_gte,
                   leftarg = user_type, rightarg = user_type);

CREATE FUNCTION ut_cmp(user_type, user_type) RETURNS INT
AS $$BEGIN
    IF $1.v < $2.v THEN
        RETURN -1;
    ELSIF $1.v > $2.v THEN
        RETURN 1;
    END IF;
    RETURN 0;
END$$
LANGUAGE plpgsql immutable;

-- Create an operator class binding the above functions to user_type
CREATE OPERATOR CLASS user_type_op_class_1
DEFAULT FOR TYPE user_type USING LSM
AS
    OPERATOR 1 < ,
    OPERATOR 2 <= ,
    OPERATOR 3 = ,
    OPERATOR 4 >= ,
    OPERATOR 5 >,
    FUNCTION 1 ut_cmp(user_type, user_type);

-- Ensure that ORDER BY user data type works
SELECT * FROM user_table ORDER BY val;

DROP OPERATOR CLASS user_type_op_class_1 USING lsm;
