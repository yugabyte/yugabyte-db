BEGIN;

CREATE EXTENSION anon;

CREATE SCHEMA nba;

CREATE TABLE nba.player (
  id SERIAL,
  name TEXT,
  first_match DATE,
  last_match DATE,
  height_cm SMALLINT
);

INSERT INTO nba.player (name, first_match, last_match, height_cm)
VALUES
('LeBron James', '2003-10-29', '2023-04-10', 206),
('Kevin Durant', '2007-10-31', '2023-05-01', 208),
('Stephen Curry', '2009-10-28', '2023-05-01', 191),
('James Harden', '2009-10-27', '2023-05-01', 193),
('Kawhi Leonard', '2011-12-26', '2023-04-01', 198),
('Giannis Antetokounmpo', '2013-12-08', NULL, 211),
('Luka Dončić', '2018-10-17', NULL, 201),
('Damian Lillard', '2012-10-31', '2023-05-01', 191),
('Anthony Davis', '2012-10-31', NULL, 208),
('Chris Paul', '2005-10-27', '2023-05-01', 183),
('Russell Westbrook', '2008-10-28', '2023-04-01', 191),
('Paul George', '2010-10-26', '2023-04-15', 201),
('Jimmy Butler', '2011-12-25', NULL, 198),
('DeMar DeRozan', '2009-10-28', '2023-04-10', 198),
('Zion Williamson', '2019-10-22', NULL, 198),
('Jayson Tatum', '2017-10-17', NULL, 201),
('Joel Embiid', '2016-10-26', '2023-05-01', 213),
('Kemba Walker', '2011-12-25', NULL, 183),
('Ben Simmons', '2017-10-18', '2023-04-01', 208),
('Donovan Mitchell', '2017-10-18', '2023-05-01', 185),
('Trae Young', '2018-07-01', NULL, 183),
('Rudy Gobert', '2014-10-29', NULL, 213),
('Victor Oladipo', '2013-10-29', NULL, 193),
('Klay Thompson', '2011-12-25', '2023-04-10', 198),
('Draymond Green', '2012-04-28', NULL, 198),
('Chris Bosh', '2003-10-29', '2016-02-09', 206),
('Dwyane Wade', '2003-10-29', '2019-04-10', 193),
('Pau Gasol', '2001-10-30', '2021-03-29', 213),
('Kevin Garnett', '1995-11-01', '2016-09-23', 203),
('Tim Duncan', '1997-11-04', '2016-04-13', 211),
('Allen Iverson', '1996-11-01', '2010-10-30', 183),
('Kobe Bryant', '1996-11-03', '2016-04-13', 198),
('Russell Westbrook', '2008-10-28', '2023-04-01', 191),
('Carmelo Anthony', '2003-10-29', NULL, 201),
('Alonzo Mourning', '1992-11-03', '2008-04-14', 203),
('Reggie Miller', '1987-10-25', '2005-04-29', 196),
('Jason Kidd', '1994-10-31', '2013-04-13', 193),
('Steve Nash', '1996-10-31', '2015-04-13', 191),
('Kevin McHale', '1980-10-12', '1993-04-10', 203),
('Larry Bird', '1979-10-12', '1992-05-18', 206),
('Magic Johnson', '1979-10-28', '1991-11-09', 206),
('Bill Russell', '1956-11-01', '1969-05-05', 188),
('Michael Jordan', '1984-10-26', '2003-04-16', 198),
('Scottie Pippen', '1987-10-31', '2004-04-15', 198),
('Dikembe Mutombo', '1991-11-07', '2009-04-13', 218),
('Yao Ming', '2002-10-30', '2011-07-20', 229),
('Grant Hill', '1994-11-04', '2013-04-07', 203),
('Vince Carter', '1998-10-28', '2020-08-14', 196),
('Steve Francis', '1999-10-31', NULL, 183),
('Tracy McGrady', '1997-10-23', '2013-04-04', 201),
('Paul Pierce', '1998-10-17', '2017-04-13', 198),
('David Robinson', '1989-10-26', '2003-04-14', 218),
('Chris Webber', '1993-11-03', '2008-05-19', 206),
('Ben Wallace', '1996-11-01', '2012-05-09', 198),
('Dennis Rodman', '1986-10-24', '2000-05-19', 201),
('Ray Allen', '1996-10-31', '2018-04-10', 193),
('Rashard Lewis', '1998-10-31', NULL, 201),
('Juwan Howard', '1994-11-03', NULL, 203),
('Kenny Smith', '1987-10-31', NULL, 183),
('Rick Fox', '1991-10-31', NULL, 198),
('Carlos Boozer', '2002-10-29', '2020-02-01', 203),
('Michael Finley', '1996-10-31', NULL, 193),
('Shawn Marion', '1999-10-31', NULL, 201),
('Nate Robinson', '2005-10-25', NULL, 175),
('Paul Millsap', '2006-10-31', NULL, 201),
('Kevin Love', '2008-10-28', '2023-05-01', 203),
('DeAndre Jordan', '2008-10-28', NULL, 208),
('Dwight Howard', '2004-10-28', '2023-05-01', 211),
('Kris Dunn', '2016-10-27', NULL, 193),
('Clint Capela', '2014-10-29', NULL, 206),
('Bam Adebayo', '2017-10-18', NULL, 203),
('Tyrese Haliburton', '2020-12-23', NULL, 193),
('Michael Porter Jr.', '2018-10-17', NULL, 198),
('Jaren Jackson Jr.', '2018-10-17', NULL, 206),
('Cade Cunningham', '2021-10-20', NULL, 193),
('Scottie Barnes', '2021-10-20', NULL, 198),
('Evan Mobley', '2021-10-20', NULL, 206),
('Jaden Ivey', '2022-10-19', NULL, 188),
('Paolo Banchero', '2022-10-19', NULL, 203),
('Victor Wembanyama', '2023-10-25', NULL, 221);

