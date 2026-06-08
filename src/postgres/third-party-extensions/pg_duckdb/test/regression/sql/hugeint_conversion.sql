create table hugeint_sum(a int);
insert into hugeint_sum select g from generate_series(1,100) g;
select pg_typeof(sum(a)) from hugeint_sum;
select sum(a) result from hugeint_sum;

drop table hugeint_sum;
