
CREATE TABLE public.alembic_version (
    version_num character varying(32) NOT NULL,
 CONSTRAINT alembic_version_pkc PRIMARY KEY (version_num)
);


CREATE TABLE public.chart (
    id integer NOT NULL,
    label character varying(200),
    conn_id character varying(250) NOT NULL,
    user_id integer,
    chart_type character varying(100),
    sql_layout character varying(50),
    sql text,
    y_log_scale boolean,
    show_datatable boolean,
    show_sql boolean,
    height integer,
    default_params character varying(5000),
    x_is_date boolean,
    iteration_no integer,
    last_modified timestamp with time zone,
CONSTRAINT chart_pkey PRIMARY KEY (id)

);


CREATE SEQUENCE public.chart_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.chart_id_seq OWNED BY public.chart.id;


CREATE TABLE public.connection (
    id integer NOT NULL,
    conn_id character varying(250),
    conn_type character varying(500),
    host character varying(500),
    schema character varying(500),
    login character varying(500),
    password character varying(5000),
    port integer,
    extra character varying(5000),
    is_encrypted boolean,
    is_extra_encrypted boolean,
CONSTRAINT connection_pkey PRIMARY KEY (id)
);


CREATE SEQUENCE public.connection_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;

ALTER SEQUENCE public.connection_id_seq OWNED BY public.connection.id;


CREATE TABLE public.dag (
    dag_id character varying(250) NOT NULL,
    is_paused boolean,
    is_subdag boolean,
    is_active boolean,
    last_scheduler_run timestamp with time zone,
    last_pickled timestamp with time zone,
    last_expired timestamp with time zone,
    scheduler_lock boolean,
    pickle_id integer,
    fileloc character varying(2000),
    owners character varying(2000),
    description text,
    default_view character varying(25),
    schedule_interval text,
    root_dag_id character varying(250),
CONSTRAINT dag_pkey PRIMARY KEY (dag_id)
);



CREATE TABLE public.dag_pickle (
    id integer NOT NULL,
    pickle bytea,
    created_dttm timestamp with time zone,
    pickle_hash bigint,
CONSTRAINT dag_pickle_pkey PRIMARY KEY (id)
);


CREATE SEQUENCE public.dag_pickle_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;

ALTER SEQUENCE public.dag_pickle_id_seq OWNED BY public.dag_pickle.id;


CREATE TABLE public.dag_run (
    id integer NOT NULL,
    dag_id character varying(250),
    execution_date timestamp with time zone,
    state character varying(50),
    run_id character varying(250),
    external_trigger boolean,
    conf bytea,
    end_date timestamp with time zone,
    start_date timestamp with time zone,
CONSTRAINT dag_run_dag_id_execution_date_key UNIQUE (dag_id, execution_date),
CONSTRAINT dag_run_dag_id_run_id_key UNIQUE (dag_id, run_id),
CONSTRAINT dag_run_pkey PRIMARY KEY (id)
);

CREATE SEQUENCE public.dag_run_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;

ALTER SEQUENCE public.dag_run_id_seq OWNED BY public.dag_run.id;

CREATE TABLE public.dag_tag (
    name character varying(100) NOT NULL,
    dag_id character varying(250) NOT NULL,
CONSTRAINT dag_tag_pkey PRIMARY KEY (name, dag_id)
);

CREATE TABLE public.import_error (
    id integer NOT NULL,
    "timestamp" timestamp with time zone,
    filename character varying(1024),
    stacktrace text,
CONSTRAINT import_error_pkey PRIMARY KEY (id)
);

CREATE SEQUENCE public.import_error_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;

ALTER SEQUENCE public.import_error_id_seq OWNED BY public.import_error.id;

CREATE TABLE public.job (
    id integer NOT NULL,
    dag_id character varying(250),
    state character varying(20),
    job_type character varying(30),
    start_date timestamp with time zone,
    end_date timestamp with time zone,
    latest_heartbeat timestamp with time zone,
    executor_class character varying(500),
    hostname character varying(500),
    unixname character varying(1000),
CONSTRAINT job_pkey PRIMARY KEY (id)
);

CREATE SEQUENCE public.job_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;

ALTER SEQUENCE public.job_id_seq OWNED BY public.job.id;

CREATE TABLE public.known_event (
    id integer NOT NULL,
    label character varying(200),
    start_date timestamp without time zone,
    end_date timestamp without time zone,
    user_id integer,
    known_event_type_id integer,
    description text,
CONSTRAINT known_event_pkey PRIMARY KEY (id)
);

