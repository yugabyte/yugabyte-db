--
-- YB_PASSWORD_HISTORY Testsuite: password-reuse blocking via pg_yb_password_history.
--
-- Run with yb_allow_prehashed_passwords = false and yb_password_reuse_history = 3
-- (see TestPgRegressPasswordHistory.java).
--
-- pg_yb_password_history is not publicly readable.
--

SELECT has_table_privilege('pg_catalog.pg_yb_password_history', 'SELECT') AS superuser_select;
SELECT has_table_privilege('public', 'pg_catalog.pg_yb_password_history', 'SELECT') AS public_select;

SET yb_non_ddl_txn_for_sys_tables_allowed TO on;
INSERT INTO pg_catalog.pg_yb_password_history (pwdhstrole, pwdhstchangetime, pwdhstpassword)
VALUES (123456, '2000-01-01 00:00:00+00', 'mockhash');
RESET yb_non_ddl_txn_for_sys_tables_allowed;

-- A superuser can still read it.
SELECT count(*) FROM pg_catalog.pg_yb_password_history;

-- A non-superuser must be denied access to the catalog.
CREATE USER pwdhist_reader;
\c yugabyte pwdhist_reader
SELECT has_table_privilege('pg_catalog.pg_yb_password_history', 'SELECT') AS my_select;
SELECT * FROM pg_catalog.pg_yb_password_history;

\c yugabyte yugabyte
SET yb_non_ddl_txn_for_sys_tables_allowed TO on;
DELETE FROM pg_catalog.pg_yb_password_history WHERE pwdhstrole=123456;
RESET yb_non_ddl_txn_for_sys_tables_allowed;
DROP USER pwdhist_reader;
\d pg_yb_password_history
SELECT oid, relname, reltype, relnatts
  FROM pg_class WHERE relname = 'pg_yb_password_history';
SELECT typname FROM pg_type WHERE typname = 'pg_yb_password_history';

-- A reuse policy may be configured because pre-hashed passwords are off.
-- The settings come from ysql_pg_conf_csv, ALTER ROLE <user> SET, or
-- ALTER ROLE ALL SET. Session SET is rejected. A per-database setting is
-- rejected because roles are global.
SHOW yb_allow_prehashed_passwords;
SHOW yb_password_reuse_history;
SET yb_password_reuse_history = 1;                 -- ERROR
SET yb_password_reuse_interval = 60;               -- ERROR
ALTER ROLE ALL SET yb_password_reuse_interval = 60;
ALTER ROLE ALL RESET yb_password_reuse_interval;

-- The mutual exclusion holds in the other direction as well.
SET yb_allow_prehashed_passwords = on;             -- ERROR

-- capped values so shared catalog does not grow without bound.
-- The range is checked before the source restriction.
SET yb_password_reuse_history = 1025;              -- ERROR: range is (0, 1024)
SET yb_password_reuse_interval = 52560001;             -- ERROR: range is (0, 52560000)

-- ALTER ROLE ... SET is validated against the session value of
-- yb_allow_prehashed_passwords. Both windows have to be 0 before that
-- session value can be turned on.
ALTER ROLE ALL SET yb_password_reuse_history = 0;
\c yugabyte yugabyte
SET yb_allow_prehashed_passwords = on;
ALTER ROLE ALL SET yb_password_reuse_history = 3;  -- ERROR
RESET yb_allow_prehashed_passwords;
ALTER ROLE ALL RESET yb_password_reuse_history;
\c yugabyte yugabyte

