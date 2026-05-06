CREATE TABLE recently_created_table(a VARCHAR);
INSERT INTO recently_created_table VALUES ('latest 1'), ('latest 2'), ('latest 3');

select 1 as result
FROM recently_created_table
WHERE  timestamp with time zone '2024-12-10 13:59:59.776896+00' < CAST((NOW() + INTERVAL '1 day') AS date)
LIMIT 1;

drop table recently_created_table;
