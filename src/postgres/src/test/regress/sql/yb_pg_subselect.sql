--
-- SUBSELECT
--

-- another variant of that (bug #16213)
explain (verbose, costs off)
select * from
(values
  (3 not in (select * from (values (1), (2)) ss1)),
  (false)
) ss;

select * from
(values
  (3 not in (select * from (values (1), (2)) ss1)),
  (false)
) ss;
