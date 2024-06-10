[![Build Status](https://travis-ci.org/aggregateknowledge/postgresql-hll.svg?branch=master)](https://travis-ci.org/aggregateknowledge/postgresql-hll)

Overview
========

This Postgres module introduces a new data type `hll` which is a [HyperLogLog](https://research.neustar.biz/2012/10/25/sketch-of-the-day-hyperloglog-cornerstone-of-a-big-data-infrastructure/) data structure. HyperLogLog is a **fixed-size**, set-like structure used for distinct value counting with tunable precision. For example, in 1280 bytes `hll` can estimate the count of tens of billions of distinct values with only a few percent error.

In addition to the algorithm proposed in the [original paper](http://algo.inria.fr/flajolet/Publications/FlFuGaMe07.pdf), this implementation is augmented to improve its accuracy and memory use without sacrificing much speed. See below for more details.

This `postgresql-hll` extension was originally developed by the Science team from Aggregate Knowledge, now a part of [Neustar](https://research.neustar.biz). Please see the [acknowledgements](#acknowledgements) section below for details about its contributors. 

Algorithms
----------

A `hll` is a combination of different set/distinct-value-counting algorithms that can be thought of as a hierarchy, along with rules for moving up that hierarchy. In order to distinguish between said algorithms, we have given them names:

### `EMPTY` ###
A constant value that denotes the empty set.

### `EXPLICIT` ###
An explicit, unique, sorted list of integers in the set, which is maintained up to a fixed cardinality.

### `SPARSE` ###
A 'lazy', map-based implementation of HyperLogLog, a probabilistic set data structure. Only stores the indices and values of non-zero registers in a map, until the number of non-zero registers exceeds a fixed cardinality.

### `FULL` ###
A fully-materialized, list-based implementation of HyperLogLog. Explicitly stores the value of every register in a list ordered by register index.

Motivation
----------

Our motivation for augmenting the original HLL algorithm went something like this:

* Naively, a HLL takes `regwidth * 2^log2m` bits to store.
* In typical usage, `log2m = 11` and `regwidth = 5`, it requires 10,240 bits or 1,280 bytes.
* That's a lot of bytes!

The first addition to the original HLL algorithm came from realizing that 1,280 bytes is the size of 160 64-bit integers. So, if we wanted more accuracy at low cardinalities, we could just keep an explicit set of the inputs as a sorted list of 64-bit integers until we hit the 161st distinct value. This would give us the true representation of the distinct values in the stream while requiring the same amount of memory. (This is the `EXPLICIT` algorithm.)

The second came from the realization that we didn't need to store registers whose value was zero. We could simply represent the set of registers that had non-zero values as a map from index to values. This map is stored as a list of index-value pairs that are bit-packed "short words" of length `log2m + regwidth`. (This is the `SPARSE` algorithm.)

Combining these two augmentations, we get a "promotion hierarchy" that allows the algorithm to be tuned for better accuracy, memory, or performance.

Initializing and storing a new `hll` object will simply allocate a small sentinel value symbolizing the empty set (`EMPTY`). When you add the first few values, a sorted list of unique integers is stored in an `EXPLICIT` set. When you wish to cease trading off accuracy for memory, the values in the sorted list are "promoted" to a `SPARSE` map-based HyperLogLog structure. Finally, when there are enough registers, the map-based HLL will be converted to a bit-packed `FULL` HLL structure.

Empirically, the insertion rate of `EMPTY`, `EXPLICIT`, and `SPARSE` representations is measured in 200k/s - 300k/s range, while the throughput of the `FULL` representation is in the millions of inserts per second on relatively new hardware ('10 Xeon).

Naturally, the cardinality estimates of the `EMPTY` and `EXPLICIT` representations is exact, while the `SPARSE` and `FULL` representations' accuracies are governed by the guarantees provided by the original HLL algorithm.

* * * * * * * * * * * * * * * * * * * * * * * * *

Usage
=====

"Hello World"
-------------

        --- Make a dummy table
        CREATE TABLE helloworld (
                id              integer,
                set     hll
        );

        --- Insert an empty HLL
        INSERT INTO helloworld(id, set) VALUES (1, hll_empty());

        --- Add a hashed integer to the HLL
        UPDATE helloworld SET set = hll_add(set, hll_hash_integer(12345)) WHERE id = 1;

        --- Or add a hashed string to the HLL
        UPDATE helloworld SET set = hll_add(set, hll_hash_text('hello world')) WHERE id = 1;

        --- Get the cardinality of the HLL
        SELECT hll_cardinality(set) FROM helloworld WHERE id = 1;

Now with the silly stuff out of the way, here's a more realistic use case.

Data Warehouse Use Case
-----------------------

Let's assume I've got a fact table that records users' visits to my site, what they did, and where they came from. It's got hundreds of millions of rows. Table scans take minutes (or at least lots and lots of seconds.)

    CREATE TABLE facts (
        date            date,
        user_id         integer,
        activity_type   smallint,
        referrer        varchar(255)
    );

I'd really like a quick (milliseconds) idea of how many unique users are visiting per day for my dashboard. No problem, let's set up an aggregate table:

    -- Create the destination table
    CREATE TABLE daily_uniques (
        date            date UNIQUE,
        users           hll
    );

    -- Fill it with the aggregated unique statistics
    INSERT INTO daily_uniques(date, users)
        SELECT date, hll_add_agg(hll_hash_integer(user_id))
        FROM facts
        GROUP BY 1;

We're first hashing the `user_id`, then aggregating those hashed values into one `hll` per day. Now we can ask for the cardinality of the `hll` for each day:

    SELECT date, hll_cardinality(users) FROM daily_uniques;

You're probably thinking, "But I could have done this with `COUNT DISTINCT`!" And you're right, you could have. But then you only ever answer a single question: "How many unique users did I see each day?"

What if you wanted to this week's uniques?

    SELECT hll_cardinality(hll_union_agg(users)) FROM daily_uniques WHERE date >= '2012-01-02'::date AND date <= '2012-01-08'::date;

Or the monthly uniques for this year?

    SELECT EXTRACT(MONTH FROM date) AS month, hll_cardinality(hll_union_agg(users))
    FROM daily_uniques
    WHERE date >= '2012-01-01' AND
          date <  '2013-01-01'
    GROUP BY 1;

Or how about a sliding window of uniques over the past 6 days?

    SELECT date, #hll_union_agg(users) OVER seven_days
    FROM daily_uniques
    WINDOW seven_days AS (ORDER BY date ASC ROWS 6 PRECEDING);

Or the number of uniques you saw yesterday that you didn't see today?

    SELECT date, (#hll_union_agg(users) OVER two_days) - #users AS lost_uniques
    FROM daily_uniques
    WINDOW two_days AS (ORDER BY date ASC ROWS 1 PRECEDING);

These are just a few examples of the types of queries that would return in milliseconds in an `hll` world from a single aggregate, but would require either completely separate pre-built aggregates or self-joins or `generate_series` trickery in a `COUNT DISTINCT` world.

Operators
---------

We've added a few operators to make using `hll`s less cumbersome/verbose. They're simple aliases for the most commonly used functions.

<table>
    <thead>
        <th>Function</th>
        <th>Operator</th>
        <th>Example</th>
    </thead>
    <tr>
        <td><code>hll_add</code></td>
        <td><code>||</code></td>
        <td>
            <code>hll_add(users, hll_hash_integer(123))</code><br/>
            <strong>or</strong><br/>
            <code>users || hll_hash_integer(123)</code><br/>
            <strong>or</strong><br/>
            <code>hll_hash_integer(123) || users</code>
        </td>
    </tr>
    <tr>
        <td><code>hll_cardinality</code></td>
        <td><code>#</code></td>
        <td>
            <code>hll_cardinality(users)</code><br/>
            <strong>or</strong><br/>
            <code>#users</code>
        </td>
    </tr>
    <tr>
        <td><code>hll_union</code></td>
        <td><code>||</code></td>
        <td>
            <code>hll_union(male_users, female_users)</code><br/>
            <strong>or</strong><br/>
            <code>male_users || female_users</code><br/>
            <strong>or</strong><br/>
            <code>female_users || male_users</code>
        </td>
    </tr>
</table>

Hashing
-------

You'll notice that all the calls to `hll_add` or `||` involve wrapping the input value in a `hll_hash_[type]` call; it's absolutely crucial that you hash your input values to `hll` structures. For more on this, see the section below titled 'The Importance of Hashing'.

The hashing functions we've made available are listed below:

<table>
    <thead>
        <th>Function</th>
        <th>Input</th>
        <th>Example</th>
    </thead>
    <tr>
        <td><code>hll_hash_boolean</code></td>
        <td><code>boolean</code></td>
        <td>
            <code>hll_hash_boolean(TRUE)</code><br/>
            <strong>or</strong><br/>
            <code>hll_hash_boolean(TRUE, 123/*hash seed*/)</code>
        </td>
    </tr>
    <tr>
        <td><code>hll_hash_smallint</code></td>
        <td><code>smallint</code></td>
        <td>
            <code>hll_hash_smallint(4)</code><br/>
            <strong>or</strong><br/>
            <code>hll_hash_smallint(4, 123/*hash seed*/)</code>
        </td>
    </tr>
    <tr>
        <td><code>hll_hash_integer</code></td>
        <td><code>integer</code></td>
        <td>
            <code>hll_hash_integer(21474836)</code><br/>
            <strong>or</strong><br/>
            <code>hll_hash_integer(21474836, 123/*hash seed*/)</code>
        </td>
    </tr>
    <tr>
        <td><code>hll_hash_bigint</code></td>
        <td><code>bigint</code></td>
        <td>
            <code>hll_hash_bigint(223372036854775808)</code><br/>
            <strong>or</strong><br/>
            <code>hll_hash_bigint(223372036854775808, 123/*hash seed*/)</code>
        </td>
    </tr>
    <tr>
        <td><code>hll_hash_bytea</code></td>
        <td><code>bytea</code></td>
        <td>
            <code>hll_hash_bytea(E'\\xDEADBEEF')</code><br/>
            <strong>or</strong><br/>
            <code>hll_hash_bytea(E'\\xDEADBEEF', 123/*hash seed*/)</code>
        </td>
    </tr>
    <tr>
        <td><code>hll_hash_text</code></td>
        <td><code>text</code></td>
        <td>
            <code>hll_hash_text('foobar')</code><br/>
            <strong>or</strong><br/>
            <code>hll_hash_text('foobar', 123/*hash seed*/)</code>
        </td>
    </tr>
    <tr>
        <td><code>hll_hash_any</code></td>
        <td><code>any</code></td>
        <td>
            <code>hll_hash_any(anyval)</code><br/>
            <strong>or</strong><br/>
            <code>hll_hash_any(anyval, 123/*hash seed*/)</code>
        </td>
    </tr>
</table>

**NOTE:** `hll_hash_any` dynamically dispatches to the appropriate type-specific function, which makes it slower than the type-specific ones it wraps. Use it only when the input type is not known beforehand.

So what if you don't want to hash your input?

    postgres=# select 1234 || hll_empty();
    ERROR:  operator does not exist: integer || hll
    LINE 1: select 1234 || hll_empty();
                        ^
    HINT:  No operator matches the given name and argument type(s). You might need to add explicit type casts.

Not pretty. Since hashing is such a crucial part of the accuracy of HyperLogLog, we decided to "enforce" this at a type level. You can only add `hll_hashval` typed things to a `hll`, which is what the `hll_hash_[type]` functions return. You can simply cast **integer** values to `hll_hashval` to add them without hashing, like so:

    postgres=# select 1234::hll_hashval || hll_empty();
             ?column?
    --------------------------
     \x128c4900000000000004d2
    (1 row)

Aggregate functions
-------------------

If you want to create a `hll` from a table or result set, use `hll_add_agg`. The naming here isn't particularly creative: it's an **agg**regate function that **add**s the values to an empty `hll`.

    SELECT date, hll_add_agg(hll_hash_integer(user_id))
    FROM facts
    GROUP BY 1;

The above example will give you a `hll` for each date that contains each day's users.

If you want to summarize a list of `hll`s that you already have stored into a single `hll`, use `hll_union_agg`. Again: it's an **agg**regate function that **union**s the values into an empty `hll`.

    SELECT EXTRACT(MONTH FROM date), hll_cardinality(hll_union_agg(users))
    FROM daily_uniques
    GROUP BY 1;

Sliding windows are another prime example of the power of `hll`s. Doing sliding window unique counting typically involves some `generate_series` trickery, but it's quite simple with the `hll`s you've already computed for your roll-ups.

    SELECT date, #hll_union_agg(users) OVER seven_days
    FROM daily_uniques
    WINDOW seven_days AS (ORDER BY date ASC ROWS 6 PRECEDING);

Explanation of Parameters and Tuning
------------------------------------

### `log2m` ###

The log-base-2 of the number of registers used in the HyperLogLog algorithm. Must be at least 4 and at most 31. This parameter tunes the accuracy of the HyperLogLog structure. The relative error is given by the expression ±1.04/√(2<sup>log2m</sup>). Note that increasing `log2m` by 1 doubles the required storage for the `hll`.

### `regwidth` ###

The number of bits used per register in the HyperLogLog algorithm. Must be at least 1 and at most 8. This parameter, in conjunction with `log2m`, tunes the maximum cardinality of the set whose cardinality can be estimated. For clarity, we've provided a table of `regwidth`s and `log2m`s and the approximate maximum cardinality that can be estimated with those parameters. (The size of the resulting structure is provided as well.)

<table>
    <thead>
        <th><code>logm2</code></th><th><code>regwidth=1</code></th><th><code>regwidth=2</code></th><th><code>regwidth=3</code></th><th><code>regwidth=4</code></th><th><code>regwidth=5</code></th><th><code>regwidth=6</code></th>
    </thead>
    <tbody>
<tr><td>10</td><td>7.4e+02 &nbsp;&nbsp;<em><sub>128B</sub></em></td><td>3.0e+03 &nbsp;&nbsp;<em><sub>256B</sub></em></td><td>4.7e+04 &nbsp;&nbsp;<em><sub>384B</sub></em></td><td>1.2e+07 &nbsp;&nbsp;<em><sub>512B</sub></em></td><td>7.9e+11 &nbsp;&nbsp;<em><sub>640B</sub></em></td><td>3.4e+21 &nbsp;&nbsp;<em><sub>768B</sub></em></td></tr>
<tr><td>11</td><td>1.5e+03 &nbsp;&nbsp;<em><sub>256B</sub></em></td><td>5.9e+03 &nbsp;&nbsp;<em><sub>512B</sub></em></td><td>9.5e+04 &nbsp;&nbsp;<em><sub>768B</sub></em></td><td>2.4e+07 &nbsp;&nbsp;<em><sub>1.0KB</sub></em></td><td>1.6e+12 &nbsp;&nbsp;<em><sub>1.2KB</sub></em></td><td>6.8e+21 &nbsp;&nbsp;<em><sub>1.5KB</sub></em></td></tr>
<tr><td>12</td><td>3.0e+03 &nbsp;&nbsp;<em><sub>512B</sub></em></td><td>1.2e+04 &nbsp;&nbsp;<em><sub>1.0KB</sub></em></td><td>1.9e+05 &nbsp;&nbsp;<em><sub>1.5KB</sub></em></td><td>4.8e+07 &nbsp;&nbsp;<em><sub>2.0KB</sub></em></td><td>3.2e+12 &nbsp;&nbsp;<em><sub>2.5KB</sub></em></td><td>1.4e+22 &nbsp;&nbsp;<em><sub>3KB</sub></em></td></tr>
<tr><td>13</td><td>5.9e+03 &nbsp;&nbsp;<em><sub>1.0KB</sub></em></td><td>2.4e+04 &nbsp;&nbsp;<em><sub>2.0KB</sub></em></td><td>3.8e+05 &nbsp;&nbsp;<em><sub>3KB</sub></em></td><td>9.7e+07 &nbsp;&nbsp;<em><sub>4KB</sub></em></td><td>6.3e+12 &nbsp;&nbsp;<em><sub>5KB</sub></em></td><td>2.7e+22 &nbsp;&nbsp;<em><sub>6KB</sub></em></td></tr>
<tr><td>14</td><td>1.2e+04 &nbsp;&nbsp;<em><sub>2.0KB</sub></em></td><td>4.7e+04 &nbsp;&nbsp;<em><sub>4KB</sub></em></td><td>7.6e+05 &nbsp;&nbsp;<em><sub>6KB</sub></em></td><td>1.9e+08 &nbsp;&nbsp;<em><sub>8KB</sub></em></td><td>1.3e+13 &nbsp;&nbsp;<em><sub>10KB</sub></em></td><td>5.4e+22 &nbsp;&nbsp;<em><sub>12KB</sub></em></td></tr>
<tr><td>15</td><td>2.4e+04 &nbsp;&nbsp;<em><sub>4KB</sub></em></td><td>9.5e+04 &nbsp;&nbsp;<em><sub>8KB</sub></em></td><td>1.5e+06 &nbsp;&nbsp;<em><sub>12KB</sub></em></td><td>3.9e+08 &nbsp;&nbsp;<em><sub>16KB</sub></em></td><td>2.5e+13 &nbsp;&nbsp;<em><sub>20KB</sub></em></td><td>1.1e+23 &nbsp;&nbsp;<em><sub>24KB</sub></em></td></tr>
<tr><td>16</td><td>4.7e+04 &nbsp;&nbsp;<em><sub>8KB</sub></em></td><td>1.9e+05 &nbsp;&nbsp;<em><sub>16KB</sub></em></td><td>3.0e+06 &nbsp;&nbsp;<em><sub>24KB</sub></em></td><td>7.7e+08 &nbsp;&nbsp;<em><sub>32KB</sub></em></td><td>5.1e+13 &nbsp;&nbsp;<em><sub>40KB</sub></em></td><td>2.2e+23 &nbsp;&nbsp;<em><sub>48KB</sub></em></td></tr>
<tr><td>17</td><td>9.5e+04 &nbsp;&nbsp;<em><sub>16KB</sub></em></td><td>3.8e+05 &nbsp;&nbsp;<em><sub>32KB</sub></em></td><td>6.0e+06 &nbsp;&nbsp;<em><sub>48KB</sub></em></td><td>1.5e+09 &nbsp;&nbsp;<em><sub>64KB</sub></em></td><td>1.0e+14 &nbsp;&nbsp;<em><sub>80KB</sub></em></td><td>4.4e+23 &nbsp;&nbsp;<em><sub>96KB</sub></em></td></tr>
    </tbody>
</table>

### `expthresh` ###

Tunes when the `EXPLICIT` to `SPARSE` promotion occurs, based on the set's cardinality. It is also possible to turn off the use of the `EXPLICIT` representation entirely. If the `EXPLICIT` representation is turned off, the `EMPTY` set is promoted directly to `SPARSE`. Must be -1, 0, or 1-18 inclusive.

<table>
    <thead><th><code>expthresh</code> value</th><th>Meaning</th></thead>
    <tr><td>-1</td><td>Promote at whatever cutoff makes sense for optimal memory usage. ('auto' mode)</td></tr>
    <tr><td>0</td><td>Skip <code>EXPLICIT</code> representation in hierarchy.</td></tr>
    <tr><td>1-18</td><td>Promote at 2<sup>expthresh - 1</sup> cardinality</td></tr>
</table>

You can choose the `EXPLICIT` cutoff such that it will end up taking more memory than a `FULL` `hll` representation. This is allowed for those cases where perfect precision and accuracy are required up through some pre-set cardinality range, after which estimates of the cardinality are sufficient.

**NOTE:** The restriction of `expthresh` to a maximum value of 18 (for the third case in the table above) is an implementation tradeoff between performance and general appeal. If you want access to higher `expthresh` values, let us know in the Issues section and we'll see what we can do.

### `sparseon` ###

Enables or disables the `SPARSE` representation. If both the `EXPLICIT` and `SPARSE` representations are disabled, an `EMPTY` set will be promoted directly to a `FULL` set. If `SPARSE` is enabled, the promotion from `SPARSE` to `FULL` will occur when the internal `SPARSE` representation's memory footprint would exceed that of the `FULL` version. Must be either either `0` (zero) or `1` (one). Zero means disabled, one is enabled.

Defaults
--------

In all the examples above, the type `hll` has been used without adornment. This is a shortcut. In reality, the type can have up to 4 arguments. The defaults are shown as well.

    hll(log2m=11, regwidth=5, expthresh=-1, sparseon=1)

You can provide any prefix of the full list of arguments. The named arguments are the same as those mentioned in the 'Explanation of Parameters' section, above. If you'd like to change these (they're hardcoded in the source) look in `hll.c` for `DEFAULT_LOG2M` and that should get you there pretty quickly.

Debugging
---------

`hll_print` is your friend! It will show you all the parameters of the `hll` as well as nicely-formatted representation of the contents.

* * * * * * * * * * * * * * * * * * * * * * * * *

Compatibility
=============

This module has been tested on:

* **Postgres 9.4, 9.5, 9.6, 10, 11, 12, 13, 14**

If you end up needing to change something to get this running on another system, send us the diff and we'll try to work it in!

Note: At the moment postgresql-hll does not work with 32bit systems.

Build
=====

## With `rpmbuild` ##

Specify versions:

    export VER=2.17
    export PGSHRT=11

Make sure `Makefile` points to the correct `pg_config` for the specified version, since `rpmbuild` doesn't respect env variables:

    PG_CONFIG = /usr/pgsql-11/bin/pg_config

Create a tarball from the source tree:

    tar cvfz postgresql${PGSHRT}-hll-${VER}.tar.gz postgresql-hll \
        --transform="s/postgresql-hll/postgresql${PGSHRT}-hll/g"

Execute rpmbuild:

    rpmbuild -tb postgresql${PGSHRT}-hll-${VER}.tar.gz

Install RPM:

    rpm -Uv rpmbuild/RPMS/x86_64/postgresql11-hll-2.16.x86_64.rpm

And if you want the debugging build:

    rpm -Uv rpmbuild/RPMS/x86_64/postgresql11-hll-debuginfo-2.17.x86_64.rpm


## From source ##

If you aren't using the `pg_config` on your path (or don't have it on your path), specify the correct one to build against:

        PG_CONFIG=/usr/pgsql-9.11/bin/pg_config make

Or to build with what's on your path, just:

        make

If you wish to build with an alternate C/C++ compiler, say `gcc`, then you can specify it like so:

        make CC=gcc CXX=gcc

(This may be useful if an older `clang` is the default compiler.)

Or for the debug build:

        DEBUG=1 make

Then install:

        sudo make install

### Troubleshooting source install ###

You need postgresql's libraries and headers for C lang available in order to
install from source. If you don't have these, you may encounter `No such file
or directory` errors and will not be able to run the `make` step above. This
step may depend on your OS but to install them on Debian variants use you may
use:

        sudo apt-get install postgresql-server-dev-<YOUR_VERSION>

Install
=======

After you've built and installed the artifacts, fire up `psql`:

        postgres=# CREATE EXTENSION hll;
        CREATE EXTENSION

And then just verify it's there:

        postgres=# \dx
                            List of installed extensions
          Name   | Version |   Schema   |            Description
        ---------+---------+------------+-----------------------------------
         hll     | 2.17    | public     | type for storing hyperloglog data
         plpgsql | 1.0     | pg_catalog | PL/pgSQL procedural language
        (2 rows)

Tests
=====

Start a PostgreSQL server running in default port:

    pg_ctl -D data -l logfile -c start
    initdb -D data

Run the tests:

    make installcheck

* * * * * * * * * * * * * * * * * * * * * * * * *

The Importance of Hashing
=========================

In brief, it is absolutely crucial to hash inputs to the `hll`. A close approximation of uniform randomness in the inputs ensures that the error guarantees laid out in the original paper hold. In fact, the [canonical C++ implementation](http://code.google.com/p/smhasher/) of MurmurHash 3 is provided in this module to facilitate this input requirement. We've empirically determined that MurmurHash 3 is an excellent and fast hash function to use in conjunction with the `hll` module.

The seed to the hash call must remain constant for all inputs to a given `hll`.  Similarly, if you plan to compute the union of two `hll`s, the input values must have been hashed using the same seed.

For a good overview of the importance of hashing and hash functions when using probabilistic algorithms as well as an analysis of MurmurHash 3, see these four blog posts:

* [K-Minimum Values: Sketching Error, Hash Functions, and You](http://blog.aggregateknowledge.com/2012/08/20/k-minimum-values-sketching-error-hash-functions-and-you/)
* [Choosing a Good Hash Function, Part 1](http://blog.aggregateknowledge.com/2011/12/05/choosing-a-good-hash-function-part-1/)
* [Choosing a Good Hash Function, Part 2](http://blog.aggregateknowledge.com/2011/12/29/choosing-a-good-hash-function-part-2/)
* [Choosing a Good Hash Function, Part 3](http://blog.aggregateknowledge.com/2012/02/02/choosing-a-good-hash-function-part-3/)

On Unions and Intersections
===========================

`hll`s have the useful property that the union of any number of `hll`s is equal to the `hll` that would have been populated by playing back all inputs to those N `hll`s into a single `hll`. Colloquially, we say that `hll`s have "lossless" unions because the same cardinality error guarantees that apply to a single `hll` apply to a union of `hll`s. This property combined with Postgres' aggregation functions (sliding window and so on) can power some pretty impressive analytics, like the number of unique visitors in a 30-day sliding window over the course of a year. See the `hll_union_agg` and `hll_union` functions.

Using the [inclusion-exclusion principle](http://en.wikipedia.org/wiki/Inclusion%E2%80%93exclusion_principle) and the union function, you can also estimate the intersection of sets represented by `hll`s. Note, however, that error is proportional to the union of the two `hll`s, while the result can be significantly smaller than the union, leading to disproportionately large error relative to the actual intersection cardinality. For instance, if one `hll` has a cardinality of 1 billion, while the other has a cardinality of 10 million, with an overlap of 5 million, the intersection cardinality can easily be dwarfed by even a 1% error estimate in the larger `hll`s cardinality.

For more information on `hll` intersections, see [this blog post](https://research.neustar.biz/2012/12/17/hll-intersections-2/).

Storage formats
===============

`hll`s are stored in the database as byte arrays, which are packed according to the [storage specification, v1.0.0](https://github.com/aggregateknowledge/hll-storage-spec/blob/v1.0.0/STORAGE.md).

It is a pretty trivial task to export these to and from Postgres and other applications by implementing a serializer/deserializer. We have provided several packages that provide such tools:

* [java-hll](https://github.com/aggregateknowledge/java-hll)
* [js-hll](https://github.com/aggregateknowledge/js-hll)
* [go-hll](https://github.com/segmentio/go-hll)

Acknowledgements
================

Original developers of `postgresql-hll` are [Ken Sedgwick](https://github.com/ksedgwic), Timon Karnezos, and [Rob Grzywinski](https://github.com/rgrzywinski).
