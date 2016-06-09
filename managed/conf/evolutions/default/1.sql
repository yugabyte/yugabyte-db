# --- Created by Ebean DDL
# To stop Ebean DDL generation, remove this comment and start using Evolutions

# --- !Ups

create table customer (
  id                            bigint auto_increment not null,
  email                         varchar(256) not null,
  password_hash                 varchar(256) not null,
  name                          varchar(256) not null,
  creation_date                 datetime(6) not null,
  auth_token                    varchar(255),
  auth_token_issue_date         datetime(6),
  constraint uq_customer_email unique (email),
  constraint pk_customer primary key (id)
);


# --- !Downs

drop table if exists customer;

