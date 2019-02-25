// Copyright (c) YugaByte, Inc.

# Yugaware Developer Documentation

## Building and running Yugaware locally

### Pre-requisites

#### General Pre-requisites

* Install Python requirements:
$ (cd ~/code/devops; git pull --rebase original master)
$ ~/code/devops/bin/install_python_requirements.sh

And check Java build JAR file:
~/code/yugabyte/java/yb-loadtester/target/yb-sample-apps.jar

* Install JDK8
* Need to have `vault_password`, yugabyte dev pem file `no-such-key.pem` (AWS) inside of `~/.yugabyte`, but this is only needed to access the releases, not if you're building from source
* And also `ansible.env` file with AWS credentials inside of `~/.yugabyte`:
  ```
  export AWS_ACCESS_KEY=<your AWS access key>
  export AWS_SECRET_KEY=<your AWS secret key>
  export AWS_ACCESS_KEY_ID="$AWS_ACCESS_KEY"
  export AWS_SECRET_ACCESS_KEY="$AWS_SECRET_KEY"
  export AWS_DEFAULT_REGION=us-west-2
  export YB_EC2_KEY_PAIR_NAME=no-such-key
  ```
* Install aws cli utility for your mac.
```
  brew install awscli
```
* For other platforms, follow the instructions at http://docs.aws.amazon.com/cli/latest/userguide/installing.html

* Download YugaByte EE release from AWS S3.
```
  cd /opt/yugabyte/releases/
  aws s3 sync s3://no-such-url/{release} {release} --exclude "*" --include "yugabyte-ee*.tar.gz"
OR
  s3cmd sync s3://no-such-url/{release} {release} --exclude "*" --include "yugabyte-ee*.tar.gz"
```

* Install third-parties
```
yb_devops_home=~/code/devops/ ~/code/devops/bin/py_wrapper ansible-playbook ~/code/devops/docker/images/thirdparty-deps/dependencies.yml
```

#### On a mac, run the following:
* Install SBT and Node
Note: on CentOS use yum to install java, sbt, node(js), awscli, postgresql-9.6. See how-to in google. Like this:
      https://www.e2enetworks.com/help/knowledge-base/how-to-install-node-js-and-npm-on-centos/
```
  $ brew install sbt
  $ brew install node
```
* Setup Postgres
```
  # Currently we support postgres@9.5 for local development
  $ brew install postgresql@9.5
  # If postgresql@9.5 is not available - install postgresql@9.2 or just postgresql
  # See up-to-date Postgres how-to in google.
  $ echo 'export PATH="/usr/local/opt/postgresql@9.5/bin:$PATH"' >> ~/.bash_profile
  $ source ~/.bash_profile
  # Note: do not set any password for postgres.
  # Make postgres a daemon.
  # Check the path (and fix if it's needed) before doing it.
  $ ln -sfv /usr/local/opt/postgresql\@9.5/*.plist ~/Library/LaunchAgents
  $ launchctl load ~/Library/LaunchAgents/homebrew.mxcl.postgresql\@9.5.plist
  # Create user and database (Use -U <username> in case of different usernames)
  $ createuser root
  $ createdb yugaware
```
#### On Ubuntu, follow these steps:
* Install SBT refer to http://www.scala-sbt.org/release/docs/Installing-sbt-on-Linux.html
```
  $ echo "deb https://dl.bintray.com/sbt/debian /" | sudo tee -a /etc/apt/sources.list.d/sbt.list
  $ sudo apt-key adv --keyserver hkp://keyserver.ubuntu.com:80 --recv 642AC823
  $ sudo apt-get update
  $ sudo apt-get install sbt
```
* Install Node js (see how-to install Node js on CentOS in google)
```
  $ curl -sL https://deb.nodesource.com/setup_7.x | sudo -E bash -
  $ sudo apt-get install -y nodejs
```
* Setup Postgres
```
# Create user and yugaware db in PostgreSQL:
# Follow all the steps on http://tecadmin.net/install-postgresql-server-on-ubuntu/
# https://linode.com/docs/databases/postgresql/how-to-install-postgresql-relational-databases-on-centos-7/
# Then do the following to ensure that we can connect to "yugaware" db as "root" user.
$ sudo -u postgres createuser -s root
$ sudo -u postgres createdb yugaware
$ sudo -u root psql -d yugaware
yugaware=> \conninfo
You are connected to database "yugaware" as user "root" via socket in "/var/run/postgresql" at port "5432".
-- Just press enter to skip setting an actual password (it will be an empty string).
yugaware=> \password
Enter new password:
Enter it again:
yugaware=> \q
```

