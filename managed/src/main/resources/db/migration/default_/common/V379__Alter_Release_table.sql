-- Copyright (c) YugaByte, Inc.

alter table release add column if not exists sensitive_gflags text array;