CREATE SEQUENCE public.known_event_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;

ALTER SEQUENCE public.known_event_id_seq OWNED BY public.known_event.id;


CREATE TABLE public.known_event_type (
    id integer NOT NULL,
    know_event_type character varying(200),
CONSTRAINT known_event_type_pkey PRIMARY KEY (id)
);

CREATE SEQUENCE public.known_event_type_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;

ALTER SEQUENCE public.known_event_type_id_seq OWNED BY public.known_event_type.id;

CREATE TABLE public.kube_resource_version (
    one_row_id boolean DEFAULT true NOT NULL,
    resource_version character varying(255),
    CONSTRAINT kube_resource_version_one_row_id CHECK (one_row_id),
CONSTRAINT kube_resource_version_pkey PRIMARY KEY (one_row_id)
);


CREATE TABLE public.kube_worker_uuid (
    one_row_id boolean DEFAULT true NOT NULL,
    worker_uuid character varying(255),
    CONSTRAINT kube_worker_one_row_id CHECK (one_row_id),
CONSTRAINT kube_worker_uuid_pkey PRIMARY KEY (one_row_id)
);

CREATE TABLE public.log (
    id integer NOT NULL,
    dttm timestamp with time zone,
    dag_id character varying(250),
    task_id character varying(250),
    event character varying(30),
    execution_date timestamp with time zone,
    owner character varying(500),
    extra text,
CONSTRAINT log_pkey PRIMARY KEY (id)
);


CREATE SEQUENCE public.log_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;

ALTER SEQUENCE public.log_id_seq OWNED BY public.log.id;

CREATE TABLE public.serialized_dag (
    dag_id character varying(250) NOT NULL,
    fileloc character varying(2000) NOT NULL,
    fileloc_hash integer NOT NULL,
    data json NOT NULL,
    last_updated timestamp with time zone NOT NULL,
CONSTRAINT serialized_dag_pkey PRIMARY KEY (dag_id)
);

CREATE TABLE public.sla_miss (
    task_id character varying(250) NOT NULL,
    dag_id character varying(250) NOT NULL,
    execution_date timestamp with time zone NOT NULL,
    email_sent boolean,
    "timestamp" timestamp with time zone,
    description text,
    notification_sent boolean,
CONSTRAINT sla_miss_pkey PRIMARY KEY (task_id, dag_id, execution_date)
);


CREATE TABLE public.slot_pool (
    id integer NOT NULL,
    pool character varying(50),
    slots integer,
    description text,
CONSTRAINT slot_pool_pkey PRIMARY KEY (id),
CONSTRAINT slot_pool_pool_key UNIQUE (pool)
);

CREATE SEQUENCE public.slot_pool_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;

ALTER SEQUENCE public.slot_pool_id_seq OWNED BY public.slot_pool.id;

CREATE TABLE public.task_fail (
    id integer NOT NULL,
    task_id character varying(250) NOT NULL,
    dag_id character varying(250) NOT NULL,
    execution_date timestamp with time zone NOT NULL,
    start_date timestamp with time zone,
    end_date timestamp with time zone,
    duration integer,
CONSTRAINT task_fail_pkey PRIMARY KEY (id)
);

CREATE SEQUENCE public.task_fail_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;

ALTER SEQUENCE public.task_fail_id_seq OWNED BY public.task_fail.id;


CREATE TABLE public.task_instance (
    task_id character varying(250) NOT NULL,
    dag_id character varying(250) NOT NULL,
    execution_date timestamp with time zone NOT NULL,
    start_date timestamp with time zone,
    end_date timestamp with time zone,
    duration double precision,
    state character varying(20),
    try_number integer,
    hostname character varying(1000),
    unixname character varying(1000),
    job_id integer,
    pool character varying(50) NOT NULL,
    queue character varying(256),
    priority_weight integer,
    operator character varying(1000),
    queued_dttm timestamp with time zone,
    pid integer,
    max_tries integer DEFAULT '-1'::integer,
    executor_config bytea,
CONSTRAINT task_instance_pkey PRIMARY KEY (task_id, dag_id, execution_date)
);

