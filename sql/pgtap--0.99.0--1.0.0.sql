CREATE OR REPLACE FUNCTION _funkargs ( TEXT[] )
RETURNS TEXT AS $$
BEGIN
    RETURN array_to_string($1::regtype[], ',');
EXCEPTION WHEN undefined_object THEN
    RETURN array_to_string($1, ',');
END;
$$ LANGUAGE PLPGSQL STABLE;

CREATE OR REPLACE FUNCTION _got_func ( NAME, NAME, NAME[] )
RETURNS BOOLEAN AS $$
    SELECT EXISTS(
        SELECT TRUE
          FROM tap_funky
         WHERE schema = $1
           AND name   = $2
           AND args = _funkargs($3)
    );
$$ LANGUAGE SQL;

CREATE OR REPLACE FUNCTION _got_func ( NAME, NAME[] )
RETURNS BOOLEAN AS $$
    SELECT EXISTS(
        SELECT TRUE
          FROM tap_funky
         WHERE name = $1
           AND args = _funkargs($2)
           AND is_visible
    );
$$ LANGUAGE SQL;
