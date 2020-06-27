[![Build Status](https://travis-ci.com/bitnine-oss/agensgraph-ext.svg?branch=master)](https://travis-ci.com/bitnine-oss/agensgraph-ext)

Apache AGE(Incubating)
==========

Note: AgensGraph-Extension was renamed to Apache AGE based on Apache requirements since we donated this project to Apache Software Foundation. 

Apache AGE is an extension of PostgreSQL that provides an implemtation of the [openCypher](https://www.opencypher.org/) query language.

This project is currently in alpha stage.

The initial goal is as follows.

* Support `CREATE` clause with a vertex
* Support `MATCH` clause with a vertex

Installation
============

Requirements
------------

Apache AGE is an extension of PostgreSQL, its build system is based on Postgres' [`Extension Building Infrastructure`](https://www.postgresql.org/docs/11/extend-pgxs.html). Therefore, **pg_config** for the PostgreSQL installation is used to build it.

Apache AGE 0.1.0 supports PostgreSQL 11.

Installation Procedure
----------------------

The build process will attempt to use the first path in the PATH environment variable when installing AGE. If the pg_config path if located there, run the following command in the source code directory of Apache AGE to build and install the extension.

    $ make install

If the path to your Postgres installation is not in the PATH variable, add the path in the arguements:

    $ make PG_CONFIG=/path/to/postgres/bin/pg_config install

Since Apache AGE will be installed in the directory of the PostgreSQL installation, proper permissions on the directory are required.

Run the following statements in ``psql`` to create and load age in PostgreSQL.

    =# CREATE EXTENSION age; -- run this statement only once
    CREATE EXTENSION
    =# LOAD 'age';
    LOAD
    =# SET search_path = ag_catalog, "$user", public;
    SET



How To Contribute 
============

There are multiple ways you can contribute to the project.And help is always welcome! All details can be found on the contributing page. Keep reading for a quick overview!

RoadmapApacheCommunityDocumentationDownload
AGE Contribution Instructions
AGE is a multi-model database that enables graph and relational models built on PostgreSQL.

AGE mainly offers a multimodel database.
Eliminates the migration efforts while having relational and graph models.
It is based on the powerful PostgreSQL RDBMS, it is very robust and fully-featured.
It optimized for handling complex connected graph data and provides plenty of powerful database features.
leverages the rich eco-systems of PostgreSQL and can be extended with many outstanding external modules.

How to contribute
There are multiple ways you can contribute to the project.And help is always welcome! All details can be found on the contributing page. Keep reading for a quick overview!

Becoming a Commiter
----------------------

If you are interested in the project and looking for ways to help, consult the list of tasks in JIRA, or ask the mailing list.

Contributing by Helping Other Users
----------------------

A great way to contribute to AGE is to help answer user questions on the mailing list or on StackOverflow. There are always many new AGE users; taking a few minutes to help answer a question is a very valuable community service.

Contributors should subscribe to this list and follow it in order to keep up to date on what’s happening in AGE. Answering questions is an excellent and visible way to help the community, which also demonstrates your expertise.

See the Mailing Lists guide for guidelines about how to effectively participate in discussions on the mailing list, as well as forums like StackOverflow.

Contributing by Reviewing Changes
----------------------

Changes to AGE source code are proposed, reviewed and committed via Github pull requests (described in Insturction Link). Anyone can view and comment on active changes here. Reviewing others’ changes is a good way to learn how the change process works and gain exposure to activity in various parts of the code. You can help by reviewing the changes and asking questions or pointing out issues – as simple as typos or small issues of style.

Contributing Documentation Changes
----------------------

To propose a change to release documentation (that is, docs that appear under https://AGE.apache.org/instruction/), edit the Markdown source files in AGE’s docs/ directory on GitHub, whose README file shows how to build the documentation locally to test your changes. The process to propose a doc change is otherwise the same as the process for proposing code changes below.

Contributing Bug Reports
----------------------

Bug reports are accompanied by a proposed code change to fix the bug. This isn’t always possible, as those who discover a bug may not have the experience to fix it. A bug may be reported by creating a JIRA but without creating a pull request.

Bug reports are only useful however if they include enough information to understand, isolate and ideally reproduce the bug. Simply encountering an error does not mean a bug should be reported; as below, search JIRA and search and inquire on the AGE user / dev mailing lists first. Unreproducible bugs, or simple error reports, may be closed.

Contributing to JIRA Maintenance
----------------------

Most contributors are able to directly resolve JIRAs. Use judgment in determining whether you are quite confident the issue should be resolved, although changes can be easily undone.

earch JIRA for existing issues: New JIRA link here Type age [search terms] at the top right search box. If a logically similar issue already exists, then contribute to the discussion on the existing JIRA ticket and pull request, instead of creating a new ticket and PR.