-- Per-database forms are rejected. Roles are global across databases.
-- yb_allow_prehashed_passwords is a cluster switch: per-role and
-- per-database settings are rejected, ALTER ROLE ALL SET is accepted.
CREATE DATABASE phdb;
ALTER DATABASE phdb SET yb_password_reuse_history = 1; -- ERROR
ALTER DATABASE phdb SET yb_allow_prehashed_passwords = off; -- ERROR
DROP DATABASE phdb;
SELECT current_database() AS cur_db \gset
CREATE ROLE phscope LOGIN PASSWORD 'pw_one';
ALTER ROLE phscope SET yb_allow_prehashed_passwords = off; -- ERROR
ALTER ROLE phscope IN DATABASE :cur_db SET yb_password_reuse_history = 1; -- ERROR
ALTER ROLE phscope IN DATABASE :cur_db SET yb_allow_prehashed_passwords = off; -- ERROR
DROP ROLE phscope;
ALTER ROLE ALL SET yb_allow_prehashed_passwords = off;
ALTER ROLE ALL RESET yb_allow_prehashed_passwords;

-- Helper: number of secrets stored for a role (0 when the role has no history).
CREATE FUNCTION pwd_hist_len(text) RETURNS int LANGUAGE sql AS $$
  SELECT count(*)::int
    FROM pg_catalog.pg_yb_password_history h
    JOIN pg_authid a ON a.oid = h.pwdhstrole
   WHERE a.rolname = $1
$$;

-- CREATE recording (no reuse check on create -- history is empty).
CREATE USER phuser PASSWORD 'pw_one';
SELECT pwd_hist_len('phuser');

-- The secret is stored in pg_authid
SELECT DISTINCT split_part(pwdhstpassword, '$', 1) AS stored_format
  FROM pg_catalog.pg_yb_password_history h
  JOIN pg_authid a ON a.oid = h.pwdhstrole
 WHERE a.rolname = 'phuser';

-- Reuse runs in the authenticated user's own session: that login loaded
-- this role's policy. An admin reset applies this role's windows, not the
-- admin's, and does not reject (see phbypass).
\c yugabyte phuser
ALTER USER phuser PASSWORD 'pw_one';               -- fail: reuse
\c yugabyte yugabyte
SELECT pwd_hist_len('phuser');                     -- still 1

-- ALTER with new passwords accumulates history up to N (=3).
\c yugabyte phuser
ALTER USER phuser PASSWORD 'pw_two';
ALTER USER phuser PASSWORD 'pw_three';
\c yugabyte yugabyte
SELECT pwd_hist_len('phuser');                     -- 3

-- All three most-recent passwords are now blocked.
\c yugabyte phuser
ALTER USER phuser PASSWORD 'pw_one';               -- fail: reuse
ALTER USER phuser PASSWORD 'pw_two';               -- fail: reuse
ALTER USER phuser PASSWORD 'pw_three';             -- fail: reuse

--
-- Trimming: a 4th distinct password evicts the oldest (pw_one), so pw_one is
-- reusable again but pw_two/pw_three are still blocked.
--
ALTER USER phuser PASSWORD 'pw_four';
\c yugabyte yugabyte
SELECT pwd_hist_len('phuser');                     -- still 3 (trimmed)
\c yugabyte phuser
ALTER USER phuser PASSWORD 'pw_two';               -- fail: still in window
ALTER USER phuser PASSWORD 'pw_one';               -- ok: evicted
\c yugabyte yugabyte
SELECT pwd_hist_len('phuser');

-- Increasing the history window to 4. ALTER ROLE ALL applies on reconnect.
ALTER ROLE ALL SET yb_password_reuse_history = 4;
SELECT pwd_hist_len('phuser');                     -- still 3
\c yugabyte phuser
ALTER USER phuser PASSWORD 'pw_two';               -- success: now pw_one, two, three, four all stored
\c yugabyte yugabyte
SELECT pwd_hist_len('phuser');                     -- 4
\c yugabyte phuser
ALTER USER phuser PASSWORD 'pw_five';              -- now pw_one, two, four, five all stored
\c yugabyte yugabyte
SELECT pwd_hist_len('phuser');                     -- 4

-- Decreasing the history window to 2.
ALTER ROLE ALL SET yb_password_reuse_history = 2;
SELECT pwd_hist_len('phuser');                     -- still 4, no change at SET time
\c yugabyte phuser
ALTER USER phuser PASSWORD 'pw_two';               -- fail: last 2 passwords are pw_two and pw_five
\c yugabyte yugabyte
SELECT pwd_hist_len('phuser');                     -- still 4: the failed ALTER pruned nothing
\c yugabyte phuser
ALTER USER phuser PASSWORD 'pw_one';
\c yugabyte yugabyte
SELECT pwd_hist_len('phuser');                     -- 2

