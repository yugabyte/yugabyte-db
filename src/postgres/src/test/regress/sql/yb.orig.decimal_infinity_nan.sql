--
-- DECIMAL / NUMERIC Infinity and NaN support (GH #23075)
--

-- Hash codes for specials (ASC DocDB / yb_hash_code payload sentinels).
SELECT yb_hash_code('-Infinity'::numeric) AS neg_inf_hash;
SELECT yb_hash_code('Infinity'::numeric) AS pos_inf_hash;
SELECT yb_hash_code('NaN'::numeric) AS nan_hash;

-- Ordering and uniqueness on a hash-partitioned PK.
CREATE TABLE dec_special_pk (k numeric PRIMARY KEY, v int);
INSERT INTO dec_special_pk VALUES
  ('-Infinity', 1),
  (-1, 2),
  (0, 3),
  (1, 4),
  ('Infinity', 5),
  ('NaN', 6);
SELECT k, v FROM dec_special_pk ORDER BY k;
-- NaN equals NaN for uniqueness.
INSERT INTO dec_special_pk VALUES ('NaN', 7); -- error
SELECT k, v FROM dec_special_pk WHERE k = 'NaN';
SELECT k, v FROM dec_special_pk WHERE yb_hash_code(k) = yb_hash_code('NaN'::numeric);

-- Range-ordered secondary index (DESC).
CREATE TABLE dec_special_idx (k int PRIMARY KEY, d numeric);
CREATE INDEX ON dec_special_idx (d DESC);
INSERT INTO dec_special_idx VALUES
  (1, 'NaN'),
  (2, 'Infinity'),
  (3, 0),
  (4, '-Infinity');
SELECT k, d FROM dec_special_idx ORDER BY d DESC, k;

DROP TABLE dec_special_pk;
DROP TABLE dec_special_idx;
