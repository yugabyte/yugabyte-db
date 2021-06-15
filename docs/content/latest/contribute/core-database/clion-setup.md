---
title: Configure a CLion project
headerTitle: Configure a CLion project
linkTitle: Configure a CLion project
description: Configure a CLion project for building YugabyteDB using cmake or ninja.
image: /images/section_icons/index/quick_start.png
headcontent: CLion project setup.
type: page
menu:
  latest:
    identifier: configure-clion
    parent: core-database
    weight: 2912
isTocNested: true
showAsideToc: true
---

There are two options for build systems that you can use with YugabyteDB, [`make`](https://en.wikipedia.org/wiki/Make_(software)) and [`ninja`](https://ninja-build.org/).
Note that the [CMake](https://cmake.org/) meta build system is used in both cases, and it generates build files to the underlying Make and Ninja build systems.

* `make` is well-supported by CLion, but slower, particularly for rebuilding mostly-built projects, compared to `ninja`.
* `ninja` is faster, but CLion has limited support for `ninja` (for example, it doesn't allow you to [rebuild individual files](https://youtrack.jetbrains.com/issue/CPP-17622)).

### Configure a CLion project for YugabyteDB

#### Opening the directory

Click **File > Open…** to open the project root directory.

#### Configuring CMake preferences

##### Using Ninja

If you want to build with ninja, use `build/debug-clang-dynamic-ninja` as a "Generation path" and add `-G Ninja` into "CMake options":

![Clion Ninja options](/images/contribute/clion-cmake-options-ninja.png)

##### Using Make

Select `build/debug-clang-dynamic` as the **Generation path** in **Preferences > Build, Execution, Deployment/CMake”**, and do not specify anything for **CMake options**.

![CLion Make options](/images/contribute/clion-cmake-options.png)

#### Reloading the project

Use **“File / Reload CMake Project"** - it should start building third party dependencies for YugabyteDB.

Building a third party can take tens of minutes and then CLion will start updating symbols which also can take a while.

#### Doing the build from CLion

Run from the command line inside project root outside CLion (omit `YB_USE_NINJA=0` if you want to use ninja):

    ```sh
    YB_USE_NINJA=0 ./yb_build.sh
    ```

Subsequent builds can be launched also from CLion.