CREATE TABLE public.task_reschedule (
    id integer NOT NULL,
    task_id character varying(250) NOT NULL,
    dag_id character varying(250) NOT NULL,
    execution_date timestamp with time zone NOT NULL,
    try_number integer NOT NULL,
    start_date timestamp with time zone NOT NULL,
    end_date timestamp with time zone NOT NULL,
    duration integer NOT NULL,
    reschedule_date timestamp with time zone NOT NULL,
CONSTRAINT task_reschedule_pkey PRIMARY KEY (id)
);

CREATE SEQUENCE public.task_reschedule_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;

ALTER SEQUENCE public.task_reschedule_id_seq OWNED BY public.task_reschedule.id;

CREATE TABLE public.users (
    id integer NOT NULL,
    username character varying(250),
    email character varying(500),
    password character varying(255),
    superuser boolean,
CONSTRAINT user_pkey PRIMARY KEY (id),
CONSTRAINT user_username_key UNIQUE (username)
);

CREATE SEQUENCE public.user_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;

ALTER SEQUENCE public.user_id_seq OWNED BY public.users.id;

CREATE TABLE public.variable (
    id integer NOT NULL,
    key character varying(250),
    val text,
    is_encrypted boolean,
CONSTRAINT variable_key_key UNIQUE (key),
 CONSTRAINT variable_pkey PRIMARY KEY (id)
);

CREATE SEQUENCE public.variable_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;

ALTER SEQUENCE public.variable_id_seq OWNED BY public.variable.id;


CREATE TABLE public.xcom (
    id integer NOT NULL,
    key character varying(512),
    value bytea,
    "timestamp" timestamp with time zone NOT NULL,
    execution_date timestamp with time zone NOT NULL,
    task_id character varying(250) NOT NULL,
    dag_id character varying(250) NOT NULL,
CONSTRAINT xcom_pkey PRIMARY KEY (id)
);


CREATE SEQUENCE public.xcom_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;

ALTER SEQUENCE public.xcom_id_seq OWNED BY public.xcom.id;


ALTER TABLE ONLY public.chart ALTER COLUMN id SET DEFAULT nextval('public.chart_id_seq'::regclass);


ALTER TABLE ONLY public.connection ALTER COLUMN id SET DEFAULT nextval('public.connection_id_seq'::regclass);

ALTER TABLE ONLY public.dag_pickle ALTER COLUMN id SET DEFAULT nextval('public.dag_pickle_id_seq'::regclass);

ALTER TABLE ONLY public.dag_run ALTER COLUMN id SET DEFAULT nextval('public.dag_run_id_seq'::regclass);

ALTER TABLE ONLY public.import_error ALTER COLUMN id SET DEFAULT nextval('public.import_error_id_seq'::regclass);


ALTER TABLE ONLY public.job ALTER COLUMN id SET DEFAULT nextval('public.job_id_seq'::regclass);


ALTER TABLE ONLY public.known_event ALTER COLUMN id SET DEFAULT nextval('public.known_event_id_seq'::regclass);


ALTER TABLE ONLY public.known_event_type ALTER COLUMN id SET DEFAULT nextval('public.known_event_type_id_seq'::regclass);


ALTER TABLE ONLY public.log ALTER COLUMN id SET DEFAULT nextval('public.log_id_seq'::regclass);


ALTER TABLE ONLY public.slot_pool ALTER COLUMN id SET DEFAULT nextval('public.slot_pool_id_seq'::regclass);


ALTER TABLE ONLY public.task_fail ALTER COLUMN id SET DEFAULT nextval('public.task_fail_id_seq'::regclass);


ALTER TABLE ONLY public.task_reschedule ALTER COLUMN id SET DEFAULT nextval('public.task_reschedule_id_seq'::regclass);

ALTER TABLE ONLY public.users ALTER COLUMN id SET DEFAULT nextval('public.user_id_seq'::regclass);

ALTER TABLE ONLY public.variable ALTER COLUMN id SET DEFAULT nextval('public.variable_id_seq'::regclass);

ALTER TABLE ONLY public.xcom ALTER COLUMN id SET DEFAULT nextval('public.xcom_id_seq'::regclass);


INSERT INTO public.alembic_version VALUES ('7939bcff74ba');


INSERT INTO public.chart VALUES (1, 'Airflow task instance by type', 'airflow_db', NULL, 'bar', 'series', 'SELECT state, COUNT(1) as number FROM task_instance WHERE dag_id LIKE ''example%'' GROUP BY state', NULL, NULL, true, 600, '{}', false, 0, '2020-04-08 01:54:28.809085+00');


