---
title: Edit the YugabyteDB docs
headerTitle: Edit the docs
linkTitle: Edit the docs
description: Get set up and edit the YugabyteDB docs locally
image: /images/section_icons/index/quick_start.png
menu:
  preview:
    identifier: docs-edit
    parent: docs
    weight: 2914
type: docs
---

After you've gotten the docs site [running locally](../docs-build/), make your changes. If you're going to be doing this a lot, read some [tips on setting up your text editor](../docs-editor-setup/).

If you need to edit syntax diagrams, see [Edit syntax diagrams](../syntax-diagrams/).

## Edit an existing page

1. In your local copy of your `yugabyte-db` fork, make sure your local `master` branch is up to date, and send any changes to GitHub's copy of your fork.

    ```sh
    git checkout master
    git pull upstream master  # get up to date with the Yugabyte GitHub repo
    git push origin           # send any changes to your fork on GitHub
    ```

1. Next, make a branch.

    ```sh
    git checkout -b my-branch-name
    ```

1. Find your file(s) in `docs/content/<version>/...` and edit as required.

1. Verify that your changes look good in the live-reload server.

    ```sh
    npm start
    ```

1. Commit your changes.

    ```sh
    git commit -A -m "Your commit message here"
    ```

1. Push your changes to your fork.

    ```sh
    git push origin
    ```

At this point, you're ready to [create a pull request](#make-a-pr).

## Add a new page

If you can, copy an existing page in the location you want the new page to be, and adjust.

Adding a new page is similar in most ways to editing an existing page, with the added complexity of sorting out the _frontmatter_ so that the page works correctly in the site, including showing up in the left-side navigation menu. See [how docs pages are structured](../docs-page-structure/) for more information.

_More content in this section is forthcoming._

## Make a pull request {#make-a-pr}

After you've made your changes, make a pull request by telling GitHub to compare a branch _on your fork_ to the master branch on the main repository.

### Use the PR preview build {#preview-build}

Preview builds take 5 minutes to build.

All PR previews on the main repository are of the form `https://deploy-preview-ABCDE--infallible-bardeen-164bc9.netlify.app/` where ABCDE is the pull request number.

Add a line in your pull request's description to tag the Netlify bot and tell it where to launch the preview:

`@netlify /preview/quick-start/`

### Run a link checker

<https://linkchecker.github.io/linkchecker/>

### Ask for a review

**Internal contributors**, please add the `area/documentation` label to your pull request, tag a member of the docs team for review, along with technical reviewers as required, and let us know about your PR in the #docs channel in Slack.

**External contributors**, please add the `area/documentation` label to your pull request, and let us know about it [in Slack](https://www.yugabyte.com/slack/).