### Setup AWS credentials

*  To download yb client jars from S3
```
  # Needs AWS auth credentials to run. Make sure no quotes around the keys!
  cat > ~/.sbt/.s3credentials
  accessKey = <user's access key>
  secretKey = <user's secret key>
```

### Building and Running Yugaware

#### API Layer

* To compile and run the code (it must still running in a separate console):
```
  $ sbt "run -Dyb.devops.home=<path to your devops repo>"
OR (better!)
  $ export DEVOPS_HOME=<path to your devops repo>
  # Better to add this to your ~/.bashrc or any other profile (like ~/.yb_build_env_bashrc)
  $ export DEVOPS_HOME=~/code/devops
  # Set the terminal to prenet error on the SBT start: [ERROR] Failed to construct terminal
  $ export TERM=xterm-color
  $ sbt run

  # Test that everything is running by going to http://localhost:9000 in a browser (or curl). This page will show 
  expected error 'Action not found' and list of possible API routes.
      For request 'GET /'
      These routes have been tried, in this order:
        1   POST  /api/login  com.yugabyte.yw.controllers.SessionController.login()
        2   GET  /api/logout  com.yugabyte.yw.controllers.SessionController.logout()
        ... etc
```

* To compile and not run:
```
  $ sbt compile
```

* To run the unit tests:
```
  $ sbt test
```

* To run integration tests:
```
# Basic example to update devops and yugaware packages and run integration test and notify
$ ./run_itest --update_packages devops yugaware --notify
# To run with additional options
$ ./run_itest --help
```

* To fix any unresolved symbols or compilation errors
```
  $ sbt clean
```

#### React UI code
* To run the UI code in development mode.

```
  $ cd ui
  $ npm install
  $ npm start
```
If this doesn't work, try using node v.10

```
  $ brew uninstall --force node
  $ brew install node@10
  $ echo 'export PATH="<node/bin path>:$PATH"' >> ~/.bash_profile
  $ source ~/.bash_profile
```

* To build production version of the UI code.
```
  $ cd ui
  $ npm install
  $ npm build
```

* Explore the UI

Go to localhost:3000 and login with admin/admin

#### Developing Yugaware in Eclipse

* Eclipse can be used as an IDE for Yugaware. To generate Eclipse project files, do:

```
  # Create the sbt plugins directory for your local machine.
  $ mkdir ~/.sbt/0.13/plugins/

  # Create a file ~/.sbt/0.13/plugins/plugins.sbt with the contents shown below.
  $ cat > ~/.sbt/0.13/plugins/plugins.sbt
  resolvers += Classpaths.typesafeResolver
  addSbtPlugin("com.typesafe.sbteclipse" % "sbteclipse-plugin" % "4.0.0")
```
You may need to remove the line ```resolvers += Classpaths.typesafeResolver``` if it gives you an
error.

* Install the plugin
```
  $ cd ~/.sbt/0.13/plugins
  # Run the following to ensure that the sbt shell works.
  $ sbt
```
* Go to the yugaware directory and create the eclipse project files.
```
  $ cd yugaware
  $ sbt eclipse
```
* Fix the classpath for eclipse.
```
  bin/activator eclipse
```

Now there should be an Eclipse '.project' file generated. Go to Eclipse and import it using:
File -> Import -> General -> Existing Projects into Workspace
Hit next, and browse to the Yugaware source directory for the project to get imported.


#### Publishing Release build to S3
* Run the following command to package and publish yugaware and react components s3
```
  $ ./yb_release
```


