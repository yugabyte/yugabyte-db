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

Preparing to Contribute Code Changes
----------------------

When new contributors want to join the project, they may have a particular change or bug in mind. If you are interested in the project and looking for ways to help, consult the list of tasks in JIRA, or ask the user@age.apache.org mailing list. Code Reviews can take hours or days of a committer’s time. Everyone benefits if contributors focus on changes that are useful, clear, easy to evaluate, and already pass basic checks.

Before proceeding, contributors should evaluate if the proposed change is likely to be relevant, new and actionable:

* Is it clear that code must change? Proposing a JIRA and pull request is appropriate only when a clear problem or change has been identified.

* If simply having trouble using AGE, use the mailing lists first, rather than creating a JIRA ticket or proposing a change.

* When in doubt, email user@age.apache.org first about the possible change
Search the user@age.apache.org and dev@age.apache.org mailing list Archive Link Here for related discussions.

Search JIRA for existing issues: New JIRA link here
Type age [search terms] at the top right search box. If a logically similar issue already exists, then contribute to the discussion on the existing JIRA ticket and pull request, instead of creating a new ticket and PR.

Code Review Process
----------------------

* Make a commit (or multiple commits) on your local branch.
* Create .patch file(s) of the commit(s).
    * Use `git format-patch` command.
* Send the .patch file(s) to the reviewer.
    * The title of the email must be "[Review] [JIRA Ticket Name Here] summary-of-the-issue"
(e.g. [Review] [JIRA Ticket Name] Support changing graph name)
        * If the commit is not for any issues on Jira, omit " [JIRA Ticket Name Here]". OR make a Jira ticket
    *The email body will look like this:
    
                Commit bef50e5d86d45707806f5733695a229f3e295b1a

                [one blank line]

                Description
        
        * The first line is the hash code of the base commit, NOT the commit you've created.
            * This will help reviewers to quickly apply the .patch files.
        * Put proper information to help the reviewer.
    * Attach .patch files.
        * Do NOT rename files. They are named with numbers in order.
        * Do NOT compress them unless the total file size is over 5MB.
* Reply to the last email in the same thread to send a review of it.
    * You can attach some .diff files.
* Reply to the last email in the same thread to send updated patch(es) and opinions.
    * If you rebase commits, state the hash code of the new base commit.
* Repeat 4 and 5.

How to Merge a Pull Request
----------------------

Git Merge Strategy for Apache AGE
Detailed below is our workflow for adding patches that have passed the approval process to the master branch

Single Commit for a Single Task
In this case, the commit will be merged into the master branch with the following process.

1. Change the current working branch to the local master branch by running the following command.
    
        $ git checkout master 

2. Make the local master branch up-to-date by running the following command (or any other commands that result the same.)

        $ git pull

3. Change the current working branch to the local task branch that the commit resides in by running the following command.

        $ git checkout

4. Rebase the local task branch by running the following command.

        $ git rebase master

5. Resolve any conflicts that occur during rebase.

6. Change the current working branch to the local master branch by running the following command.

        $ git checkout master

7. Merge the local task branch into the local master branch by running the following command.

        $ git merge

Multiple Commits for a Single Task
----------------------

Keeping Commit History
Sometimes, having logically separated multiple commits for a single task helps developers to grasp the logical process of the work that had been done for the task. If the commits are merged with fast-forward strategy, the commits will not be grouped together. Therefore, to group the commits, create an explicit merge commit.

In this case, the commits will be merged into the master branch with the same process above except the last step(step 7).

For the last step, the local task branch will be merged into the local master branch with an explicit merge commit by running the following command. If you omit --no-ff option, the command will do fast-forward merge instead.

    $ git merge --no-ff


The above process will result, for example, the following commit history. (This is captured from bitnine/agensgraph-ext.) There is an explicit merge commit, 69f3b32. Each explicit merge commit groups related commits.

Note: There is no commit between an explicit merge commit and the parent commit, which is on the master branch, of the explicit merge commit. This is done by doing rebase before merge.

http://jira.bitnine.net:19090/display/USRND/Git+Merge+Strategy

Setting up GIT remotes
----------------------

https://git-scm.com/book/en/v2/Git-Basics-Working-with-Remotes

Policy on Reporting Bug
----------------------

Bug reports are only useful however if they include enough information to understand, isolate and reproduce the bug. Simply encountering an error does not mean a bug should be reported. A bug logged in JIRA that cannot be reproduced will be closed.

The more context the reporter can give about a bug, the better, such as: how the bug was introduced, by which commit, etc. It assists the committers in the decision process on how far the bug fix should be backported, when the pull request is merged. The pull request to fix the bug should narrow down the problem to the root cause.

Ideally, bug reports are accompanied by a proposed code change to fix the bug. This isn’t always possible, as those who discover a bug may not have the experience to fix it. A bug may be reported by creating a JIRA ticket without creating a pull request.

Data correctness/data loss bugs are very serious. Make sure the corresponding bug report JIRA ticket is labeled as correctness or data-loss. Please send an email to dev@age.apache.org after submitting the bug report, to quickly draw attention to the issue.

Performance issues are classified as bugs. The pull request to fix a performance bug must provide a benchmark to prove the problem is indeed fixed.

Maintaining JIRA issues
----------------------

Inevitably some issues are duplicates, become obsolete, can not be reproduced, could benefit from more detail, etc. It’s useful to help identify these issues and resolve them, either by advancing the discussion or resolving the JIRA ticket. Most contributors are able to directly resolve JIRAs. Use judgment in determining whether you are confident the issue should be resolved. If in doubt, just leave a comment on the JIRA ticket.

When resolving JIRA tickets, please observe the following conventions:

* Resolve as Fixed if there’s a release or code commit that resolved the issue.
    * Set Fix Version(s), if and only if the resolution is Fixeds
    * Set Assignee to the person who contributed the most to its resolution, usually the person who opened the PR that resolved the issue.
* For issues that can’t be reproduced against master as reported, resolve as Cannot Reproduce.
* If the issue is the same as or a subset of another issue, resolved as Duplicate
    * Mark the issue that has less activity or discussion as the duplicate.
    * Link it to the JIRA ticket it duplicates.
* If the issue seems clearly obsolete and applies to issues or components that have changed radically since it was opened, resolve as Not a Problem
* If the issue doesn’t make sense – not actionable – resolve as Invalid.
* If it’s a coherent issue, but there is a clear indication that there is not support or interest in acting on it, then resolve as Won’t Fix.

Code Style Guide
----------------------

For a full list of coding style guidelines, please refer to the style setup in the clang-format.5 in the AGE git repository.

General Rules
NOTE: In some environments, code block does not properly show indentation. To see the correct indentation, copy and paste the code to a text editor.

##### Indentation

Use 4 spaces per indentation level. (no tab character)
    * You can see the same indentation in all environments.
    
##### Breaking long lines and strings

The line length limit is 79 columns, except for strings longer than 79 characters.

##### Placing Braces and Spaces.

All braces are on their own line solely.

If all the bodies of if/else statement contain a singe line, omit braces.

One exception is do statement.

##### Naming
###### Style
Use the underscore name convention for all variables, functions, structs, enums and define macros.

##### Typedefs
Use typedef only for struct and enum. It must not be used for pointer types.

##### Commenting
For multi-line comments, use C style multi-line comments.

For single-line comments, use C++ style single-line comments.

##### Newlines
For newlines, only \n is allowed, not \r\n and \r.

##### Conditions
If a pointer variable (including List *) is used as a condition, which means that it is evaluated as true/false value, use it AS-IS. Do not perform explicit comparison with NULL (or NIL). For negation, put ! before it.


