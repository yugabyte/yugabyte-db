---
title: Configure a CLion project
headerTitle: Configure a CLion project
linkTitle: Configure a CLion project
description: Configure a CLion project for building YugabyteDB using cmake or ninja.
headcontent: Use the CLion IDE with YugabyteDB
menu:
  stable:
    identifier: configure-clion
    parent: core-database
    weight: 2912
type: docs
---

Configure a project in the [CLion](https://www.jetbrains.com/clion/) C/C++ IDE.

## Configure a CLion compilation database project

For best performance, configure the project as a compilation database project:

1. Run `./yb_build.sh compilecmds` to generate the `compile_commands.json` file in the `yugabyte-db` directory.

1. Verify that `compile_commands.json` is present in the `yugabyte-db` folder. From the `yugabyte-db` folder, run the following command:

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

1. If you previously opened the folder in CLion, delete the `.idea` folder in the `yugabyte-db` folder:

    ```sh
    rm -r .idea
    ```

1. Re-open the `yugabyte-db` folder in CLion.

1. When prompted to open the folder as a CMake project or as a Compilation Database project, choose **Compilation Database project**.
