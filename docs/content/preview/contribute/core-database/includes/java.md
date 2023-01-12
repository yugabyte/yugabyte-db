<!--
+++
private = true
+++
-->

YugabyteDB core is written in C and C++, but the repository contains Java code needed to run some tests and sample applications. To build the Java part, you need:

* Java Development Kit (JDK) 1.8. JDK installers for Linux and macOS can be downloaded from [OpenJDK](http://jdk.java.net/), [AdoptOpenJDK](https://adoptopenjdk.net/), or [Azul Systems](https://www.azul.com/downloads/zulu-community/). Homebrew users on macOS can install using `brew install openjdk`.
* [Apache Maven](https://maven.apache.org/) 3.3 or later.

Also make sure Maven's `bin` directory is added to your `PATH` (for example, by adding to your `~/.bashrc`). For example, if you've installed Maven into `~/tools/apache-maven-3.6.3`:

```sh
export PATH=$HOME/tools/apache-maven-3.6.3/bin:$PATH
```
