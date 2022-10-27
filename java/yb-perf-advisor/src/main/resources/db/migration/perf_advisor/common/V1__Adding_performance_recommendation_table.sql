create table performance_recommendation (
                                            id                            uuid not null,
                                            universe_id                   uuid not null,
                                            recommendation_type           varchar(15) not null,
                                            observation                   varchar(255) not null,
                                            recommendation                varchar(255) not null,
                                            entity_type                   varchar(8) not null,
                                            entity_names                  varchar(255) not null,
                                            recommendation_info           jsonb not null,
                                            recommendation_state          varchar(8) not null,
                                            recommendation_priority       varchar(6) not null,
                                            recommendation_timestamp      timestamp not null,
                                            constraint ck_performance_recommendation_recommendation_type check ( recommendation_type in ('QUERY_LOAD_SKEW','UNUSED_INDEX','RANGE_SHARDING','CONNECTION_SKEW','CPU_SKEW','CPU_USAGE')),
                                            constraint ck_performance_recommendation_entity_type check ( entity_type in ('UNIVERSE','NODE','TABLE','DATABASE','INDEX')),
                                            constraint ck_performance_recommendation_recommendation_state check ( recommendation_state in ('OPEN','HIDDEN','RESOLVED')),
                                            constraint ck_performance_recommendation_recommendation_priority check ( recommendation_priority in ('HIGH','MEDIUM','LOW')),
                                            constraint pk_performance_recommendation primary key (id)
);