-- Two roles may hold the same password: each history is keyed by role OID and
-- each SCRAM secret carries its own salt.
CREATE USER phother PASSWORD 'pw_one';
SELECT pwd_hist_len('phother');

-- RENAME truncates the history.
ALTER USER phuser RENAME TO phuser2;
SELECT pwd_hist_len('phuser2');
\c yugabyte phuser2
ALTER USER phuser2 PASSWORD 'pw_one';
\c yugabyte yugabyte

-- PASSWORD NULL leaves history intact.
ALTER USER phuser2 PASSWORD NULL;
SELECT pwd_hist_len('phuser2');
\c yugabyte phuser2
ALTER USER phuser2 PASSWORD 'pw_one';
\c yugabyte yugabyte

-- Pre-hashed passwords are not allowed.
CREATE USER phscram PASSWORD 'SCRAM-SHA-256$4096:MDEyMzQ1Njc4OWFiY2RlZg==$AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=:ICEiIyQlJicoKSorLC0uLzAxMjM0NTY3ODk6Ozw9Pj8=';
CREATE USER phmd5 PASSWORD 'md5aabbccddeeff00112233445566778899';

-- A globals dump restores the stored verifier. Backup replay sets
-- yb_binary_restore; major-version upgrade is a binary-upgrade backend.
-- The verifier is recorded and not compared.
SET yb_binary_restore = true;
CREATE USER phrestore PASSWORD 'md5aabbccddeeff00112233445566778899';
SELECT pwd_hist_len('phrestore');
RESET yb_binary_restore;
DROP USER phrestore;

-- DROP purges the role's entire history.
DROP USER phuser2;
SELECT pwd_hist_len('phuser2');                     -- 0

DROP USER phother;

-- helper function to check if the password history is consistent with the pg_authid.
CREATE FUNCTION pwd_hist_consistent(text) RETURNS bool LANGUAGE sql AS $$
  SELECT (SELECT h.pwdhstpassword
            FROM pg_catalog.pg_yb_password_history h
           WHERE h.pwdhstrole = a.oid
           ORDER BY h.pwdhstchangetime DESC
           LIMIT 1) = a.rolpassword
    FROM pg_authid a WHERE a.rolname = $1
$$;

CREATE USER phtxn PASSWORD 'pw_one';
SELECT pwd_hist_len('phtxn'), pwd_hist_consistent('phtxn');

-- Reuse rejection must leave pg_authid and history unchanged.  The check may
-- prune expired rows in the same scan before it finds the match and errors;
-- those deletes must roll back with the failed ALTER.
\c yugabyte phtxn
ALTER USER phtxn PASSWORD 'pw_one';                -- fail: reuse
\c yugabyte yugabyte
SELECT pwd_hist_len('phtxn'), pwd_hist_consistent('phtxn');

\c yugabyte phtxn
BEGIN;
ALTER USER phtxn PASSWORD 'pw_one';                -- fail: reuse
ROLLBACK;
\c yugabyte yugabyte
SELECT pwd_hist_len('phtxn'), pwd_hist_consistent('phtxn');

-- A successful ALTER inside a block that is then rolled back. Transactional
-- DDL restores both pg_authid.rolpassword and the history row, and the two
-- stay consistent.
\c yugabyte phtxn
BEGIN;
ALTER USER phtxn PASSWORD 'pw_two';
ROLLBACK;
\c yugabyte yugabyte
SELECT pwd_hist_len('phtxn'), pwd_hist_consistent('phtxn');

DROP USER phtxn;
DROP FUNCTION pwd_hist_consistent(text);

