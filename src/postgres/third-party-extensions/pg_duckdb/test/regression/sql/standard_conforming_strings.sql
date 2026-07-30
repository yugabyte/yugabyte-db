CREATE TABLE foo(t text);
set standard_conforming_strings = off;
INSERT INTO foo VALUES('foo\'bar');
select * from foo where t = 'foo\'bar';

DROP TABLE foo;
