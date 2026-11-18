
-- This test can't be runned inside a single transaction
BEGIN;

SET ROLE postgres;

CREATE EXTENSION IF NOT EXISTS anon;

CREATE TABLE app_log (
    tms   TIMESTAMP NOT NULL,
    login VARCHAR(255) NOT NULL,
    ip INET NOT NULL,
    action TEXT
);

INSERT INTO app_log (tms, login, ip, action) VALUES
    ('2024-11-04 08:23:15', 'john', '192.168.1.100', 'login_success'),
    ('2024-11-04 08:25:32', 'sarah', '10.0.0.45', 'view_dashboard'),
    ('2024-11-04 09:15:00', 'mike', '172.16.0.89', 'update_profile'),
    ('2024-11-04 09:30:45', 'emma', '192.168.2.200', 'download_report'),
    ('2024-11-04 10:05:22', 'john', '192.168.1.100', 'logout'),
    ('2024-11-04 11:45:10', 'alex', '10.0.0.78', 'login_failed'),
    ('2024-11-04 12:00:00', 'sarah', '10.0.0.45', 'create_document'),
    ('2024-11-04 13:20:33', 'peter', '172.16.0.150', 'login_success'),
    ('2024-11-04 14:15:27', 'wilson', '192.168.2.200', 'share_file'),
    ('2024-11-04 15:00:00', 'mike', '172.16.0.89', 'send_message'),
    ('2024-11-04 15:45:12', 'alex', '10.0.0.78', 'login_success'),
    ('2024-11-04 16:30:00', 'peter', '172.16.0.150', 'logout');

COPY app_log TO '/tmp/app.log';

CREATE EXTENSION IF NOT EXISTS file_fdw;

CREATE SERVER fdw_files FOREIGN DATA WRAPPER file_fdw;

CREATE SCHEMA files;

CREATE FOREIGN TABLE files.app_log
(
    tms   TIMESTAMP,
    login VARCHAR(255),
    ip INET,
    action TEXT
)
  SERVER fdw_files
  OPTIONS ( filename '/tmp/app.log' )
;


-- David wants to extract some stats from the applications logs but he should
-- not have access to personal informations

CREATE ROLE david;

SECURITY LABEL FOR anon ON ROLE david IS 'MASKED';

SECURITY LABEL FOR anon ON COLUMN files.app_log.login
  IS 'MASKED WITH VALUE $$CONFIDENTIAL$$';

SECURITY LABEL FOR anon ON COLUMN files.app_log.ip
  IS 'MASKED WITH FUNCTION anon.dummy_ipv4()';

GRANT USAGE ON SCHEMA files TO david;
GRANT SELECT ON TABLE files.app_log TO david;

SET anon.transparent_dynamic_masking TO TRUE;

SET ROLE david;

SELECT login = 'CONFIDENTIAL' FROM files.app_log LIMIT 1;

RESET ROLE;

DROP SCHEMA files CASCADE;

DROP SERVER fdw_files CASCADE;

DROP ROLE david;

ROLLBACK;