-- Time-based reuse (yb_password_reuse_interval), the other half of the policy
-- Rows are aged by rewriting pwdhstchangetime directly
ALTER ROLE ALL SET yb_password_reuse_history = 0;
ALTER ROLE ALL SET yb_password_reuse_interval = 60;
\c yugabyte yugabyte
CREATE USER phtime PASSWORD 'pw_one';
SELECT pwd_hist_len('phtime');

-- Inside the window: blocked
\c yugabyte phtime
ALTER USER phtime PASSWORD 'pw_one';               -- fail: reuse
ALTER USER phtime PASSWORD 'pw_two';
\c yugabyte yugabyte
SELECT pwd_hist_len('phtime');                     -- 2
\c yugabyte phtime
ALTER USER phtime PASSWORD 'pw_one';               -- fail: still in window
\c yugabyte yugabyte

-- Age both rows past the cutoff.  They become unprotected, so the next change
-- accepts a repeat and prunes them in the same pass.
SET yb_non_ddl_txn_for_sys_tables_allowed TO on;
UPDATE pg_catalog.pg_yb_password_history h
   SET pwdhstchangetime = h.pwdhstchangetime - interval '2 hours'
  FROM pg_authid a
 WHERE a.oid = h.pwdhstrole AND a.rolname = 'phtime';
RESET yb_non_ddl_txn_for_sys_tables_allowed;
\c yugabyte phtime
ALTER USER phtime PASSWORD 'pw_one';               -- ok: aged out
\c yugabyte yugabyte
SELECT pwd_hist_len('phtime');                     -- 1: both old rows pruned
DROP USER phtime;

-- Both windows at once.  A row is protected while it falls in either.
ALTER ROLE ALL SET yb_password_reuse_history = 2;
ALTER ROLE ALL SET yb_password_reuse_interval = 60;
\c yugabyte yugabyte
CREATE USER phboth PASSWORD 'pw_one';
\c yugabyte phboth
ALTER USER phboth PASSWORD 'pw_two';
ALTER USER phboth PASSWORD 'pw_three';
\c yugabyte yugabyte
-- pw_one has fallen out of the count window but the time window
-- still covers it, so it was not pruned.
SELECT pwd_hist_len('phboth');
\c yugabyte phboth
ALTER USER phboth PASSWORD 'pw_one';               -- fail: held by time window
\c yugabyte yugabyte

-- Aging only pw_one releases it from both windows.
SET yb_non_ddl_txn_for_sys_tables_allowed TO on;
UPDATE pg_catalog.pg_yb_password_history h
   SET pwdhstchangetime = h.pwdhstchangetime - interval '2 hours'
  FROM pg_authid a
 WHERE a.oid = h.pwdhstrole AND a.rolname = 'phboth'
   AND h.pwdhstchangetime = (SELECT min(h2.pwdhstchangetime)
                            FROM pg_catalog.pg_yb_password_history h2
                           WHERE h2.pwdhstrole = h.pwdhstrole);
RESET yb_non_ddl_txn_for_sys_tables_allowed;
\c yugabyte phboth
ALTER USER phboth PASSWORD 'pw_one';               -- ok: released by both
ALTER USER phboth PASSWORD 'pw_two';               -- fail: still in both
\c yugabyte yugabyte
DROP USER phboth;

-- MD5 secrets are recomputed from the role name, so hashing produces the same string.
-- An aged-out row holds the very hash about to be inserted, and only survives
-- because the prune runs before the insert.
-- The count window has to be off to reach that path at all.
ALTER ROLE ALL SET yb_password_reuse_history = 0;
\c yugabyte yugabyte
SET password_encryption = 'md5';
CREATE USER phmd5time PASSWORD 'pw_one';
\c yugabyte phmd5time
SET password_encryption = 'md5';
ALTER USER phmd5time PASSWORD 'pw_one';            -- fail: reuse
\c yugabyte yugabyte
SET password_encryption = 'md5';
SET yb_non_ddl_txn_for_sys_tables_allowed TO on;
UPDATE pg_catalog.pg_yb_password_history h
   SET pwdhstchangetime = h.pwdhstchangetime - interval '2 hours'
  FROM pg_authid a
 WHERE a.oid = h.pwdhstrole AND a.rolname = 'phmd5time';
