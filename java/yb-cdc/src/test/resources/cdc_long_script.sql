insert into test values (1,2,3);

update test set c=c+1 where a=1;

update test set a=a+1 where a=1;

begin;
insert into test values(7,8,9);
rollback;

begin;
insert into test values(7,8,9);
update test set b=b+9 where a=7;
end transaction;

begin;
insert into test values(6,7,8);
update test set c=c+9 where a=6;
update test set a=a+9 where a=6;
commit;

update test set b=b+1, c=c+1 where a=1;

begin;
update test set b=b+1, c=c+1 where a=1;
end;

begin;
insert into test values(11,12,13);
update test set b=b+1, c=c+1 where a=11;
end;

begin;
insert into test values(12,112,113);
delete from test where a=12;
end;

begin;
insert into test values(13,113,114);
update test set c=c+1 where a=13;
update test set a=a+1 where a=13;
end;

begin;
insert into test values(17,114,115);
update test set c=c+1 where a=17;
update test set a=a+1 where a=17;
update test set b=b+1, c=c+1 where a=18;
end;

begin;
update test set b=b+1, c=c+1 where a=18;
insert into test values(20,21,22);
update test set b=b+1, c=c+1 where a=20;
update test set a=a+1 where a=20;
end;

begin;
update test set b=b+1, c=c+1 where a=21;
delete from test where a=21;
insert into test values(21,23,24);
end;

begin;
insert into test values (-1,-2,-3), (-4,-5,-6);
insert into test values (-11, -12, -13);
delete from test where a=-1;
commit;

insert into test values (404, 405, 406), (104, 204, 304);

insert into test values(41,43,44);

update test set b=b+1, c=c+1 where a=41;

delete from test where a=41;

insert into test values(41,43,44);

update test set b=b+1, c=c+1 where a=41;
