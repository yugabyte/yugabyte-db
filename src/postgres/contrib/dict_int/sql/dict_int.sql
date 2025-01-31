CREATE EXTENSION dict_int;

--lexize
select ts_lexize('intdict', '511673');
select ts_lexize('intdict', '129');
select ts_lexize('intdict', '40865854');
select ts_lexize('intdict', '952');
select ts_lexize('intdict', '654980341');
select ts_lexize('intdict', '09810106');
select ts_lexize('intdict', '14262713');
select ts_lexize('intdict', '6532082986');
select ts_lexize('intdict', '0150061');
select ts_lexize('intdict', '7778');
select ts_lexize('intdict', '9547');
select ts_lexize('intdict', '753395478');
select ts_lexize('intdict', '647652');
select ts_lexize('intdict', '6988655574');
select ts_lexize('intdict', '1279');
select ts_lexize('intdict', '1266645909');
select ts_lexize('intdict', '7594193969');
select ts_lexize('intdict', '16928207');
select ts_lexize('intdict', '196850350328');
select ts_lexize('intdict', '22026985592');
select ts_lexize('intdict', '2063765');
select ts_lexize('intdict', '242387310');
select ts_lexize('intdict', '93595');
select ts_lexize('intdict', '9374');
select ts_lexize('intdict', '996969');
select ts_lexize('intdict', '353595982');
select ts_lexize('intdict', '925860');
select ts_lexize('intdict', '11848378337');
select ts_lexize('intdict', '333');
select ts_lexize('intdict', '799287416765');
select ts_lexize('intdict', '745939');
select ts_lexize('intdict', '67601305734');
select ts_lexize('intdict', '3361113');
select ts_lexize('intdict', '9033778607');
select ts_lexize('intdict', '7507648');
select ts_lexize('intdict', '1166');
select ts_lexize('intdict', '9360498');
select ts_lexize('intdict', '917795');
select ts_lexize('intdict', '9387894');
select ts_lexize('intdict', '42764329');
select ts_lexize('intdict', '564062');
select ts_lexize('intdict', '5413377');
select ts_lexize('intdict', '060965');
select ts_lexize('intdict', '08273593');
select ts_lexize('intdict', '593556010144');
select ts_lexize('intdict', '17988843352');
select ts_lexize('intdict', '252281774');
select ts_lexize('intdict', '313425');
select ts_lexize('intdict', '641439323669');
select ts_lexize('intdict', '314532610153');

ALTER TEXT SEARCH DICTIONARY intdict (MAXLEN = -214783648);  -- fail
-- This ought to fail, perhaps, but historically it has not:
ALTER TEXT SEARCH DICTIONARY intdict (MAXLEN = 6.7);

select ts_lexize('intdict', '-40865854');
select ts_lexize('intdict', '+40865854');
ALTER TEXT SEARCH DICTIONARY intdict (ABSVAL = true);
select ts_lexize('intdict', '-40865854');
select ts_lexize('intdict', '+40865854');
ALTER TEXT SEARCH DICTIONARY intdict (REJECTLONG = 1);
select ts_lexize('intdict', '-40865854');
select ts_lexize('intdict', '-4086585');
select ts_lexize('intdict', '-408658');

SELECT dictinitoption FROM pg_ts_dict WHERE dictname = 'intdict';
