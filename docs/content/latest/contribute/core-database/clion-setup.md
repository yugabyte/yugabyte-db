---
title: CLion project setup
linkTitle: CLion project setup
description: CLion project setup
image: /images/section_icons/index/quick_start.png
headcontent: CLion project setup.
type: page
menu:
  latest:
    identifier: clion-setup
    parent: contribute-db-core
    weight: 2912
isTocNested: true
showAsideToc: true
---

There are two options for build systems that you can use with YugabyteDB, `cmake` and `ninja`.

* `make` is well-supported by CLion, but slower for rebuild comparing to ninja.
* `ninja` is faster, but CLion has limited support for `ninja` (for example, it doesn't allow you to [rebuild individual files](https://youtrack.jetbrains.com/issue/CPP-17622)).


#### Configure a CLion project for YugabyteDB:

1. Use “File / Open…” to open the project root directory.
2. Put build/debug-clang-dynamic (or build/debug-clang-dynamic-ninja if you want to use ninja) as a “Generation path” in “Preferences/Build, Execution, Deployment/CMake”: 
If you want to build with ninja, use build/debug-clang-dynamic-ninja as a generation path and add -G Ninja into “CMake options”:    
    
![CLion cmake options](/images/contribute/clion-cmake-options.png)

If you want to build with ninja, use `build/debug-clang-dynamic-ninja` as a "Generation path" and add `-G Ninja` into "CMake options":

![Clion ninja options](/images/contribute/clion-cmake-options-ninja.png)


3. Use “File / Reload CMake Project'' - it should start building third party dependencies for YugabyteDB. 
Building a third party can take tens of minutes and then CLion will start updating symbols which also can take a while.
4. Run from the command line inside project root (omit `YB_USE_NINJA=0` if you want to use ninja): ```YB_USE_NINJA=0 ./yb_build.sh```

Subsequent builds can be launched also from CLion.