INSERT INTO public.connection VALUES (1, 'airflow_db', 'mysql', 'mysql', 'airflow', 'root', NULL, NULL, NULL, false, false);
INSERT INTO public.connection VALUES (2, 'beeline_default', 'beeline', 'localhost', 'default', NULL, NULL, 10000, 'gAAAAABejS7UyLciR9-Q5DdjtftONqR5Kndx-VnyP_rTR2tKMAgzXCZB8AUsxlqGA-jfwjwT-R_lcFHTym_FWp-9BocSPjduqQycqU2jx0pIWoSKNt14NVAnj-bQyWmX5RlfRiCy1jCG', false, true);
INSERT INTO public.connection VALUES (3, 'bigquery_default', 'google_cloud_platform', NULL, 'default', NULL, NULL, NULL, NULL, false, false);
INSERT INTO public.connection VALUES (4, 'local_mysql', 'mysql', 'localhost', 'airflow', 'airflow', 'gAAAAABejS7U2Q2pm18vk01uvL0IQ-Pwch9ATNb3c_bMJ6OXjLYabAg-X1rT7E16PATTO7Y0X63YjzBDopr3m4vuU1x_Cu2emw==', NULL, NULL, true, false);
INSERT INTO public.connection VALUES (5, 'presto_default', 'presto', 'localhost', 'hive', NULL, NULL, 3400, NULL, false, false);
INSERT INTO public.connection VALUES (6, 'google_cloud_default', 'google_cloud_platform', NULL, 'default', NULL, NULL, NULL, NULL, false, false);
INSERT INTO public.connection VALUES (7, 'hive_cli_default', 'hive_cli', NULL, 'default', NULL, NULL, NULL, NULL, false, false);
INSERT INTO public.connection VALUES (8, 'pig_cli_default', 'pig_cli', NULL, 'default', NULL, NULL, NULL, NULL, false, false);
INSERT INTO public.connection VALUES (9, 'hiveserver2_default', 'hiveserver2', 'localhost', 'default', NULL, NULL, 10000, NULL, false, false);
INSERT INTO public.connection VALUES (10, 'metastore_default', 'hive_metastore', 'localhost', NULL, NULL, NULL, 9083, 'gAAAAABejS7UjRPghXAsrX4sy0-zb7p8gYqIfygwmiw8NTj9J-qstwe6Qk7gxf9O_KePSLVDVBHWVxIezVhWszWjuId_9fchvz6B1PdbYuP4YSkVhvIs_yw=', false, true);
INSERT INTO public.connection VALUES (11, 'mongo_default', 'mongo', 'mongo', NULL, NULL, NULL, 27017, NULL, false, false);
INSERT INTO public.connection VALUES (12, 'mysql_default', 'mysql', 'mysql', 'airflow', 'root', NULL, NULL, NULL, false, false);
INSERT INTO public.connection VALUES (13, 'postgres_default', 'postgres', 'postgres', 'airflow', 'postgres', 'gAAAAABejS7U3jASHuTo4cc0OH9ydaMNXA5HMdhEOJS94rPiaW43CFdebPOVQLr_mK9KVWpAy4OvKRAFS-iSjDow1tewdAEesw==', NULL, NULL, true, false);
INSERT INTO public.connection VALUES (14, 'sqlite_default', 'sqlite', '/tmp/sqlite_default.db', NULL, NULL, NULL, NULL, NULL, false, false);
INSERT INTO public.connection VALUES (15, 'http_default', 'http', 'https://www.httpbin.org/', NULL, NULL, NULL, NULL, NULL, false, false);
INSERT INTO public.connection VALUES (16, 'mssql_default', 'mssql', 'localhost', NULL, NULL, NULL, 1433, NULL, false, false);
INSERT INTO public.connection VALUES (17, 'vertica_default', 'vertica', 'localhost', NULL, NULL, NULL, 5433, NULL, false, false);
INSERT INTO public.connection VALUES (18, 'wasb_default', 'wasb', NULL, NULL, NULL, NULL, NULL, 'gAAAAABejS7UWX8w9fme3Xt0rOFXPWE8K_S2uiX1yV4Wt0jMu76KgGhHJFGcwkGE8mbA88Rv3v1Rm22gYuCYPMcVr145cO4FM20FTCwz1n2GsN8n686j2yg=', false, true);
INSERT INTO public.connection VALUES (19, 'webhdfs_default', 'hdfs', 'localhost', NULL, NULL, NULL, 50070, NULL, false, false);
INSERT INTO public.connection VALUES (20, 'ssh_default', 'ssh', 'localhost', NULL, NULL, NULL, NULL, NULL, false, false);
INSERT INTO public.connection VALUES (21, 'sftp_default', 'sftp', 'localhost', NULL, 'airflow', NULL, 22, 'gAAAAABejS7UQdA8g-0wmu7BHaZhhqmu72cGWhUPrrgxTL3sZqPW9PB3eT2dtLpinW5CuLPNBr0oTPhqM8rp15efSOu6BvNXsgV27vJXPx3boxLkFr8SUmBqyr9In4qPLTxheQ-_cG_zrSI4FQ3rvedkCbAEHVR-aeviu4i4wDclrNQbZp7MqgaInkHALwoNKvVKzYMOaQLs', false, true);
INSERT INTO public.connection VALUES (22, 'fs_default', 'fs', NULL, NULL, NULL, NULL, NULL, 'gAAAAABejS7Udf6HjqvKzaF7UyLFlfT3JZ0JvYFSS-RH-MYHjl2aLKXXN_JVJ1ggh6G6VXn_jF20hJpj8bb2Z2GjFsCWbAJOXA==', false, true);
INSERT INTO public.connection VALUES (23, 'aws_default', 'aws', NULL, NULL, NULL, NULL, NULL, NULL, false, false);
INSERT INTO public.connection VALUES (24, 'spark_default', 'spark', 'yarn', NULL, NULL, NULL, NULL, 'gAAAAABejS7UQgT9gtXig7tzWdsEgZq5p9fbqiyePzBoRxErkNhI_zhSISPyKueuvLh0uYpMWAk5ZG3F1YR1L_RliPMoP-3kOjsOQrjL7zsitdcAMvoOQpM=', false, true);
INSERT INTO public.connection VALUES (25, 'druid_broker_default', 'druid', 'druid-broker', NULL, NULL, NULL, 8082, 'gAAAAABejS7UNbrJolenUROBTe2oH1NZPwOWrZm8Zht6-PfO21h47VaQd81h_mHkXCx0lNnZ7w4lqJmkyl13NQhFtfn2c4Uc2HMBj1YEpPHuw1wRlxJPJ-w=', false, true);
INSERT INTO public.connection VALUES (26, 'druid_ingest_default', 'druid', 'druid-overlord', NULL, NULL, NULL, 8081, 'gAAAAABejS7UBx8-E7nMsVNCLvMgJWJERrke84EhS4qrDH0gVR2c1iwZJsTr2bJf2DM_YvbzH46N8pyHhQ_SMd2-XUW_oEfkaTeOrumi-0oXFiJ96walY0XY9t8Mp_Opu_eYrJzZ8fQ8', false, true);
INSERT INTO public.connection VALUES (27, 'redis_default', 'redis', 'redis', NULL, NULL, NULL, 6379, 'gAAAAABejS7UemkX7Kscp8I1pkh84v8H-Wd4wfK0smxWtpguB5IcqZEZMNatq5LqmRL0oZn2Eac_m0rI2ai-NBrhs2BHldTnEA==', false, true);
INSERT INTO public.connection VALUES (28, 'sqoop_default', 'sqoop', 'rmdbs', NULL, NULL, NULL, NULL, '', false, false);
INSERT INTO public.connection VALUES (29, 'emr_default', 'emr', NULL, NULL, NULL, NULL, NULL, 'gAAAAABejS7Uh5Vx79_0T25yEVUv2vu1fclhTPokjMNjG4H9iZf_gbGDR5uAosMqYDZDkNnd-ND2Rf4FoOKLPTYJN1cBx_M3Y0VHYR2688ireYIKNauhgj37TOwcasUQ48AvBbai_KsulG7xkRn9yoHKpSNVvBSWw4q0tbvGl53gy21DH_KpA4nTtLaL_d83PTYp-bZltXD3_K_RC9f-c86EaKNgCIqJiTBrDFTZ-UuskGkLhZkr_rgkVx6QieDcYcQf_dJGFBqusnozf29AxK7i4fq1DHrQetgBvphy_TD1JPitpcx93xkwVYF0aZ05cKCzjnw9xmcJ7859iWHucTbXbNFEJPB9q8esdh197zXya-UDPX3FqtmGfecKwUJh-eIb7d62ubEqc2f1An38k3N3_WhSL3ujTsj-BuASRAnmds6KwJtXKyBev6bWukFyiRQnj9LaW8hlcMkYaVfEFiHJFHZh7QwTl5N3HdaskFe9-18uIx05GRq5E-f9T4H5yEw_QE8Ib9GNDBx9j17IVTQMUCQdPqI0c4Gt1aqbLtQ46-TIsKqBg3jehYNeH9YMMmukJNBjp6JKEeeky-TetZmSXgnHolswN7x8NaXGmDwWpJwmivx0omm1UkreghEEj84-aIxIiQEgxWu_kn3B4aAYoMOx0h0uxOiIC935yw7exMSZDu-t-u0aUbLMjXc3iSc4lI41t1bwrnBy5tgk37bg-nxhw647UDluUktci0TF7uxl87KWn68a-KHOM_1aDqh6cYtjcEZcYHY5M9J8tiX3RXGNaNhEH9GIEpWq6oY_nej6BBaOFBlNjK1kCI3yd7i33jQRpVs6VGCSywK4DqF95YPzwCws48Cics5JcNoPyIFr2drUGaF1nw5iu4ofFklIBUCCVzkssJFredZSQ3GKCKxpwPay5aSQ4qXzYBsyfMx7X8VNVLoS7YN8YCdw8PXlxH9eMg82VYa58k8BMUNd7FadabAfylm_1k1sDIllYqmvisy6wSl-PkGmO9amnI1kmCZaSOpWgCJXgXtt45wzAOb_KKiG01GMEH-mjNu1AOAZ4VEA7ZseJbIvVasektUWZHaF_IIKHanwtVZJ2Az1_gpLg4DPhLgdSSbm8eV2vP8cGMm2bD4LIDmeR_7vvtSZ1PxhjFj1WHGIJYPHRSIRhDSKzNafpRYtDxpujhIEe1MET9viJYGXmiH04ONKuscm2m6OLzDAjulA3ucJGNkkV5I70Pet4KhD-R9RDg1ZdbbeMWlx3azAbF6_MdYMstccDHw795c-3MJltLqjOe0zriufOwKFnqa2OZoGLNwsOaImHIgHNz9U1ntGf9EWEcAQwIZfL6FY0RWUoklKsiPxIi475u3CxfNGWrqNse1HuMQAEXKRLBOp7SXxDy5Y-pIFJ86YlE8FtquZlpf1rqiW8CaSKL_9B00ONyF_k6DSohSnuhqdA9K7I1g-sBUEkzHrDRlO70JxUxF3s4NTeaiDgZfOgYBQrm6LS8IBx4lKu0CTGhMB85fUmaAxUCy5Rf5jNEgfW3_j9CFEnOaZiNOq-yZxuaBM8F42UXfMIOz2wvG9JB2IE8seTwhOsYxJX_hjfOuxo-XyO2HaMLNbQrgk05AGHtuZLha8oYjmJCjYivnTH407va6ZPT2oxTXTGDPsERPeOqSrTcdzu2pBHjKTNyANJZ9nDypa70cPaiL4xL3EdpXLvGpXoeh1z8O4RUQXmWeIOHOzEOv93LufKDjQTslywPeOlCrkFqqUb2lg7f7TEX9RtCMtcykXA0yxKthP9mRt_U_wIwNmR_LQLa0nAF4qZAftKChZ0z358dC99fyWkr0mIfRBzu3Z6UNfyIqkJw_yi3gAwcX_1C32WQuQg75y1sJkPPRdFN58turFfu_55hF-YKVB8L8eoyWON_79m5p8FdUAuz_f20UqA_oFMDCPvIAghlcnxPLuZAOKtqzmMDfX5zeXf8E27gShVywg6UCD27gn_2x03JuqxK5icvqBI3c2JqA67ltXL-cjGp5UrBhKdBOMlZ651H6YflmPqZzE_IYHPROoZntVclnbPddPytohEmC6UPmGWeXa0fLGCyYCW2589jkuRM5GB8V_roEroaJ3rnY9CGofayVya7RG6-NdOFrvYQof7EyGoYANRyusDQUAIt0rwnGyTPnzT-yWVJhJH1KjyS2-B3y6TgVZ0iRJJ1vXnnUBlA8G7JpO-iM0qRPMoU92B0lP0LtdyZyrmS2NIKJiYUlaxq8meVO9Hi9jurKX8Opf8yU4y_jjoOP7YYa0L9SHKX_3NDXLcuU4tFtqDlVx9PzXdBBvpG8VGMVlaelw-YXVlqYb3CqJ_FmFD3soWXJraA39SVNswf9iarkZyde21dA4l0PaOFN4qHGn3F47F7iyNxZQK50FYvVZBHyOnmnIFrSPhKiykDTdo8ft5htmY_E_cP4vSZ0am6oIXwF2CR5ToExmMHp0fhdufMw3AABduRNDnZRriWgUB1oThYKbHptyoJpTvSUTVGmZXdttahvXooMRvKv6eeGKTh4=', false, true);
INSERT INTO public.connection VALUES (30, 'databricks_default', 'databricks', 'localhost', NULL, NULL, NULL, NULL, NULL, false, false);
INSERT INTO public.connection VALUES (31, 'qubole_default', 'qubole', 'localhost', NULL, NULL, NULL, NULL, NULL, false, false);
INSERT INTO public.connection VALUES (32, 'segment_default', 'segment', NULL, NULL, NULL, NULL, NULL, 'gAAAAABejS7Ur3Dc0uGaWgYfTwmdOzVdKLusVbFX33EqjiCzKxeBf38lkwJMZfbjl-qaoHOSlpyc4RkOd2b5cgvVB-ofqrSzc9T4bucTB1xZBZwpqCkdqtnmEbIiwIhJv6j1bq0G6oc0', false, true);
INSERT INTO public.connection VALUES (33, 'azure_data_lake_default', 'azure_data_lake', NULL, NULL, NULL, NULL, NULL, 'gAAAAABejS7U_JlivKOsqjBFXI31YnZgV6q9GBhpquSwPFA1HKaB2DVA07g7LQ7liCy1CLki6pEJPB43xzwxVZTTP-Q9Eq-6C3fsOUfiBU4TFRdCDEAg8MD0eWBB393J8SiA7kcXHo_gRWzDhS9riRMBRLb0cdK6iw==', false, true);
INSERT INTO public.connection VALUES (34, 'azure_cosmos_default', 'azure_cosmos', NULL, NULL, NULL, NULL, NULL, 'gAAAAABejS7Uj_oPYC4odcPzvrh4KITyPvDxER1ARr3BRxuU-w0l4iNFxhQFRiNgHWbYr6LKgA3FCm5KUVqTMnNW9xwJDheNRjVJe9utXJfBhphc3988rFG8q0YGXxHqfg1Ttx6WFDIdEZ5LS-lFhvXiNPnT9Uv--nOP6v517aCbTV2J01wjq5g=', false, true);
INSERT INTO public.connection VALUES (35, 'azure_container_instances_default', 'azure_container_instances', NULL, NULL, NULL, NULL, NULL, 'gAAAAABejS7UE6qosX4kARc24Rhj0yK722mTHuqWZDtMqpfL7AHjc2hgTbwd9cCJpilTAVbeLY-GttyyXZ96r7bhmR_n_OHqXyhnsNw5q4HHvL48raCFJNI-Val087_1LwkRoecHuqetFjopU-WW7ukBZBUloiwt0BExp9w_T6pTYsrNpLyBePU=', false, true);
INSERT INTO public.connection VALUES (36, 'cassandra_default', 'cassandra', 'cassandra', NULL, NULL, NULL, 9042, NULL, false, false);
INSERT INTO public.connection VALUES (37, 'dingding_default', 'http', '', NULL, NULL, NULL, NULL, NULL, false, false);
INSERT INTO public.connection VALUES (38, 'opsgenie_default', 'http', '', NULL, NULL, NULL, NULL, NULL, false, false);


