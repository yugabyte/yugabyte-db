drop view oracle.user_constraints;

create view oracle.user_constraints as
    select conname as constraint_name,
           conindid::regclass as index_name,
           case contype when 'p' then 'P' when 'f' then 'R' end as constraint_type,
           conrelid::regclass as table_name,
           case contype when 'f' then (select conname
                                         from pg_constraint c2
                                        where contype = 'p' and c2.conindid = c1.conindid)
                                      end as r_constraint_name
      from pg_constraint c1, pg_class
     where conrelid = pg_class.oid;
