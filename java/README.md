
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

Asynchronous Native Java Client for Kudu

System Requirements
------------------------------------------------------------

- Java 7
- Maven 3
- protobuf 2.6.1 (it needs to be the exact version)


Building the Client
------------------------------------------------------------

$ mvn package -DskipTests

The client jar will can then be found at kudu-client/target.


Building the Kudu CSD
------------------------------------------------------------

By default, the Kudu CSD will not be built with the client.
It requires access to the Kudu binaries which may not be
available. For example, when building on OSX.

Here's how to build the kudu-csd module:

$ mvn package -DskipTests -PbuildCSD

Also by default, building the CSD does not validate it,
because (for the moment) this requires access to an internal
Cloudera repository containing the validator maven plugin.

Here's how to build the kudu-csd module with validation:

$ mvn package -DskipTests -PbuildCSD -PvalidateCSD


Running the Tests
------------------------------------------------------------

Most of the unit tests will start their own cluster but it
is also possible to provide your own.

By default, the unit tests will start a master and a tablet
server using the flags file located in the src/test/resources/
directory. The tests will locate the master and tablet server
binaries by looking in 'build/latest/bin' from the root of
the git repository. If you have recently built the C++ code
for Kudu, those should be present already.

Once everything is setup correctly, run:

$ mvn test

In order to point the unit tests to an existing cluster,
you need to use a command line like this one:

$ mvn test -DstartCluster=false

If you choose to not start a cluster, the tests will look for
a master running on localhost:7051. If you would like to run
against a remote cluster, you can override this using
-DmasterAddress:

$ mvn test -DstartCluster=false -DmasterAddress=foo.example.com:7051

If for some reason you would like to start a cluster, but use
binaries other than the ones in build/latest/, you can pass
-DbinDir=/path/to/directory.

Integration tests, including tests which cover Hadoop integration,
may be run with:

$ mvn verify

State of Eclipse integration
------------------------------------------------------------

Maven projects can be integrated with Eclipse in one of two
ways:

1. Import a Maven project using Eclipse's m2e plugin.
2. Generate Eclipse project files using maven's
   maven-eclipse-plugin plugin.

Each approach has its own pros and cons.

## m2e integration (Eclipse to Maven)

The m2e approach is generally recommended as m2e is still
under active development, unlike maven-eclipse-plugin. Much
of the complexity comes from how m2e maps maven lifecycle
phases to Eclipse build actions. The problem is that m2e
must be told what to do with each maven plugin, and this
information is either conveyed through explicit mapping
metadata found in pom.xml, or in an m2e "extension". m2e
ships with extensions for some of the common maven plugins,
but not for maven-antrun-plugin or maven-protoc-plugin. The
explicit metadata mapping found in kudu-client/pom.xml has
placated m2e in both cases (in Eclipse see
kudu-client->Properties->Maven->Lifecycle Mapping).
Nevertheless, maven-protoc-plugin isn't being run correctly.

To work around this, you can download, build, and install a
user-made m2e extension for maven-protoc-plugin:

  http://www.masterzen.fr/2011/12/25/protobuf-maven-m2e-and-eclipse-are-on-a-boat

See http://wiki.eclipse.org/M2E_plugin_execution_not_covered
for far more excruciating detail.

## maven-eclipse-plugin (Maven to Eclipse)

The maven-eclipse-plugin approach, despite being old
fashioned and largely unsupported, is easier to use. The
very first time you want to use it, run the following:

$ mvn -Declipse.workspace=<path-to-eclipse-workspace> eclipse:configure-workspace

This will add the M2_REPO classpath variable to Eclipse. You
can verify this in
Preferences->Java->Build Path->Classpath Variables. It
should be set to `/home/<user>/.m2/repository`.

To generate the Eclipse project files, run:

$ mvn eclipse:eclipse

If you want to look at Javadoc/source in Eclipse for
dependent artifacts, run:

$ mvn eclipse:eclipse -DdownloadJavadocs=true -DdownloadSources=true

So what's the problem with maven-eclipse-plugin? The issue
lies with maven-protoc-plugin. Because all of our .proto
files are in src/kudu, the "resource path" in
maven-protoc-plugin must be absolute and prefixed with
${project.baseDir). This absolute path is copied verbatim
to an Eclipse .classpath <classpathentry/>, and Eclipse
doesn't know what to do with it, causing it avoid building
kudu-client altogether. Other plugins (like
maven-avro-plugin) don't seem to have this problem, so it's
likely a bug in maven-protoc-plugin.

There's a simple workaround: delete the errant folder within
Eclipse and refresh the kudu-client project.
