### This directory contains all types of yugabyte packages.

# How to build Deb and RPM package.

To build the deb and rpm package we are using a command-line utility called `fpm` which is designed to help us to build a different kind of packages.

- Installing `fpm` utility on Debian-derived systems (Debian, Ubuntu, etc). 
   - Install pre required packages.
        ```
        $ apt-get install ruby ruby-dev rubygems build-essential
        ```
   - Install `fpm` utility.
        ```
        $ gem install --no-document fpm
        ```
   - Check `fpm` version
        ```
        $ fpm --version
        ```

- To create Deb package 
  - First we will get the Yugabyte latest relese from its download [link](https://download.yugabyte.com/)
   - Now extract the downloaded package.
        ``` 
        $ tar xvfz yugabyte-2.0.5.2-linux.tar.gz
        ```
   - Add some script required to configure yugabyte.
        ```
        $ cp script/install.sh yugabyte-2.0.5.2/
        $ cp script/uninstall.sh yugabyte-2.0.5.2/
        $ cp script/yugabyte yugabyte-2.0.5.2/
        $ cp -r script/etc yugabyte-2.0.5.2/
        ```
   - Now use the `fpm` comand line utiliy to create a deb package. 
        ```
        $ fpm -s dir -t deb -n yugabyte -v 2.0.5.2 --after-install yugabyte-2.0.5.2/install.sh --after-remove yugabyte-2.0.5.2/uninstall.sh --deb-init yugabyte-2.0.5.2/yugabyte --url https://www.yugabyte.com/ -m yugabye yugabyte-2.0.5.2=/opt
        ```
- TO create rpm package.
  - Extract the downloaded package.
        ``` 
        $ tar xvfz yugabyte-2.0.5.2-linux.tar.gz
        ```
   - Add some script required to configure yugabyte.
        ```
        $ cp install.sh yugabyte-2.0.5.2/
        $ cp uninstall.sh yugabyte-2.0.5.2/
        $ cp yugabyte yugabyte-2.0.5.2/
        ```
   - Now use the `fpm` comand line utiliy to create a deb package. 
        ```
        $ fpm -s dir -t rpm -n yugabyte -v 2.0.5.2 --after-install yugabyte-2.0.5.2/install.sh --after-remove yugabyte-2.0.5.2/uninstall.sh --deb-init yugabyte-2.0.5.2/yugabyte --url https://www.yugabyte.com/ -m yugabyte yugabyte-2.0.5.2=/opt
        ```
- Convert deb package to rpm package.
   - To convert deb package to rpm package we will use `fpm` command.
        ```
        $ fpm --verbose -s deb -t rpm yugabyte_2.0.5.2_amd64.deb
        ```
   - To copnvert rpm to deb package 
        ```
        $ fpm --verbose -s rpm -t deb yugabyte_2.0.5.2_amd64.rpm
        ```
