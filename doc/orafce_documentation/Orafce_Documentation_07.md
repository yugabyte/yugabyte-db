Chapter 7 Transaction behavior
---

Most of the transaction behavior are exactly same, however the below stuff is not.

### 7.1 Handled Statement Failure.

```
create table t (a int primary key, b int);
begin;
insert into t values(1,1);
insert into t values(1, 1);
commit; 
```

Oracle : commit can succeed. t has 1 row after that.

PostgreSQL: commit failed due to the 2nd insert failed. so t has 0 row.


### 7.2 DML with Subquery

Case 1:

```
create table dml(a int, b int);
insert into dml values(1, 1), (2,2);

-- session 1: 
begin; 
delete from dml where a in (select min(a) from dml); 

--session 2:  
delete from dml where a in (select min(a) from dml); 

-- session 1:  
commit;
```

In Oracle:  1 row deleted in sess 2. so 0 rows in the dml at last.

In PG    :  0 rows are deleted in sess 2, so 1 rows in the dml at last.

Oracle probably detects the min(a) is changed and rollback/rerun the statement.

The same reason can cause the below difference as well.

```
create table su (a int, b int);
insert into su values(1, 1);

- session 1:
begin;
update su set b = 2 where b = 1;

- sess 2:
select * from su where a in (select a from su where b = 1) for update;

- sess 1:
commit;
```

In oracle, 0 row is selected. In PostgreSQL, 1 row (1, 2) is selected.


A best practice would be never use subquery in DML & SLEECT ... FOR UPDATE. Even in Oracle, the behavior is inconsistent as well. Oracle between 11.2.0.1 and 11.2.0.3 probably behavior same as Postgres, but other versions not.
