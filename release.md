pgTAP Release Management
========================

Here are the steps to take to make a release of pgTAP:

*   Review and fix any open bugs in the
    [issue tracker](https://github.com/theory/pgtap/issues).

*   Review and merge any appropriate
    [pull requests](https://github.com/theory/pgtap/pulls).

*   Test on all supported PostgreSQL versions, starting with the latest version
    (10) and moving backward in order (9.6, 9.5, 9.4, etc.).
    [pgenv](https://github.com/theory/pgenv/) is a handy tool for installing and
    switching between versions. For each version, ensure that:

    +   Patches apply cleanly (try to eliminate Hunk warnings for patches to
        `pgtap.sql` itself, usually by fixing line numbers)

    +   All files are installed (on 8.3 and earlier that includes pgtap.so).

    +   `ALTER EXTENSION pgtap UPDATE;` works on 9.1 and higher.

    +   `CREATE EXTENSION pgtap;` works on 9.1 and higher.

    +   All tests pass in `make installcheck` (on 8.1, move the pgtap source
        dir into the postgres source `contrib` directory and run
        `make NO_PGXS=1 installcheck`)

*   If you've made any significant changes while testing versions backward, test
    them again in forward order (9.1, 9.2, 9.3, etc.) to make sure the changes
    didn't break any later versions.

*   Review the documentation in `doc/pgtap.mmd`, and make any necessary changes,
    including to the list of PostgreSQL version-compatibility notes at the end
    of the document.

*   Add an item to the top of the `%changelog` section of `contrib/pgtap.spec`.
    It should use the version you're about to release, as well as the date and
    your name and email address. Add at least one bullet mentioning the
    upgrade.

*   Run `make html` (you'll need
    [MultiMarkdown](http://fletcherpenney.net/multimarkdown/) in your path and
    the [Pod::Simple::XHTML](https://metacpan.org/module/Pod::Simple::XHTML)
    Perl module installed), then checkout the `gh-pages` branch and make these
    changes:

    +   Open `documentation.html` and delete all the lines between these "DOC"
        comments, until the main div looks like this:

            <div id="main">
              <!-- DOC INTRO HERE -->
              <!-- END DOC INTRO HERE -->
              <div id="toc">
                <!-- TOC SANS "pgTAP x.xx" -->
                <!-- END TOC -->
              </div>
              <!-- DOCS HERE, WITH INTRO MOVED ABOVE -->
              <!-- END DOCS -->
            </div>

    +   Copy the first `<h1>` and `<p>` from `doc/pgtap.html` into the
        `DOC INTRO HERE` section.

    +   Copy the rest of `doc/pgtap.html` into the
        `DOCS HERE, WITH INTRO MOVED ABOVE` section.

    +   Copy the entire contents of `doc/toc.html` into the
        `DOC SANS pgTAP x.xx` section, and then remove the first `<li>` element that
        says "pgTAP x.xx".

    +   Sanity-check that everything looks right; use `git diff` to make sure
        nothing important was lost. It should mainly be additions.

    +   Commit the changes, but don't push them yet.

*   Go back to the `master` branch and proofread the additions to the `Changes`
    file since the last release. Make sure all relevant changes are recorded
    there, and that any typos or formatting errors are corrected.

*   Timestamp the `Changes` file. I generate the timestamp like so:

        perl -MDateTime -e 'print DateTime->now->datetime . "Z\n"'

    Paste that into the line with the new version, maybe increment by a minute
    to account for the time you'll need to actually do the release.

*   Commit the timestamp and tag it:

         git ci -m 'Timestamp v0.98.0.'
         git tag -sm 'Tag v0.98.0.' v0.98.0

*   Package the source and submit to [PGXN](http://manager.pgxn.org/).

        gem install pgxn_utils
        git archive --format zip --prefix=pgtap-1.0.0/ \
        --output pgtap-1.0.0.zip master

*   Push all the changes to GitHub.

        git push
        git push --tags
        git push origin up/gh-pages:gh-pages

*   Increment the minor version to kick off development for the next release.
    The version should be added to the `Changes` file, and incremented in the
    following files:

    +   `META.json` (including for the three parts of the `provides` section)
    +   `README.md`
    +   `contrib/pgtap.spec`
    +   `doc/pgtap.mmd`
    +   `pgtap.control`

*   Commit that change and push it.

        git ci -m 'Increment to v1.0.1.'
        git push

*   Start hacking on the next version!