#### Generating Map Tiles and uploading to S3
The maps in Yugaware are generated using TileMill and mbTiles project and uploaded to S3,
from where they are downloaded into /public folder during the build process.
To generate your own Map tiles, do the following -
Download TileMill or build from source https://tilemill-project.github.io/tilemill/
Create Outline World Map , customize styles using CartoCSS http://tilemill-project.github.io/tilemill/docs/manual/carto/
Export to .mbTiles file (choose zoom level, center, tile quality etc. all of which will affect the size of your output)
Use mbutil to generate base map pngs.
git clone git://github.com/mapbox/mbutil.git
sudo python setup.py install
Upload to S3 bucket, make sure permissions are open, set content/type to "image/png"

#### Troubleshooting 
##### PSQL FATAL: Ident authentication failed for user "root"
 To fix this issue, login to local postgres 
 ```
  On Centos/Ubuntu connect to postgres
  sudo -u root psql -d yugaware
OR
  sudo -u postgres psql -d yugaware
  On Mac
  psql -d yugaware
 ```
 And locate the hba_file location like this
 ```
 postgres=# show hba_file ;
  hba_file
 --------------------------------------
  /etc/postgresql/9.3/main/pg_hba.conf
 (1 row)

On Centos:
 /var/lib/pgsql/data/pg_hba.conf
 ```
 Edit the file and make sure change the method from "intent" to "trust" for localhost
 NOTE:
 ```
 host    all             all             127.0.0.1/32            trust
 ```
OR better to enable all lines in the file:
 ```
 local   all             all                                     trust
 host    all             all             127.0.0.1/32            trust
 host    all             all             ::1/128                 trust
 ```

##### java.lang.OutOfMemoryError: GC overhead limit exceeded
 To fix this issue, do the following
 ```
    echo SBT_OPTS="-XX:MaxPermSize=4G -Xmx4096M" > ~/.sbt_config
 ```
#### Incompatibility between sbt 1.2 and Java 10/11 on MacOS
 If you have sbt version 1.2 and Java version higher than 8, then you may run into
 NullPointerException on MacOS for aby sbt command (sbt run or sbt -version).
 ```
 java.lang.NullPointerException
        at java.base/java.util.regex.Matcher.getTextLength(Matcher.java:1770)
        at java.base/java.util.regex.Matcher.reset(Matcher.java:416)
        at java.base/java.util.regex.Matcher.<init>(Matcher.java:253)
        at java.base/java.util.regex.Pattern.matcher(Pattern.java:1133)
        at java.base/java.util.regex.Pattern.split(Pattern.java:1261)
        at java.base/java.util.regex.Pattern.split(Pattern.java:1334)
        at sbt.IO$.pathSplit(IO.scala:797)
        at sbt.IO$.parseClasspath(IO.scala:912)
        at sbt.compiler.CompilerArguments.extClasspath(CompilerArguments.scala:66)
 ```

 To fix this, uninstall existing Java using
 ```
    brew cask uninstall java
 ```
 and then download and install Java8 from https://www.oracle.com/technetwork/java/javase/downloads/jdk8-downloads-2133151.html

#### npm install fails
If ```npm install``` fails with this error:
```
xcode-select: error: tool 'xcodebuild' requires Xcode, but active developer directory '/Library/Developer/CommandLineTools' is a command line tools instance
```
Install Xcode from https://developer.apple.com/xcode/ if you don't have it yet.
Point xcode-select to the Xcode Developer directory using the following command:
```
sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
```

#### node-sass module not found while starting UI
Install node-sass using the command below and try again:
```
sudo npm install --save-dev  --unsafe-perm node-sass
```

See the docs for additional info:

YugaWare driven testing
https://docs.google.com/document/d/1tuwn1vQj2nOQVW9JzR6L9IwdkYzLfW8lXepUCKGrKgk

Deploying a private YB build on portal
https://docs.google.com/document/d/16eG3S8exZRbdJOqQasYsFq344oGUZeFkW48M_V5oqKQ
