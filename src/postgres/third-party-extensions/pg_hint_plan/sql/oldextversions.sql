--
-- tests for upgrade paths
--

CREATE EXTENSION pg_hint_plan VERSION "1.3.0";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.3.1";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.3.2";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.3.3";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.3.4";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.3.5";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.3.6";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.3.7";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.3.8";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.3.9";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.4";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.4.1";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.4.2";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.5";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.5.1";
\dx+ pg_hint_plan
DROP EXTENSION pg_hint_plan;
