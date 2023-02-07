<!--
+++
private = true
+++
-->

YugabyteDB core is written in C and C++, but the repository contains Java code needed to run some tests and sample applications.
To build the Java part, you need:

* Java Development Kit (JDK) 8 or 11.
  JDK can be downloaded from [OpenJDK](http://jdk.java.net/archive), [AdoptOpenJDK](https://adoptopenjdk.net/), or [Azul Systems](https://www.azul.com/downloads/zulu-community/).
* [Apache Maven](https://maven.apache.org/) 3.3 or later.

If downloading releases, don't forget to add the bin directory to `PATH`.
For example,

```sh
mkdir ~/tools
cd ~/tools
curl 'https://cdn.azul.com/zulu/bin/zulu11.62.17-ca-jdk11.0.18-linux_x64.tar.gz' | tar xz
curl 'https://dlcdn.apache.org/maven/maven-3/3.8.7/binaries/apache-maven-3.8.7-bin.tar.gz' | tar xz
echo 'export PATH=$HOME/tools/zulu11.62.17-ca-jdk11.0.18-linux_x64/bin:$HOME/tools/apache-maven-3.8.7/bin:$PATH' >>~/.bashrc
```
