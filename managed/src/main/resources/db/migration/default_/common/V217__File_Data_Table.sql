-- Copyright (c) YugaByte, Inc.

create table file_data (
  file_path                     varchar(1000) not null,
  extension                     varchar(255),
  parent_uuid                   uuid not null,
  file_content                  text not null,
  timestamp                     timestamp not null,
  constraint pk_file_data primary key (file_path, timestamp)
);

create index ix_file_data_parent_uuid on file_data (parent_uuid);