INSERT INTO public.job VALUES (1, NULL, 'running', 'SchedulerJob', '2020-04-08 01:54:51.176395+00', NULL, '2020-04-08 15:03:11.186832+00', 'NoneType', 'airflow.c.jimmy-experimental.internal', 'jimmy');


INSERT INTO public.known_event_type VALUES (1, 'Holiday');
INSERT INTO public.known_event_type VALUES (2, 'Outage');
INSERT INTO public.known_event_type VALUES (3, 'Natural Disaster');
INSERT INTO public.known_event_type VALUES (4, 'Marketing Campaign');


INSERT INTO public.kube_resource_version VALUES (true, '');


INSERT INTO public.kube_worker_uuid VALUES (true, '');


INSERT INTO public.log VALUES (1, '2020-04-08 01:54:51.137451+00', NULL, NULL, 'cli_scheduler', NULL, 'jimmy', '{"host_name": "airflow", "full_command": "[''/usr/local/bin/airflow'', ''scheduler'', ''-D'']"}');
INSERT INTO public.log VALUES (2, '2020-04-08 01:54:59.503794+00', NULL, NULL, 'cli_webserver', NULL, 'jimmy', '{"host_name": "airflow", "full_command": "[''/usr/local/bin/airflow'', ''webserver'', ''-p'', ''8080'', ''-D'']"}');






