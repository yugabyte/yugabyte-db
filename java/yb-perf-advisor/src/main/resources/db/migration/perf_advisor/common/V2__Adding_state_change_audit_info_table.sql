create table state_change_audit_info (
                                         id                            uuid not null,
                                         performance_recommendation_id uuid not null,
                                         field_name                    varchar(255) not null,
                                         previous_value                varchar(255) not null,
                                         updated_value                 varchar(255) not null,
                                         user_id                       uuid not null,
                                         timestamp                     timestamp not null,
                                         constraint pk_state_change_audit_info primary key (id)
);

create index ix_state_change_audit_info_performance_recommendation_id on state_change_audit_info (performance_recommendation_id);
alter table state_change_audit_info add constraint fk_state_change_audit_info_performance_recommendation_id foreign key (performance_recommendation_id) references performance_recommendation (id) on delete restrict on update restrict;
