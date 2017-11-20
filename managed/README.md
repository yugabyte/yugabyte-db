// Copyright (c) YugaByte, Inc.

# Yugaware Developer Documentation

## Building and running Yugaware locally

### Pre-requisites

#### General Pre-requisites

* Install JDK8
* Need to have `vault_password`, yugabyte dev pem file `no-such-key.pem` (AWS) inside of `~/.yugabyte`
* And also `ansible.env` file with AWS credentials inside of `~/.yugabyte`:
  ```
  export AWS_ACCESS_KEY=<your AWS access key>
  export AWS_SECRET_KEY=<your AWS secret key>
  export AWS_ACCESS_KEY_ID="$AWS_ACCESS_KEY"
  export AWS_SECRET_ACCESS_KEY="$AWS_SECRET_KEY"
  export AWS_DEFAULT_REGION=us-west-2
  export YB_EC2_KEY_PAIR_NAME=no-such-key
  ```

#### On a mac, run the following:
* Install SBT and Node
```
  $ brew install sbt
  $ brew install node
```
* Setup Postgres
```
  # Currently we support postgres@9.5 for local development
  $ brew install postgresql@9.5
  $ echo 'export PATH="/usr/local/opt/postgresql@9.5/bin:$PATH"' >> ~/.bash_profile
  # Note: do not set any password for postgres.
  # Make postgres a daemon.
  $ ln -sfv /usr/local/opt/postgresql/*.plist ~/Library/LaunchAgents
  $ launchctl load ~/Library/LaunchAgents/homebrew.mxcl.postgresql.plist
  # Create user and database
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
* Install Node js
```
  $ curl -sL https://deb.nodesource.com/setup_7.x | sudo -E bash -
  $ sudo apt-get install -y nodejs
```
* Setup Postgres
```
# Create user and yugaware db in PostgreSQL:
# Follow all the steps on http://tecadmin.net/install-postgresql-server-on-ubuntu/
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

* Install third-parties
```
yb_devops_home=~/code/devops/ ~/code/devops/bin/py_wrapper ansible-playbook ~/code/devops/docker/images/thirdparty-deps/dependencies.yml
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

* To compile and run the code:
```
  $ sbt "run -Dyb.devops.home=<path to your devops repo>"
  # Test that everything is running by going to http://localhost:9000 in a browser (or curl). This page will show 
  expected error 'Action not found' and list of possible API routes.
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

* To build production version of the UI code.
```
  $ cd ui
  $ npm install
  $ npm build
```

#### Developing Yugaware in Eclipse

* Eclipse can be used as an IDE for Yugaware. To generate Eclipse project files, do:

```
  # Create the sbt plugins directory for your local machine.
  $ mkdir ~/.sbt/0.13/plugins/

  # Create a file ~/.sbt/0.13/plugins/build.sbt with the contents shown below.
  $ cat > ~/.sbt/0.13/plugins/build.sbt
  resolvers += Classpaths.typesafeResolver
  addSbtPlugin("com.typesafe.sbteclipse" % "sbteclipse-plugin" % "4.0.0")
```

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
