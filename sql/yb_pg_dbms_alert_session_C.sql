\set ECHO all

/* Register alerts */
SELECT dbms_alert.register('a1');
SELECT dbms_alert.register('a2');

/* Test: multisession waitone */
SELECT dbms_alert.waitone('a1',20);

/* Test: multisession waitany */
SELECT dbms_alert.waitany(10);

/* cleanup */
SELECT dbms_alert.removeall();