INSERT INTO public.slot_pool VALUES (1, 'default_pool', 128, 'Default pool');










SELECT pg_catalog.setval('public.chart_id_seq', 1, true);



SELECT pg_catalog.setval('public.connection_id_seq', 38, true);




SELECT pg_catalog.setval('public.dag_pickle_id_seq', 1, false);


SELECT pg_catalog.setval('public.dag_run_id_seq', 1, false);



SELECT pg_catalog.setval('public.import_error_id_seq', 1, false);


SELECT pg_catalog.setval('public.job_id_seq', 1, true);


SELECT pg_catalog.setval('public.known_event_id_seq', 1, false);




SELECT pg_catalog.setval('public.known_event_type_id_seq', 4, true);


SELECT pg_catalog.setval('public.log_id_seq', 2, true);




SELECT pg_catalog.setval('public.slot_pool_id_seq', 1, true);



SELECT pg_catalog.setval('public.task_fail_id_seq', 1, false);


SELECT pg_catalog.setval('public.task_reschedule_id_seq', 1, false);



SELECT pg_catalog.setval('public.user_id_seq', 1, false);


SELECT pg_catalog.setval('public.variable_id_seq', 1, false);



SELECT pg_catalog.setval('public.xcom_id_seq', 1, false);




CREATE INDEX dag_id_state ON public.dag_run USING lsm (dag_id, state);


