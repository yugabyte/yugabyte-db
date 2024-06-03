--
-- YB_FEATURE Testsuite: CREATE LANGUAGE
--
DROP EXTENSION plpgsql CASCADE;
CREATE LANGUAGE plpgsql;
CREATE FUNCTION test() RETURNS INTEGER AS $$begin return 1; end$$ LANGUAGE plpgsql;
SELECT * FROM test();
DROP LANGUAGE plpgsql CASCADE;
-- leave the cluster in a clean state
CREATE EXTENSION plpgsql;
