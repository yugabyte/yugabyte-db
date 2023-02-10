#!/bin/bash

set -ex

export DEBIAN_FRONTEND=noninteractive
export TZ=Europe/Moskow
echo $TZ >/etc/timezone

/root/odys/docker/dpkg/tzdata.sh


cd /root/odys

#======================================================================================================================================

mk-build-deps --build-dep --install --tool='apt-get -o Debug::pkgProblemResolver=yes --no-install-recommends --yes' debian/control

dpkg-buildpackage -us -uc

#======================================================================================================================================
# here we should get some files at /root