CREATE INDEX idx_fileloc_hash ON public.serialized_dag USING lsm (fileloc_hash);



CREATE INDEX idx_job_state_heartbeat ON public.job USING lsm (state, latest_heartbeat);


CREATE INDEX idx_log_dag ON public.log USING lsm (dag_id);



CREATE INDEX idx_root_dag_id ON public.dag USING lsm (root_dag_id);




CREATE INDEX idx_task_fail_dag_task_date ON public.task_fail USING lsm (dag_id, task_id, execution_date);




CREATE INDEX idx_task_reschedule_dag_task_date ON public.task_reschedule USING lsm (dag_id, task_id, execution_date);



CREATE INDEX idx_xcom_dag_task_date ON public.xcom USING lsm (dag_id, task_id, execution_date);



CREATE INDEX job_type_heart ON public.job USING lsm (job_type, latest_heartbeat);




CREATE INDEX sm_dag ON public.sla_miss USING lsm (dag_id);



CREATE INDEX ti_dag_date ON public.task_instance USING lsm (dag_id, execution_date);


CREATE INDEX ti_dag_state ON public.task_instance USING lsm (dag_id, state);



CREATE INDEX ti_job_id ON public.task_instance USING lsm (job_id);



CREATE INDEX ti_pool ON public.task_instance USING lsm (pool, state, priority_weight);


CREATE INDEX ti_state ON public.task_instance USING lsm (state);



CREATE INDEX ti_state_lkp ON public.task_instance USING lsm (dag_id, task_id, execution_date, state);


ALTER TABLE ONLY public.chart
    ADD CONSTRAINT chart_user_id_fkey FOREIGN KEY (user_id) REFERENCES public.users(id);


ALTER TABLE ONLY public.dag_tag
    ADD CONSTRAINT dag_tag_dag_id_fkey FOREIGN KEY (dag_id) REFERENCES public.dag(dag_id);


ALTER TABLE ONLY public.known_event
    ADD CONSTRAINT known_event_known_event_type_id_fkey FOREIGN KEY (known_event_type_id) REFERENCES public.known_event_type(id);


ALTER TABLE ONLY public.known_event
    ADD CONSTRAINT known_event_user_id_fkey FOREIGN KEY (user_id) REFERENCES public.users(id);



ALTER TABLE ONLY public.task_reschedule
    ADD CONSTRAINT task_reschedule_dag_task_date_fkey FOREIGN KEY (task_id, dag_id, execution_date) REFERENCES public.task_instance(task_id, dag_id, execution_date) ON DELETE CASCADE;



