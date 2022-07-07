-- Copyright (c) YugaByte, Inc.
alter table if exists support_bundle add column if not exists status varchar(32) not null;
alter table if exists support_bundle add constraint ck_support_bundle_status check (status in ('Running','Success','Failed'));
