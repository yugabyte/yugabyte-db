-- Copyright (c) YugaByte, Inc.

/*
 * In V181 we remove "customer.universe_uuids". If some universes are not linked
 * to any customers, these universes will become visible (unexpectedly).
 * 
 * We are deleting such unlinked universes copying the records into a separate table.
 * In later releases the table should be removed.
 * 
 * Use next command to restore removed universes:
 *
 * INSERT INTO universe (
 *     SELECT * FROM removed_universes_V180_9 WHERE universe_uuid::text NOT IN
 *        (SELECT unnest(string_to_array(universe_uuids, ',')) AS a FROM customer));
 *
 */

DO
$$
  BEGIN
	  IF EXISTS(SELECT column_name FROM information_schema.columns
	            WHERE table_name='customer' and column_name='universe_uuids') THEN
	    IF EXISTS(SELECT * FROM universe WHERE universe_uuid::text NOT IN
                (SELECT unnest(string_to_array(universe_uuids, ',')) AS a FROM customer)) THEN

        CREATE TABLE removed_universes_V180_9 (LIKE universe INCLUDING ALL);

        INSERT INTO removed_universes_V180_9 (
          SELECT * FROM universe WHERE universe_uuid::text NOT IN
            (SELECT unnest(string_to_array(universe_uuids, ',')) AS a FROM customer));

        DELETE FROM universe WHERE universe_uuid::text NOT IN
            (SELECT unnest(string_to_array(universe_uuids, ',')) AS a FROM customer);
      END IF;
    END IF;
  END;
$$;