SECURITY LABEL FOR anon ON FUNCTION pg_catalog.date_trunc(text,interval)
  IS 'TRUSTED';

SECURITY LABEL FOR anon ON COLUMN nba.player.name
  IS  'MASKED WITH VALUE NULL';

SECURITY LABEL FOR anon ON COLUMN nba.player.first_match
  IS  $$ MASKED WITH FUNCTION pg_catalog.date_trunc('year',first_match) $$;


SECURITY LABEL FOR anon ON COLUMN nba.player.last_match
  IS  $$ MASKED WITH FUNCTION pg_catalog.date_trunc('year',last_match) $$;

--
-- Dynamic masking
--

--
-- Sheila runs analytic requests on the player stats.
-- She is autorized to see only retired player ( `last_match IS NOT NULL` )
--
CREATE ROLE sheila LOGIN;


ALTER TABLE nba.player ENABLE ROW LEVEL SECURITY;

CREATE POLICY analytics_on_retired_players ON nba.player TO sheila
    USING (last_match IS NOT NULL);

SECURITY LABEL FOR anon ON ROLE sheila IS 'MASKED';

GRANT USAGE ON SCHEMA nba TO sheila;
GRANT ALL ON ALL TABLES IN SCHEMA nba TO sheila;

SET anon.transparent_dynamic_masking TO true;

SELECT COUNT(*) = 80 FROM nba.player;

-- Average height of players who played in the 90s
SELECT AVG(height_cm)::INT = 199
FROM nba.player
WHERE daterange(first_match, last_match) && daterange '[1990-01-01,2000-01-01)';

SET ROLE sheila;

-- Sheila sees only retired players

SELECT COUNT(*) = 46 FROM nba.player;

-- Average height of players who played in the 90s
SELECT AVG(height_cm)::INT = 201
FROM nba.player
WHERE daterange(first_match, last_match) && daterange '[1990-01-01,2000-01-01)';


RESET ROLE;

-- One player is retiring
UPDATE nba.player SET last_match = '2024-10-23' WHERE name = 'DeAndre Jordan';

SET ROLE sheila;

SELECT COUNT(*) = 47 FROM nba.player;

ROLLBACK;
