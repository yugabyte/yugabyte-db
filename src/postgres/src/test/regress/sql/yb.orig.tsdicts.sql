--
-- YB non-ported tests on text search configurations and dictionaries.
--

CREATE TEXT SEARCH CONFIGURATION ybtscfg (COPY=english);
\dF+ ybtscfg

-- Test ALTER TEXT SEARCH CONFIGURATION syntax variations that aren't tested in
-- upstream postgres tests.
SELECT to_tsvector('ybtscfg', 'cafe 123');
ALTER TEXT SEARCH CONFIGURATION ybtscfg DROP MAPPING FOR int, uint;
SELECT to_tsvector('ybtscfg', 'cafe 123');
ALTER TEXT SEARCH CONFIGURATION ybtscfg DROP MAPPING FOR int, uint;
ALTER TEXT SEARCH CONFIGURATION ybtscfg DROP MAPPING IF EXISTS FOR int, uint;
\dF+ ybtscfg

ALTER TEXT SEARCH CONFIGURATION ybtscfg ADD MAPPING FOR uint
    WITH danish_stem;
SELECT to_tsvector('ybtscfg', 'cafe 123');
ALTER TEXT SEARCH CONFIGURATION ybtscfg ADD MAPPING FOR uint
    WITH danish_stem;
\dF+ ybtscfg

ALTER TEXT SEARCH CONFIGURATION ybtscfg ALTER MAPPING FOR asciiword, int, uint
    REPLACE english_stem WITH french_stem;
SELECT to_tsvector('ybtscfg', 'cafe 123');
ALTER TEXT SEARCH CONFIGURATION ybtscfg ALTER MAPPING FOR asciiword, int, uint
    REPLACE english_stem WITH french_stem;
\dF+ ybtscfg
