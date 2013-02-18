pg_partman Test Suite
=====================

The pgTAP testing suite is used to provide an extensive and easily maintainable set of tests. Please see the pgTAP home page for more details on its installation and use.

http://pgTAP.org/

Tests assume that the required extensions have been installed in the following schemas:

    pg_partman: partman
    pgTAP: tap

If you've installed any of the above extensions in a different schema and would like to run the test suite, simply change the configuration option found at the top of each testing file to match your setup. If you've also installed pg_jobmon, be aware that the logging of the tests cannot be rolled back and any failures will be picked up by the monitoring in the jobmon extension.

    SELECT set_config('search_path','partman, tap',false);
    
Once that's done, it's best to use the **pg_prove** script that pgTAP comes with to run all the tests. I like using the  -o -f -v options to get more useful feedback.

    pg_prove -o -f -v /path/to/partman/test/*.sql

The tests must be run by a superuser since roles & schemas are created & dropped as part of the test. There is a separate test script for each of the partitioning types. The tests are not required to run pg_partman, so if you don't feel safe doing this you don't need to run the tests. But if you are running into problems and report any issues without a clear explanation of what is wrong, I will ask that you run the test suite so you can try and narrow down where the problem may be. You are free to look through to tests to see exactly what they're doing. Everything is run inside a transaction that is rolled back, so it should not change anything (except jobmon logging as mentioned).
