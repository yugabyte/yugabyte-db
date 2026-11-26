LOAD 'pg_hint_plan';

CREATE TABLE ja (a int);
CREATE TABLE jb (b int);

-- Leading hint on a join that is proven empty
EXPLAIN (COSTS OFF) SELECT * FROM ja, jb WHERE false;
/*+ Leading(ja jb) */ SELECT * FROM ja, jb WHERE false;
/*+ Leading(ja jb) */ SELECT * FROM ja JOIN jb ON a = b WHERE a = 1 AND a = 2;
/*+ Leading(ja jb) */ SELECT * FROM ja JOIN jb ON a = b;

DROP TABLE ja, jb;
