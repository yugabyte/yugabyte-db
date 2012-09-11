INSTALLATION
-----------
Still in early development, so not actually in extension format yet. Run install.sql located in root directory with psql from the pg_partman top level folder.

    cd pg_partman
    psql < install.sql

EXAMPLE
-------

First create a parent table with an appropriate column type for the partitioning you will do. Here's one with a columns that can be used for either

    CREATE schema test;
    CREATE TABLE test.part_test (col1 serial, col2 text, col3 timestamptz DEFAULT now());

If you're looking to do time-based partitioning, and will only be inserting new data, time-static is an appropriate choice. Just run the create_parent() function with the appropriate parameters

    SELECT part.create_parent('test.part_test', 'col3', 'time-static', 'daily');

This will turn your table into a parent table and premake 3 future partitions. To make new partitions for time-based partitioning, run the run_maintenance() function. Ideally, you'd run this as a cronjob to keep new partitions premade in preparation of new data.

This should be enough to get you started. Please see the partman.md file in the doc folder for more information on the types of partitioning supported and what the columns in the create_parent() function mean. 

LICENSE AND COPYRIGHT
-------------------

PG Partition Manager (pg_partman) is released under the PostgreSQL License, a liberal Open Source license, similar to the BSD or MIT licenses.

Copyright (c) 2012 OmniTI, Inc.

Permission to use, copy, modify, and distribute this software and its documentation for any purpose, without fee, and without a written agreement is hereby granted, provided that the above copyright notice and this paragraph and the following two paragraphs appear in all copies.

IN NO EVENT SHALL THE AUTHOR BE LIABLE TO ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF THE AUTHOR HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

THE AUTHOR SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS" BASIS, AND THE AUTHOR HAS NO OBLIGATIONS TO PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