RESET yb_non_ddl_txn_for_sys_tables_allowed;
-- No duplicate-key error: the identical hash is deleted before it is inserted.
\c yugabyte phmd5time
SET password_encryption = 'md5';
ALTER USER phmd5time PASSWORD 'pw_one';
\c yugabyte yugabyte
SELECT pwd_hist_len('phmd5time');                  -- 1
DROP USER phmd5time;
RESET password_encryption;
ALTER ROLE ALL RESET yb_password_reuse_interval;
ALTER ROLE ALL RESET yb_password_reuse_history;
\c yugabyte yugabyte

-- ALTER ROLE SET stores per-role defaults in pg_db_role_setting.
-- SET ROLE switches privilege checks only; it does not reload role defaults
-- (those are applied at connection start for the session user).
CREATE USER phguc PASSWORD 'pw_a';
ALTER USER phguc SET yb_password_reuse_history = 5;
SELECT unnest(r.setconfig) AS setting
  FROM pg_db_role_setting r
  JOIN pg_authid a ON a.oid = r.setrole
 WHERE a.rolname = 'phguc';

-- Still the session user's value (3 from cluster), not phguc's ALTER ROLE SET.
SET ROLE phguc;
SHOW yb_password_reuse_history;
RESET ROLE;

-- Admin reset does not apply this session's window of 3. phguc's role
-- default is 5; applying the admin window would prune the CREATE row, which
-- that role still protects. The assigned secret is recorded beside it.
ALTER USER phguc PASSWORD 'pw_b';
SELECT pwd_hist_len('phguc');                      -- 2

-- Login as phguc: role defaults load at connection start, so the window is 5.
\c yugabyte phguc
SHOW yb_password_reuse_history;
ALTER USER phguc PASSWORD 'pw_c';
ALTER USER phguc PASSWORD 'pw_d';
ALTER USER phguc PASSWORD 'pw_e';
ALTER USER phguc PASSWORD 'pw_f';

\c yugabyte yugabyte
SELECT pwd_hist_len('phguc');
DROP USER phguc;

-- history = 1 blocks only the current password. The one before it is reusable
-- on the next change.
ALTER ROLE ALL SET yb_password_reuse_history = 1;
\c yugabyte yugabyte
CREATE USER phhist1 PASSWORD 'pw_one';
\c yugabyte phhist1
ALTER USER phhist1 PASSWORD 'pw_one';              -- fail: current
ALTER USER phhist1 PASSWORD 'pw_two';
ALTER USER phhist1 PASSWORD 'pw_one';              -- ok: evicted
\c yugabyte yugabyte
SELECT pwd_hist_len('phhist1');                    -- 1
DROP USER phhist1;
ALTER ROLE ALL RESET yb_password_reuse_history;
\c yugabyte yugabyte

-- A row still inside the count window stays blocked after it ages out of the
-- time window. The combined case above only ages a row already outside the count.
ALTER ROLE ALL SET yb_password_reuse_history = 2;
ALTER ROLE ALL SET yb_password_reuse_interval = 60;
\c yugabyte yugabyte
CREATE USER phcount PASSWORD 'pw_old';
\c yugabyte phcount
ALTER USER phcount PASSWORD 'pw_new';
\c yugabyte yugabyte
SET yb_non_ddl_txn_for_sys_tables_allowed TO on;
UPDATE pg_catalog.pg_yb_password_history h
   SET pwdhstchangetime = h.pwdhstchangetime - interval '2 hours'
  FROM pg_authid a
 WHERE a.oid = h.pwdhstrole AND a.rolname = 'phcount'
   AND h.pwdhstchangetime = (SELECT min(h2.pwdhstchangetime)
                            FROM pg_catalog.pg_yb_password_history h2
                           WHERE h2.pwdhstrole = h.pwdhstrole);
RESET yb_non_ddl_txn_for_sys_tables_allowed;
\c yugabyte phcount
ALTER USER phcount PASSWORD 'pw_old';              -- fail: count window
\c yugabyte yugabyte
DROP USER phcount;

