pg_partman Test Suite
=====================

The pgTAP testing suite is used to provide an extensive and easily maintainable set of unit tests. Please see the pgTAP home page for more details on its installation and use. 

http://pgTAP.org/

A minimum version of pgtap 0.99.1 is required for pg_partman's test suite.

Tests assume that the required extensions have been installed in the following schemas:

    pg_partman: partman
    pgTAP: public 

If you've installed any of the above extensions in a different schema and would like to run the test suite, simply change the configuration option found at the top of each testing file to match your setup. If you've also installed pg_jobmon, be aware that the logging of the tests cannot be rolled back and any failures will be picked up by the monitoring in the jobmon extension.

    SELECT set_config('search_path','partman, public',false);
    
Once that's done, it's best to use the **pg_prove** script that pgTAP comes with to run all the tests. I like using the  -o -f -v options to get more useful feedback.

    pg_prove -ovf /path/to/partman/test/*.sql

For most tests, they must be run by a superuser since roles & schemas are created & dropped as part of the test. There is a separate test script for each of the partitioning types. The tests are not required to run pg_partman, so if you don't feel safe doing this you don't need to run the tests. But if you are running into problems and report any issues without a clear explanation of what is wrong, I will ask that you run the test suite so you can try and narrow down where the problem may be. You are free to look through to tests to see exactly what they're doing. All tests in the top level of the test folder are run inside a transaction that is rolled back, so they should not change anything (except jobmon logging as mentioned).

Tests for the time-custom partition type are in their own folder. The 30second test frequently errors out if run in certain 30 second blocks. Waiting for the next 30 second block and running it again should allow it to pass. Same goes for the 30 second test in the native folder. 

Tests for the reindexing script can be found in the test/test_reindex folder. These tests cannot just be run all at once and are not run within rolled back transactions. They must be run in order, one at a time, and there are explicit instructions at the end of each test for what to do next.

Tests for the Background Worker can be found in the test/test_bgw folder. Please read the header comments at the top of each test for the postgresql.conf settings required for that test.

Tests for procedures are in their own folders. Like the reindexing tests, these must be run in stages with manual commands between them since pgtap cannot handle distinct commits within a single test run.

As of version 4.4.0, SECURITY DEFINER is no longer in use. The following tests can be used for non-native partitioning to ensure this is working by passing the -U option to pg_prove with a role that has the necessary privileges:

    pg_prove -ovf -U partman -d mydatabase test/test-time-nonsuperuser.sql
    pg_prove -ovf -U partman -d mydatabase test/test-id-nonsuperuser.sql

For native partitioning, use the test/test_native/test_nonsuperuser tests and read the instructions contained in them


