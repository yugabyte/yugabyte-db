---
title: Edit the YugabyteDB docs
headerTitle: Edit the docs
linkTitle: Edit the docs
description: Get set up and edit the YugabyteDB docs locally
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

1. Verify that your changes look good in the live-reload server. If you don't have the docs site running locally, refer to [Build the docs site](../docs-build/#live-reload).

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

Preview builds take up to 5 minutes to build.

All PR previews on the main repository are of the form `https://deploy-preview-ABCDE--infallible-bardeen-164bc9.netlify.app/` where ABCDE is the pull request number.

Optionally, you can tag the Netlify bot in the PR description to tell the bot where to launch the preview. For example, if you changed the page at `docs.yugabyte.com/preview/contribute/docs/docs-edit/` you could add a tag to the PR description as shown in the following illustration:

![Tag Netlify in a PR](/images/contribute/contribute-docs-description.png)

When your reviewer opens the build preview, it will automatically land on the page you specified.

### Run a link checker

Optionally, run a link checker. For example, <https://linkchecker.github.io/linkchecker/>.

### Ask for a review

**External contributors**

If possible, in your PR on GitHub, click **Labels**, filter on 'doc', and apply the `area/documentation` label.

![Apply the docs label](/images/contribute/contribute-docs-pr-panel.png)

Let us know about your PR [in Slack](https://www.yugabyte.com/slack/) in the `#contributors-docs` channel.

**Internal contributors**

- Under **Reviewers** set a member of the docs team for review, along with technical reviewers as required.
- Under **Labels**, apply the `area/documentation` label to your PR.

Let us know about your PR in the `#docs` channel in Slack.
