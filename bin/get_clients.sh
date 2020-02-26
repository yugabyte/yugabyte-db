#!/bin/sh
# Download yugabyte client binaries locally

set -e

VERSION=2.1.0.0

uname=$(uname | tr '[:upper:]' '[:lower:]')
pkg="yugabyte-${VERSION}-$uname.tar.gz"

printf "Downloading %s ... \r" "$pkg"
wget -q "https://downloads.yugabyte.com/${pkg}" -O "$pkg"

printf "Extracting %s ... \r" $pkg
tar -zxf "$pkg"

if test "$uname" = "linux"; then
   printf "Setting up %s ... \r" $pkg
  "yugabyte-${VERSION}/bin/post_install.sh" > /dev/null 2>&1
fi

echo "ysqlsh is at yugabyte-${VERSION}/bin/ysqlsh                                      "
echo "csqlsh is at yugabyte-${VERSION}/bin/cqlsh"
echo You may want to run
echo " sudo mv yugabyte-${VERSION}/bin/ysqlsh /usr/local/bin/"
