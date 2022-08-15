-- Copyright (c) YugaByte, Inc.

drop index if exists ix_universe_customer_id;
create index ix_universe_customer_id on universe (customer_id);