-- The time cutoff is inclusive (changetime >= now - interval). The ALTER reads
-- the clock again, so the stamps sit five minutes on each side of that line
-- rather than a few seconds, which flakes on a slow sanitizer run.
ALTER ROLE ALL SET yb_password_reuse_history = 0;
\c yugabyte yugabyte
CREATE USER phcut PASSWORD 'pw_one';
SET yb_non_ddl_txn_for_sys_tables_allowed TO on;
UPDATE pg_catalog.pg_yb_password_history h
   SET pwdhstchangetime = clock_timestamp() - interval '60 minutes' + interval '5 minutes'
  FROM pg_authid a
 WHERE a.oid = h.pwdhstrole AND a.rolname = 'phcut';
RESET yb_non_ddl_txn_for_sys_tables_allowed;
\c yugabyte phcut
ALTER USER phcut PASSWORD 'pw_one';                -- fail: still inside
\c yugabyte yugabyte
SET yb_non_ddl_txn_for_sys_tables_allowed TO on;
UPDATE pg_catalog.pg_yb_password_history h
   SET pwdhstchangetime = clock_timestamp() - interval '60 minutes' - interval '5 minutes'
  FROM pg_authid a
 WHERE a.oid = h.pwdhstrole AND a.rolname = 'phcut';
RESET yb_non_ddl_txn_for_sys_tables_allowed;
\c yugabyte phcut
ALTER USER phcut PASSWORD 'pw_one';                -- ok: past the cutoff, old row pruned
\c yugabyte yugabyte
SELECT pwd_hist_len('phcut');                      -- 1
DROP USER phcut;
ALTER ROLE ALL RESET yb_password_reuse_interval;
ALTER ROLE ALL RESET yb_password_reuse_history;
\c yugabyte yugabyte

-- An empty password clears rolpassword, writes no history row, and leaves the
-- previous secret blocked.
CREATE USER phempty PASSWORD 'pw_one';
SELECT pwd_hist_len('phempty');
\c yugabyte phempty
ALTER USER phempty PASSWORD '';
\c yugabyte yugabyte
SELECT rolpassword IS NULL AS cleared
  FROM pg_authid WHERE rolname = 'phempty';
SELECT pwd_hist_len('phempty');                    -- still 1
\c yugabyte phempty
ALTER USER phempty PASSWORD 'pw_one';              -- fail: previous secret
\c yugabyte yugabyte
DROP USER phempty;

-- No PASSWORD clause writes nothing. DROP of a role with no history is silent.
CREATE USER phnopw;
SELECT pwd_hist_len('phnopw');                     -- 0
ALTER USER phnopw CREATEDB;
SELECT pwd_hist_len('phnopw');                     -- 0
DROP USER phnopw;
CREATE USER phnopw2 PASSWORD 'pw_one';
ALTER USER phnopw2 CREATEDB;
SELECT pwd_hist_len('phnopw2');                    -- still 1
DROP USER phnopw2;

-- ALTER ROLE SET yb_password_reuse_history = 0 is stored and applied at login,
-- which turns the cluster policy off for that session.
CREATE USER phbypass PASSWORD 'pw_one';
ALTER USER phbypass SET yb_password_reuse_history = 0;
\c yugabyte phbypass
SHOW yb_password_reuse_history;
ALTER USER phbypass PASSWORD 'pw_one';             -- ok: role setting is 0
\c yugabyte yugabyte
SELECT pwd_hist_len('phbypass');                   -- still 1: the bypass recorded nothing
-- An admin reset applies the role's windows, not this session's 3, so it
-- records nothing either.
ALTER USER phbypass PASSWORD 'pw_two';
SELECT pwd_hist_len('phbypass');                   -- still 1
DROP USER phbypass;

