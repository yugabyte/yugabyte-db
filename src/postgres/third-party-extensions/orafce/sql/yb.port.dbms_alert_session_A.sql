\set ECHO none

-- YB note: advisory locks not yet supported.
-- wait for other processes, wait max 100 sec
do $$
declare c int;
begin
  if pg_try_advisory_xact_lock(1) then
    for i in 1..1000 loop
      perform pg_sleep(0.1);
      c := (select count(*) from pg_locks where locktype = 'advisory' and objid = 1 and not granted);
      if c = 2 then
        return;
      end if;
    end loop;
  else
    perform pg_advisory_xact_lock(1);
  end if;
end;
$$;

\set ECHO all

SELECT pg_sleep(3);

/* 
 * DBMS_ALERT is used for one-way communication of one session to other.
 *
 * This session mainly sends signals for testing the alert functionality in 
 * session B and C.
 *
 * The following alerts are used to ensure that signals are sent at correct 
 * times to session B for testing. These signals are sent from session B 
 * indicating completion of an event. 
 * After the signal is received, the next required signal for testing is sent
 * from this session. 
 */

SELECT dbms_alert.register('b1');
SELECT dbms_alert.register('b2');
SELECT dbms_alert.register('b3');
SELECT dbms_alert.register('b4');
SELECT dbms_alert.register('b5');

SELECT dbms_alert.signal('a1','Msg1 for a1');

SELECT dbms_alert.signal('a2','Msg1 for a2');

/* 
 * Test: defered_signal 
 * The signal is received only when the signalling transaction commits.
 * To test this, an explict BEGIN-COMMIT block is used. 
 */ 
SELECT dbms_alert.signal('tds','Begin defered_signal test');
BEGIN;
SELECT dbms_alert.signal('tds','Testing defered_signal');
/* The signal is received while transaction is running */
SELECT dbms_alert.waitone('b1',20);
COMMIT;
/* The signal is received after transaction completed.
 * After this the tds signal is received in session B indicating that the
 * signal is received only after commit.
 */
SELECT dbms_alert.waitone('b1',20);

SELECT dbms_alert.waitone('b2',20);
/* This signals a3 which is not registered in Session B */
SELECT dbms_alert.signal('a3','Msg1 for a3');
/* alert a4 is signalled soon after a3 */
SELECT dbms_alert.signal('a4','Test- Register after signal');

/* This signal indicates at remove() is called */
SELECT dbms_alert.waitone('b3',20);
/* Send signal which is removed in session B */
SELECT dbms_alert.signal('a1','Msg2 for a1');

SELECT dbms_alert.waitone('b4',20);
/* Send signal which is registered in B and not removed */
SELECT dbms_alert.signal('a4','Msg1 for a4');

/* This signal inidcates that removeall() is called */
SELECT dbms_alert.waitone('b5',20);
/* Send a signal to test if session B receives it after removeall() */
SELECT dbms_alert.signal('a2','Msg2 for a2');

/* cleanup */
SELECT dbms_alert.removeall();
