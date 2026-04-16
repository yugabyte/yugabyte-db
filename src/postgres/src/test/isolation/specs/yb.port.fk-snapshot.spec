setup
{
  CREATE TABLE pk_noparted (
	a			int		PRIMARY KEY
  );

  CREATE TABLE fk_parted_pk (
	a			int		PRIMARY KEY REFERENCES pk_noparted ON DELETE CASCADE
  ) PARTITION BY LIST (a);
  CREATE TABLE fk_parted_pk_1 PARTITION OF fk_parted_pk FOR VALUES IN (1);
  CREATE TABLE fk_parted_pk_2 PARTITION OF fk_parted_pk FOR VALUES IN (2);

  CREATE TABLE fk_noparted (
	a			int		REFERENCES fk_parted_pk ON DELETE NO ACTION INITIALLY DEFERRED
  );

  CREATE TABLE fk_noparted_sn (
	a			int		REFERENCES pk_noparted ON DELETE SET NULL
  );

  INSERT INTO pk_noparted VALUES (1);
  INSERT INTO fk_parted_pk VALUES (1);
  INSERT INTO fk_noparted VALUES (1);
}

teardown
{
  DROP TABLE pk_noparted, fk_parted_pk, fk_noparted, fk_noparted_sn;
}

session s1
step s1brr	{ BEGIN ISOLATION LEVEL REPEATABLE READ; SELECT COUNT(*) FROM pk_noparted UNION ALL SELECT COUNT(*) FROM fk_parted_pk; -- YB: to avoid restart read errors }
step s1brc	{ BEGIN ISOLATION LEVEL READ COMMITTED; }
step s1ifp2	{ INSERT INTO fk_parted_pk VALUES (2); }
step s1ifp1	{ INSERT INTO fk_parted_pk VALUES (1); }
step s1ifn2	{ INSERT INTO fk_noparted_sn VALUES (2); }
step s1dfp	{ DELETE FROM fk_parted_pk WHERE a = 1; }
step s1c	{ COMMIT; }
step s1sfp	{ SELECT * FROM fk_parted_pk; }
step s1sp	{ SELECT * FROM pk_noparted; }
step s1sfn	{ SELECT * FROM fk_noparted ORDER BY a; -- YB: Need ORDER BY if not using BasePgRegressTestPorted }

session s2
step s2brr	{ BEGIN ISOLATION LEVEL REPEATABLE READ; SELECT COUNT(*) FROM pk_noparted UNION ALL SELECT COUNT(*) FROM fk_parted_pk; -- YB: to avoid restart read errors }
step s2brc	{ BEGIN ISOLATION LEVEL READ COMMITTED; }
step s2ip2	{ INSERT INTO pk_noparted VALUES (2); }
step s2dp2	{ DELETE FROM pk_noparted WHERE a = 2; }
step s2ifn2	{ INSERT INTO fk_noparted VALUES (2); }
step s2c	{ COMMIT; }
step s2sfp	{ SELECT * FROM fk_parted_pk; }
step s2sfn	{ SELECT * FROM fk_noparted ORDER BY a; -- YB: Need ORDER BY if not using BasePgRegressTestPorted }

# inserting into referencing tables in transaction-snapshot mode
# PK table is non-partitioned
# YB: TODO(#28237): YSQL currently throws a serialization error instead of the "violates foreign key constraint" error
# in release builds.
permutation s1brr s2brc s2ip2 s1sp s2c s1sp s1ifp2 s1c s1sfp
# PK table is partitioned: buggy, because s2's serialization transaction can
# see the uncommitted row thanks to the latest snapshot taken for
# partition lookup to work correctly also ends up getting used by the PK index
# scan
# YB: TODO(#28134): YSQL currently throws a serialization error for the INSERT into fk_noparted instead of the succeeding.
# This behaviour is said to by "buggy" as per the Pg comment above. But let us still track this difference in a GitHub
# issue.
permutation s2ip2 s2brr s1brc s1ifp2 s2sfp s1c s2sfp s2ifn2 s2c s2sfn

# inserting into referencing tables in up-to-date snapshot mode
permutation s1brc s2brc s2ip2 s1sp s2c s1sp s1ifp2 s2brc s2sfp s1c s1sfp s2ifn2 s2c s2sfn

# deleting a referenced row and then inserting again in the same transaction; works
# the same no matter the snapshot mode
permutation s1brr s1dfp s1ifp1 s1c s1sfn
permutation s1brc s1dfp s1ifp1 s1c s1sfn

# trying to delete a row through DELETE CASCADE, whilst that row is deleted
# in a concurrent transaction
# YB: TODO(#27040): Pg faces a serialization error but YSQL doesn't. This is because in Pg, when
# DELETing/ UPDATing a tuple as part of a trigger using the latest snapshot, it also
# cross-checks visibility of that tuple against the "transaction" snapshot. If not visible,
# a serialization error is thrown.
permutation s2ip2 s1brr s1ifp2 s2brr s2dp2 s1c s2c

# trying to update a row through DELETE SET NULL, whilst that row is deleted
# in a concurrent transaction
# YB: TODO(#27040): Pg faces a serialization error but YSQL doesn't. This is because in Pg, when
# DELETing/ UPDATing a tuple as part of a trigger using the latest snapshot, it also
# cross-checks visibility of that tuple against the "transaction" snapshot. If not visible,
# a serialization error is thrown.
permutation s2ip2 s1brr s1ifn2 s2brr s2dp2 s1c s2c
