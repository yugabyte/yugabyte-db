--drop table if exists test;
--create table test(a int, b int,c int, d int, primary key(a,b));

insert into test values(1,2,3,4);

update test set c=c+1, d=d+1 where a=1;

update test set a=a+1 where a=1;

update test set a=a+1, b=b+1 where a=2;

begin;
update test set c=c+1, d=d+1 where a=3;
insert into test values(7,8,9,10);
update test set c=c+1, d=d+1 where a=7;
update test set a=a+1 where a=7;
update test set a=a+1, b=b+1 where a=8;
end;

begin;
insert into test values(2,3,4,5);
end;

begin;
insert into test values(5,6,7,8);
end;

begin;
insert into test values(6,7,8,9);
update test set d=d+9, c=c+9 where a=6;
update test set d=d+9, c=c+9 where a=6 and b=7;
update test set a=a+9 where a=6;
update test set b=b+9 where a=15 and b=7;
update test set a=a+9, b=b+9 where a=15;
update test set a=a+9, b=b+9 where a=24 and b=25;
commit;

update test set a=a+9, b=b+9 where a=33 and b=34;

begin;
insert into test values(60,70,80,90);
update test set c=c+9, d=d+9  where a=60;
update test set a=a+9, b=b+9 where a=60;
commit;
update test set b=b+1, c=c+1 where a=69;

begin;
update test set a=a+1, d=d+1 where a=69;
end;

begin;
insert into test values(11,12,13,14);
update test set d=d+1, c=c+1 where a=11;
end;

begin;
insert into test values(12,112,113,114);
delete from test where a=12 and b=112;
end;

begin;
insert into test values(13,113,114,115);
update test set c=c+1, d=d+1 where a=13;
update test set a=a+1, b=b+1 where a=13;
rollback;

begin;
insert into test values(17,114,115, 116);
update test set c=c+1 where a=17;
update test set a=a+1 where a=17;
update test set b=b+1, c=c+1 where a=18;
end;

begin;
update test set b=b+1, c=c+1 where a=18;
insert into test values(20,21,22,23);
update test set d=d+1, c=c+1 where a=20;
update test set a=a+1, b=b+1 where a=20;
abort;

begin;
update test set b=b+1, c=c+1 where a=21;
delete from test where a=21 and b=23;
insert into test values(21,23,24,25);
end;

insert into test values(41,43,44,45);

update test set b=b+1, c=c+1, d=d+1 where a=41;

delete from test where a=41 and b=44;

insert into test values(41,44,45,46);
