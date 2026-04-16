---
title: Build the YugabyteDB docs locally
headerTitle: Build the docs
linkTitle: Build the docs
description: Build the YugabyteDB docs locally
menu:
  stable:
    identifier: docs-build
    parent: docs
    weight: 2913
type: docs
---

## Prerequisites

To run the docs site locally and edit the docs, you'll need:

* **A text editor**, such as [Visual Studio Code](https://code.visualstudio.com).

* [**Node.js**](https://nodejs.org/en/download/) LTS (22) using [NVM](https://github.com/nvm-sh/nvm?tab=readme-ov-file#install--update-script) : `nvm install 22.14.0`

* **A GitHub account**.

In addition to the above, there are tools you'll need to install, and the steps vary depending on your machine.

<ul class="nav nav-tabs nav-tabs-yb">
    <li >
    <a href="#macos" class="nav-link active" id="macos-tab" data-bs-toggle="tab" role="tab" aria-controls="macos" aria-selected="true">
      <i class="fa-brands fa-apple" aria-hidden="true"></i>
      macOS
    </a>
  </li>
  <li>
    <a href="#ubuntu" class="nav-link" id="ubuntu-tab" data-bs-toggle="tab" role="tab" aria-controls="ubuntu" aria-selected="true">
      <i class="fa-brands fa-ubuntu" aria-hidden="true"></i>
      Ubuntu
    </a>
  </li>

</ul>

<div class="tab-content">
  <div id="macos" class="tab-pane fade show active" role="tabpanel" aria-labelledby="macos-tab">
{{% readfile "./macos.md" %}}
  </div>
  <div id="ubuntu" class="tab-pane fade" role="tabpanel" aria-labelledby="ubuntu-tab">
{{% readfile "./ubuntu.md" %}}
  </div>
</div>

## Configure Hugo

By default, Hugo uses the operating system's temporary directory to cache modules, which can cause some problems. You can avoid those problems by telling Hugo to put its cache elsewhere.

Add a line similar to the following to your `.bashrc` or `.zshrc` file:

```sh
export HUGO_CACHEDIR=~/.hugo-cache
```

Create the folder with `mkdir ~/.hugo-cache`, then start a new terminal session.

## Fork the repository

1. To make the commands in this section work correctly when you paste them, set an environment variable to store your GitHub username. (Replace `your-github-id` in the following command with your own GitHub ID.)

    ```sh
    export GITHUB_ID=your-github-id
    ```

1. Fork the [`yugabyte-db` GitHub repository](https://github.com/yugabyte/yugabyte-db/).

1. Create a local clone of your fork:

    ```sh
    git clone https://github.com/$GITHUB_ID/yugabyte-db.git
    ```

1. Identify your fork as _origin_ and the original YB repository as _upstream_:

    ```sh
    cd yugabyte-db/
    git remote set-url origin https://github.com/$GITHUB_ID/yugabyte-db.git
    git remote add upstream https://github.com/yugabyte/yugabyte-db.git
    ```

1. Make sure that your local repository is still current with the upstream Yugabyte repository:

    ```sh
    cd docs/
    git checkout master
    git pull upstream master
    ```

Refer to [Edit an existing page](../docs-edit/#edit-an-existing-page) to create a new branch, commit your changes, and create a pull request.

## Build the docs site {#live-reload}

The YugabyteDB documentation is written in Markdown, and processed by Hugo (a static site generator) into an HTML site.

To get the docs site running in a live-reload server on your local machine, run the following commands:

```sh
cd yugabyte-db/docs  # Make sure this is YOUR fork.
npm ci               # Only necessary the first time you clone the repo.
hugo mod get -u      # Installs Hugo as a dependency of the site.
hugo mod clean       # Only necessary the first time you clone the repo.
npm start            # Do this every time to build the docs and launch the live-reload server.
```

The live-reload server runs at <http://localhost:1313/> unless port 1313 is already in use. Check the output from the `npm start` command to verify the port.

When you're done, type Ctrl-C stop the server.

### Optional: Run builds more quickly

If you are only working in `preview` or `stable`, you can start the live-reload server more quickly using the following:

```sh
npm run fast
```

This builds only the `preview` and `stable` directories, and does not generate syntax diagrams.

### Optional: Run a full build {#full-build}

The live-reload server is the quickest way to get the docs running locally. If you want to run the build exactly the same way the CI pipeline does for a deployment, do the following:

```sh
cd yugabyte-db/docs
npm run build
```

When the build is done, the `yugabyte-db/docs/public` folder contains a full HTML site, exactly the same as what's deployed on the live website at <https://docs.yugabyte.com/>.

## Troubleshooting

* Make sure the GUI installer for the command-line tools finishes with a dialog box telling you the install succeeded. If not, run it again.

* If you get an error about missing command-line tools, make sure xcode-select is pointing to the right directory, and that the directory contains a `usr/bin` subdirectory. Run `xcode-select -p` to find the path to the tools. Re-run xcode-select --install.

* If the live-reload server (`npm start`) is returning a Hugo error &mdash; say, about shortcodes &mdash; re-run `hugo mod clean`, followed by `npm start`. Also, be sure you've followed the instructions on this page to [configure Hugo](#configure-hugo).

* Make sure your tools are up-to-date. Run `brew update` periodically, and if it reports anything out of date, run `brew upgrade`.

* If you get an error about missing modules, try running `npm install`.

## Next steps

Need to edit an existing page? [Start editing](../docs-edit/) it now. (Optional: [set up your editor](../docs-editor-setup/).)

Adding a new page? Use the [overview of sections](../docs-layout/) to find the appropriate location.
