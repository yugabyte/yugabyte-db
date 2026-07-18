pgTAP Release Management
========================

Here are the steps to take to make a release of pgTAP:

*   Review and fix any open bugs in the
    [issue tracker](https://github.com/theory/pgtap/issues).

*   Review and merge any appropriate
    [pull requests](https://github.com/theory/pgtap/pulls).

*   Make sure that [all tests](https://github.com/theory/pgtap/actions) pass on
    all supported versions of Postgres. If you want to run the tests manually,
    you can use the [pgxn-utils Docker image](https://github.com/pgxn/docker-pgxn-tools)
    or [pgenv](https://github.com/theory/pgenv/) to install and
    switch between versions. For each version, ensure that:

    +   Patches apply cleanly (try to eliminate Hunk warnings for patches to
        `pgtap.sql` itself, usually by fixing line numbers)

    +   All files are installed.

    +   `ALTER EXTENSION pgtap UPDATE;` works.

    +   `CREATE EXTENSION pgtap;` works.

    +   All tests pass in `make installcheck`.

*   If you've made any significant changes while testing versions backward, test
    them again in forward order (9.1, 9.2, 9.3, etc.) to make sure the changes
    didn't break any later versions.

*   Review the documentation in `doc/pgtap.mmd`, and make any necessary changes,
    including to the list of PostgreSQL version-compatibility notes at the end
    of the document.

*   Add an item to the top of the `%changelog` section of `contrib/pgtap.spec`.
    It should use the version you're about to release, as well as the date (use
    `date +'%a %b %d %Y'`) and your name and email address. Add at least one
    bullet mentioning the upgrade.

*   Run `make html` (you'll need
    [MultiMarkdown](https://fletcherpenney.net/multimarkdown/) (Homebrew:
    `brew install multimarkdown`) in your path and the
    [Pod::Simple::XHTML](https://metacpan.org/module/Pod::Simple::XHTML)
    (Homebrew: `brew install cpanm && cpanm Pod::Simple::XHTML`) Perl module
    installed), then checkout the `gh-pages` branch and make these changes:

    +   `cp .documentation.html.template documentation.html`. Edit
        documentation.html, the main div should look like this:

            <div id="main">
              <!-- DOCS INTRO HERE -->
              <!-- END DOCS INTRO HERE -->
              <div id="toc">
                <!-- TOC SANS "pgTAP x.xx" -->
                <!-- END TOC -->
              </div>
              <!-- DOCS HERE, WITH INTRO MOVED ABOVE -->
              <!-- END DOCS -->
            </div>

    +   Copy the first `<h1>` and `<p>` from `doc/pgtap.html` into the
        `DOCS INTRO HERE` section.

    +   Copy the rest of `doc/pgtap.html` into the
        `DOCS HERE, WITH INTRO MOVED ABOVE` section.

    +   Copy the entire contents of `doc/toc.html` into the
        `DOC SANS pgTAP x.xx` section, and then remove the first `<li>` element that
        says "pgTAP x.xx".

    +   Review to ensure that everything looks right; use `git diff` to make sure
        nothing important was lost. It should mainly be additions.

    +   Commit the changes, but don't push them yet.

*   Go back to the `main` branch and proofread the additions to the `Changes`
    file since the last release. Make sure all relevant changes are recorded
    there, and that any typos or formatting errors are corrected.

*   Timestamp the `Changes` file. I generate the timestamp like so:

        perl -MDateTime -e 'print DateTime->now->datetime . "Z\n"'

    Paste that into the line with the new version, maybe increment by a minute
    to account for the time you'll need to actually do the release.

*   Commit the timestamp and push it:

        git ci -m 'Timestamp v0.98.0.'
        git push

*   Once again make sure [all tests](https://github.com/theory/pgtap/actions)
    pass. Fix any that fail.

*   Once all tests pass, tag the release with its semantic version (including
    the leading `v`) and push the tag.

        git tag -sm 'Tag v0.98.0.' v0.98.0
        git push --tags

*   Monitor the [release workflow](https://github.com/theory/pgtap/actions/workflows/release.yml)
    to make sure the new version is released on both PGXN and GitHub.

*   Push the `gh-pages` branch:

        git push

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
