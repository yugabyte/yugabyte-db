SET search_path = public;

CREATE OR REPLACE FUNCTION next_day(value date, weekday text) 
RETURNS date
AS '$libdir/orafunc'
LANGUAGE 'C' IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION last_day(value date) 
RETURNS date
AS '$libdir/orafunc'
LANGUAGE 'C' IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION months_between(date1 date, date2 date) 
RETURNS float8
AS '$libdir/orafunc'
LANGUAGE 'C' IMMUTABLE STRICT;

CREATE OR REPLACE FUNCTION add_months(day date, value int) 
RETURNS date
AS '$libdir/orafunc'
LANGUAGE 'C' IMMUTABLE STRICT;
