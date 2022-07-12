---
title: Docs contribution checklist
headerTitle: Docs checklist
linkTitle: Docs checklist
description: Review the steps to start contributing documentation
image: /images/section_icons/index/quick_start.png
menu:
  preview:
    identifier: docs-checklist
    parent: docs
    weight: 2911
type: docs
---

## Quick notes

* [File issues in GitHub](#file-tickets).
  * Internal users: please file Platform and Cloud issues in Jira.
* Docs are written in Markdown and built using the Hugo static site generator.
* For live previews as you work, install the command-line tools (macOS), Node.js, and Hugo. (See [How to build the docs](../docs-build/).)
* Pull requests:
  * Open PRs against a fork of the yugabyte/yugabyte-db repository.
  * Add tag "area/documentation".
  * Internal users: add to project Documentation, and assign a member of the docs team as a reviewer.

## File docs issues and make suggestions {#file-tickets}

Regardless of the type of problem (such as errors, bad links, out-of-date content, or new features), if you don't intend to [edit the docs](#edit-the-docs) and make a pull request right away to fix the problem, please create an issue in GitHub or Jira (Yugabyte internal users only) to track the problem.

Every YugabyteDB docs page has a Contribute button that lets you file an issue or make a suggestion, both of which help you to create a GitHub issue. You can also create an issue [directly from GitHub](https://github.com/yugabyte/yugabyte-db/issues/new/choose).

The GitHub issue template starts your issue title with `[docs]` for faster scanning, and adds the `area/documentation` label. You can also add the section of the docs (for example, `[docs] [troubleshooting]`) for added context.

**Internal users**, add a member of the docs team as a reviewer, in addition to any other technical reviewers, and add your ticket directly to the Documentation project.

## Run the docs site locally

Follow the instructions in [Build the docs](../docs-build/) to fork the repository and get the docs site running on your local machine.

## Find the right page or section

Use the [overview of sections](../docs-layout/) to help you find the page you want to edit, or the correct section for a new page you want to add.

## Edit the docs {#edit-the-docs}

Follow the instructions in [Edit the docs](../docs-edit/) to make your changes (read the [docs style guide](../docs-style/), too!) and submit a pull request.

## Submit a pull request {#make-a-pr}

Congratulations on the change! You should now [submit a pull request](../docs-edit/#make-a-pr) for review and leave a message on the Slack channel. After it's reviewed and approved, your docs change will get merged in.
