PG Partition Manager
====================

pg_partman is an extension to create and manage both time-based and serial-based table partition sets. Sub-partitoning is supported. Child table & trigger function creation is all managed by the extension itself. Tables with existing data can also have their data partitioned in easily managed smaller batches. Optional retention policy can automatically drop partitions no longer needed.

INSTALLATION
------------
Recommended: pg_jobmon (>=v1.1.2). PG Job Monitor will automatically be used if it is installed.  
https://github.com/omniti-labs/pg_jobmon

In directory where you downloaded pg_partman to run

    make
    make install

Log into PostgreSQL and run the following commands. Schema can be whatever you wish, but it cannot be changed after installation.

    CREATE SCHEMA partman;
    CREATE EXTENSION pg_partman SCHEMA partman;

Functions must either be run as a superuser or you can set the ownership of the extension functions to a superuser role and they will also work (SECURITY DEFINER is set).

UPGRADE
-------

Make sure all the upgrade scripts for the version you have installed up to the most recent version are in the $/share/extension folder.  Running make install should take care of this.

    ALTER EXTENSION pg_partman UPDATE TO '<latest version>';

EXAMPLE
-------

First create a parent table with an appropriate column type for the partitioning type you will do. Apply all defaults, indexes, constraints, privileges & ownership to the parent table and they will be inherited to newly created child tables automatically (not already existing partitions, see docs for how to fix that). Here's one with columns that can be used for either

    CREATE schema test;
    CREATE TABLE test.part_test (col1 serial, col2 text, col3 timestamptz NOT NULL DEFAULT now());

If you're looking to do time-based partitioning, and will only be inserting new data, time-static is an appropriate choice. Just run the create_parent() function with the appropriate parameters

    SELECT partman.create_parent('test.part_test', 'col3', 'time-static', 'daily');

This will turn your table into a parent table and premake 4 future partitions and also make 4 past partitions. To make new partitions for time-based partitioning, use the run_maintenance() function. Ideally, you'd run this as a cronjob to keep new partitions premade in preparation of new data.

This should be enough to get you started. Please see the [pg_partman.md file](doc/pg_partman.md) in the doc folder for more information on the types of partitioning supported and what the parameters in the create_parent() function mean. 


TESTING
-------
This extension can use the pgTAP unit testing suite to evalutate if it is working properly (http://www.pgtap.org).
WARNING: You MUST increase max_locks_per_transaction above the default value of 64. For me, 128 has worked well so far. This is due to the sub-partitioning tests that create/destroy several hundred tables in a single transaction. If you don't do this, you risk a cluster crash when running subpartitioning tests.


LICENSE AND COPYRIGHT
---------------------

PG Partition Manager (pg_partman) is released under the PostgreSQL License, a liberal Open Source license, similar to the BSD or MIT licenses.

Copyright (c) 2015 OmniTI, Inc.

Permission to use, copy, modify, and distribute this software and its documentation for any purpose, without fee, and without a written agreement is hereby granted, provided that the above copyright notice and this paragraph and the following two paragraphs appear in all copies.

IN NO EVENT SHALL THE AUTHOR BE LIABLE TO ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF THE AUTHOR HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

THE AUTHOR SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS" BASIS, AND THE AUTHOR HAS NO OBLIGATIONS TO PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