-- An admin reset prunes to the role's own window of 5, not this session's
-- 3, and does not reject: pw_five is inside both windows.
CREATE USER phadmin PASSWORD 'pw_one';
ALTER USER phadmin SET yb_password_reuse_history = 5;
\c yugabyte phadmin
ALTER USER phadmin PASSWORD 'pw_two';
ALTER USER phadmin PASSWORD 'pw_three';
ALTER USER phadmin PASSWORD 'pw_four';
ALTER USER phadmin PASSWORD 'pw_five';
\c yugabyte yugabyte
SELECT pwd_hist_len('phadmin');                    -- 5
ALTER USER phadmin PASSWORD 'pw_five';
ALTER USER phadmin PASSWORD 'pw_admin';
SELECT pwd_hist_len('phadmin');                    -- 5: both recorded, oldest two pruned
\c yugabyte phadmin
ALTER USER phadmin PASSWORD 'pw_admin';            -- fail: secret just assigned
\c yugabyte yugabyte
DROP USER phadmin;

-- An admin whose own windows are 0 still records the secret it assigns, so
-- the target's per-role policy protects it.
CREATE USER phsu SUPERUSER;
ALTER USER phsu SET yb_password_reuse_history = 0;
CREATE USER phrole PASSWORD 'pw_one';
ALTER USER phrole SET yb_password_reuse_history = 5;
\c yugabyte phsu
SHOW yb_password_reuse_history;
ALTER USER phrole PASSWORD 'pw_admin';
\c yugabyte yugabyte
SELECT pwd_hist_len('phrole');                     -- 2
\c yugabyte phrole
ALTER USER phrole PASSWORD 'pw_admin';             -- fail: secret just assigned
\c yugabyte yugabyte
DROP USER phrole;
-- phsu's own setting replaced the cluster window at its login, so a role
-- that inherits that window has unknown windows here: record, prune nothing.
CREATE USER phinherit PASSWORD 'pw_one';
\c yugabyte phsu
ALTER USER phinherit PASSWORD 'pw_two';
ALTER USER phinherit PASSWORD 'pw_three';
ALTER USER phinherit PASSWORD 'pw_four';
\c yugabyte yugabyte
SELECT pwd_hist_len('phinherit');                  -- 4
DROP USER phinherit;
-- CREATE does not use the creator's windows either: phsu's 0 does not skip
-- recording the initial password.
\c yugabyte phsu
CREATE USER phnew PASSWORD 'pw_one';
\c yugabyte yugabyte
SELECT pwd_hist_len('phnew');                      -- 1
\c yugabyte phnew
ALTER USER phnew PASSWORD 'pw_one';                -- fail: initial password
\c yugabyte yugabyte
DROP USER phnew;
DROP USER phsu;

-- A role whose password only an admin changes stays within the cluster
-- window of 3.
CREATE USER phsvc PASSWORD 'pw_one';
ALTER USER phsvc PASSWORD 'pw_two';
ALTER USER phsvc PASSWORD 'pw_three';
ALTER USER phsvc PASSWORD 'pw_four';
ALTER USER phsvc PASSWORD 'pw_five';
SELECT pwd_hist_len('phsvc');                      -- 3
\c yugabyte phsvc
ALTER USER phsvc PASSWORD 'pw_three';              -- fail: still protected
ALTER USER phsvc PASSWORD 'pw_two';                -- ok: pruned
\c yugabyte yugabyte
DROP USER phsvc;

-- A plaintext is blocked whichever verifier was stored. Switching
-- password_encryption between SCRAM and MD5 does not open a hole.
CREATE USER phenc PASSWORD 'pw_one';
\c yugabyte phenc
SET password_encryption = 'md5';
ALTER USER phenc PASSWORD 'pw_one';                -- fail: stored SCRAM
ALTER USER phenc PASSWORD 'pw_two';
SET password_encryption = 'scram-sha-256';
ALTER USER phenc PASSWORD 'pw_two';                -- fail: stored MD5
ALTER USER phenc PASSWORD 'pw_one';                -- fail: stored SCRAM
RESET password_encryption;
\c yugabyte yugabyte
DROP USER phenc;

DROP FUNCTION pwd_hist_len(text);
