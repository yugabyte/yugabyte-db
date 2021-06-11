create function migrate_values_to_binary() returns void as
$$
declare
  value_type text;
begin
  select data_type into value_type from information_schema.columns where table_name= 'runtime_config_entry' and column_name = 'value';
  if value_type = 'text' then
    alter table runtime_config_entry alter column value type bytea using convert_to(value::text, 'utf8');
  end if;
end;
$$ language plpgsql;

select migrate_values_to_binary();
drop function migrate_values_to_binary();
