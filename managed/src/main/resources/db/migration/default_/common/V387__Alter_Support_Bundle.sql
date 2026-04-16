-- Copyright (c) YugaByte, Inc.

alter table if exists support_bundle drop constraint if exists ck_support_bundle_status;

alter table if exists support_bundle add constraint ck_support_bundle_status check (status in ('Running','Success','Failed','Aborted'));