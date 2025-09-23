-- Copyright (c) YugaByte, Inc.

alter table if exists schedule drop constraint if exists ck_schedule_status;
alter table if exists schedule alter column status TYPE varchar(10);
alter table if exists schedule add constraint ck_schedule_status
        check (status in ('Creating',
                          'Editing',
                          'Deleting',
                          'Active',
                          'Paused',
                          'Stopped',
                          'Error'));