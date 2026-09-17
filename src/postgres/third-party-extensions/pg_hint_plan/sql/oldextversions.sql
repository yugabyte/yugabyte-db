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
ALTER EXTENSION pg_hint_plan UPDATE TO "1.3.10";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.3.11";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.4";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.4.1";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.4.2";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.4.3";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.4.4";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.4.5";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.5";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.5.1";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.5.2";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.5.3";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.5.4";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.6.0";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.6.1";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.6.2";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.6.3";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.7.0";
\dx+ pg_hint_plan
\d hint_plan.hints
ALTER EXTENSION pg_hint_plan UPDATE TO "1.7.1";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.7.2";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.8.0";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.8.1";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.9.0";
\dx+ pg_hint_plan
ALTER EXTENSION pg_hint_plan UPDATE TO "1.9.0-yb-1.0";
\dx+ pg_hint_plan
DROP EXTENSION pg_hint_plan;
