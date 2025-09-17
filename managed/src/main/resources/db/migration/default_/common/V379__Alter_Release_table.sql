-- Copyright (c) YugabyteDB, Inc.

alter table release add column if not exists sensitive_gflags text array;