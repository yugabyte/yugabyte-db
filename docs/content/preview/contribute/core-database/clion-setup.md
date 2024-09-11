---
title: Configure a CLion project
headerTitle: Configure a CLion project
linkTitle: Configure a CLion project
description: Configure a CLion project for building YugabyteDB using cmake or ninja.
headcontent: Use the CLion IDE with YugabyteDB
menu:
  preview:
    identifier: configure-clion
    parent: core-database
    weight: 2912
type: docs
---

There are two options for build systems that you can use with YugabyteDB, [`ninja`](https://ninja-build.org/) and [`make`](https://en.wikipedia.org/wiki/Make_(software)).

Note that the [CMake](https://cmake.org/) meta build system is used in both cases, and it generates build files consumed by the underlying Ninja and Make build systems.

* `ninja` is faster than `make`, especially for rebuilding mostly-built projects, but CLion has limited support for `ninja` (for example, it doesn't allow you to [rebuild individual files](https://youtrack.jetbrains.com/issue/CPP-17622)).
* `make` is well-supported by CLion, but slower, particularly for rebuilding mostly-built projects, compared to `ninja`.

### Configure as a compilation database project

For best performance, configure the project as a compilation database project:

1. Run `./yb_build.sh compilecmds` to generate the `compile_commands.json` file in the `yugabyte-db` directory.

1. Verify that `compile_commands.json` is present in the `yugabyte-db` folder. From the yugabyte-db folder, run the following command:

    ```sh
    $ find . -name "compile_commands.json"
    ```

    You should see output similar to the following:

    ```output
    ./compile_commands.json
    ./build/compilecmds-clang-dynamic-arm64-ninja/compile_commands.json
    ./build/compilecmds-clang-dynamic-arm64-ninja/compile_commands/combined_raw/compile_commands.json
    ./build/compilecmds-clang-dynamic-arm64-ninja/compile_commands/yb_postprocessed/compile_commands.json
    ./build/compilecmds-clang-dynamic-arm64-ninja/compile_commands/yb_raw/compile_commands.json
    ./build/compilecmds-clang-dynamic-arm64-ninja/compile_commands/combined_postprocessed/compile_commands.json
    ./build/compilecmds-clang-dynamic-arm64-ninja/compile_commands/pg_raw/compile_commands.json
    ./build/compilecmds-clang-dynamic-arm64-ninja/compile_commands/pg_postprocessed/compile_commands.json
    ./build/compilecmds-clang-dynamic-arm64-ninja/postgres_build/compile_commands.json
    ./build/compilecmds-clang-dynamic-arm64-ninja/postgres_build/contrib/compile_commands.json
    ./build/compilecmds-clang-dynamic-arm64-ninja/postgres_build/third-party-extensions/compile_commands.json
    ```

1. If `./compile_commands.json` is not there, then make a symlink to the one in the build folder as follows:

    ```sh
    ln -s build/compilecmds-clang-dynamic-arm64-ninja/compile_commands/combined_postprocessed/compile_commands.json compile_commands.json
    ```

    Replace `compilecmds-clang-dynamic-arm64-ninja` as appropriate.

1. Close the CLion project.

1. Delete the `.idea` folder in the `yugabyte-db` folder:

    ```sh
    rm -r .idea
    ```

1. Re-open the `yugabyte-db` folder in CLion.

1. When prompted to open the folder as a CMake project or as a Compilation Database project, choose **Compilation Database project**.

### Configure a CLion project for YugabyteDB

#### Opening the directory

Click **File > Open…** to open the project root directory.

#### Configuring CMake preferences

##### Setting CMake preferences when using Ninja

If you want to build with Ninja, use `build/debug-clang-dynamic-ninja` as a "Generation path" and add `-G Ninja` into "CMake options":

![CLion Ninja options](/images/contribute/clion-cmake-options-ninja.png)

##### Setting CMake preferences when using Make

Select `build/debug-clang-dynamic` as the **Generation path** in **Preferences > Build, Execution, Deployment/CMake**, and do not specify anything for **CMake options**.

![CLion Make options](/images/contribute/clion-cmake-options.png)

#### Reloading the project

Use **File / Reload CMake Project**. CLion will start updating symbols, which also can take a while.

#### Doing the build from CLion

Run from the command line inside project root outside CLion (omit `YB_USE_NINJA=0` if you want to use Ninja):

```sh
YB_USE_NINJA=0 ./yb_build.sh
```

Subsequent builds can be launched also from CLion.
