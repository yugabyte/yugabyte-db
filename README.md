pgTAP 0.25.0
============

[pgTAP](http://pgtap.org) is a unit testing framework for PostgreSQL written
in PL/pgSQL and PL/SQL. It includes a comprehensive collection of
[TAP](http://testanything.org)-emitting assertion functions, as well as the
ability to integrate with other TAP-emitting test frameworks. It can also be
used in the xUnit testing style. For detailed documentation, see the
documentation in `doc/pgtap.md` or
[online](http://pgtap.org/documentation.html "Complete pgTAP Documentation").

To build it, just do this:

    make
    make installcheck
    make install

If you encounter an error such as:

    "Makefile", line 8: Need an operator

You need to use GNU make, which may well be installed on your system as
`gmake`:

    gmake
    gmake install
    gmake installcheck

If you encounter an error such as:

    make: pg_config: Command not found

Or:

    Makefile:52: *** pgTAP requires PostgreSQL 8.0 or later. This is .  Stop.

Be sure that you have `pg_config` installed and in your path. If you used a
package management system such as RPM to install PostgreSQL, be sure that the
`-devel` package is also installed. If necessary tell the build process where
to find it:

    env PG_CONFIG=/path/to/pg_config make && make install && make installcheck

And finally, if all that fails (and if you're on PostgreSQL 8.1 or lower, it
likely will), copy the entire distribution directory to the `contrib/`
subdirectory of the PostgreSQL source tree and try it there without
`pg_config`:

    env NO_PGXS=1 make && make install && make installcheck

If you encounter an error such as:

    ERROR:  must be owner of database regression

You need to run the test suite using a super user, such as the default
"postgres" super user:

    make installcheck PGUSER=postgres

If you want to schema-qualify pgTAP (that is, install all of its functions
into their own schema), set the `$TAPSCHEMA` variable to the name of the
schema you'd like to be created, for example:

    make TAPSCHEMA=tap
    make install
    make installcheck

Dependencies
------------

pgTAP requires PostgreSQL 8.0 or higher, with 8.4 or higher recommended for
full use of its API. It also requires PL/pgSQL.

Copyright and License
---------------------

Copyright (c) 2008-2011 Kineticode, Inc. Some rights reserved.

Permission to use, copy, modify, and distribute this software and its
documentation for any purpose, without fee, and without a written agreement is
hereby granted, provided that the above copyright notice and this paragraph
and the following two paragraphs appear in all copies.

IN NO EVENT SHALL KINETICODE BE LIABLE TO ANY PARTY FOR DIRECT, INDIRECT,
SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST PROFITS, ARISING
OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF KINETICODE HAS
BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

KINETICODE SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS" BASIS, AND
KINETICODE HAS NO OBLIGATIONS TO PROVIDE MAINTENANCE, SUPPORT, UPDATES,
ENHANCEMENTS, OR MODIFICATIONS.
