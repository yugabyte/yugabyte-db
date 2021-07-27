-- Copyright (c) Yugabyte, Inc.
alter table if exists provider drop constraint if exists uq_customer_provider_code;
