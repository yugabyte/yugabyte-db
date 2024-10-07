#!/usr/bin/env bash

shell_quote_string() {
  echo "$1" | sed -e 's,\([^a-zA-Z0-9/_.=-]\),\\\1,g'
}

usage () {
    cat <<EOF
Usage: $0 [OPTIONS]
    The following options may be given :
        --builddir=DIR      Absolute path to the dir where all actions will be performed
        --get_sources       Source will be downloaded from github
        --build_src_rpm     If it is 1 src rpm will be built
        --build_source_deb  If it is 1 source deb package will be built
        --build_rpm         If it is 1 rpm will be built
        --build_deb         If it is 1 deb will be built
        --build_tarball     If it is 1 tarball will be built
        --install_deps      Install build dependencies(root previlages are required)
        --branch            Branch for build
        --repo              Repo for build
        --rpm_release       RPM version( default = 1)
        --deb_release       DEB version( default = 1)
        --pg_release        PPG version build on( default = 11)
        --version           product version
        --help) usage ;;
Example $0 --builddir=/tmp/test --get_sources=1 --build_src_rpm=1 --build_rpm=1
EOF
        exit 1
}

append_arg_to_args () {
  args="$args "$(shell_quote_string "$1")
}

 parse_arguments() {
    pick_args=
    if test "$1" = PICK-ARGS-FROM-ARGV
    then
        pick_args=1
        shift
    fi

    for arg do
        val=$(echo "$arg" | sed -e 's;^--[^=]*=;;')
        case "$arg" in
            # these get passed explicitly to mysqld
            --builddir=*) WORKDIR="$val" ;;
            --build_src_rpm=*) SRPM="$val" ;;
            --build_source_deb=*) SDEB="$val" ;;
            --build_rpm=*) RPM="$val" ;;
            --build_deb=*) DEB="$val" ;;
            --get_sources=*) SOURCE="$val" ;;
            --build_tarball=*) TARBALL="$val" ;;
            --install_deps=*) INSTALL="$val" ;;
            --branch=*) BRANCH="$val" ;;
            --repo=*) REPO="$val" ;;
            --rpm_release=*) RPM_RELEASE="$val" ;;
            --deb_release=*) DEB_RELEASE="$val" ;;
            --pg_release=*) PG_RELEASE="$val" ;;
            --version=*) VERSION="$val" ;;
            --help) usage ;;
            *)
              if test -n "$pick_args"
              then
                  append_arg_to_args "$arg"
              fi
              ;;
        esac
    done
}

check_workdir(){
    if [ "x$WORKDIR" = "x$CURDIR" ]
    then
        echo >&2 "Current directory cannot be used for building!"
        exit 1
    else
        if ! test -d "$WORKDIR"
        then
            echo >&2 "$WORKDIR is not a directory."
            exit 1
        fi
    fi
    return
}

set_changelog(){
    if [ -z $1 ]
    then
        echo "No spec file is provided"
        return
    else
        start_line=0
        while read -r line; do
            (( start_line++ ))
            if [ "$line" = "%changelog" ]
            then
                (( start_line++ ))
                echo "$start_line"
                current_date=$(date +"%a %b %d %Y")
                sed -i "$start_line,$ d" $1
                echo "* $current_date Percona Build/Release Team <eng-build@percona.com> - ${VERSION}-${RPM_RELEASE}" >> $1
                echo "- Release ${VERSION}-${RPM_RELEASE}" >> $1
                echo >> $1
                return
            fi
        done <$1
    fi
}

get_sources(){
    cd "${WORKDIR}"
    if [ "${SOURCE}" = 0 ]
    then
        echo "Sources will not be downloaded"
        return 0
    fi
    PRODUCT=percona-pg-stat-monitor${PG_RELEASE}
    PRODUCT_FULL=${PRODUCT}-${VERSION}

    echo "PRODUCT=${PRODUCT}" > pg-stat-monitor.properties
    echo "PRODUCT_FULL=${PRODUCT_FULL}" >> pg-stat-monitor.properties
    echo "VERSION=${VERSION}" >> pg-stat-monitor.properties
    echo "BRANCH_NAME=$(echo ${BRANCH} | awk -F '/' '{print $(NF)}')" >> pg-stat-monitor.properties
    echo "BUILD_NUMBER=${BUILD_NUMBER}" >> pg-stat-monitor.properties
    echo "BUILD_ID=${BUILD_ID}" >> pg-stat-monitor.properties
    echo "BRANCH_NAME=$(echo ${BRANCH} | awk -F '/' '{print $(NF)}')" >> pg-stat-monitor.properties
    echo "PG_RELEASE=${PG_RELEASE}" >> pg-stat-monitor.properties
    echo "RPM_RELEASE=${RPM_RELEASE}" >> pg-stat-monitor.properties
    echo "DEB_RELEASE=${DEB_RELEASE}" >> pg-stat-monitor.properties
    git clone "$REPO" ${PRODUCT_FULL}
    retval=$?
    if [ $retval != 0 ]
    then
        echo "There were some issues during repo cloning from github. Please retry one more time"
        exit 1
    fi
    cd ${PRODUCT_FULL}
    if [ ! -z "$BRANCH" ]
    then
        git reset --hard
        git clean -xdf
        git checkout "$BRANCH"
    fi
    REVISION=$(git rev-parse --short HEAD)
    echo "REVISION=${REVISION}" >> ${WORKDIR}/pg-stat-monitor.properties

    EDITFILES="debian/control debian/control.in debian/rules rpm/pg-stat-monitor.spec"
    for file in $EDITFILES; do
        sed -i "s:@@PG_REL@@:${PG_RELEASE}:g" "$file"
    done

    sed -i "s:@@RPM_RELEASE@@:${RPM_RELEASE}:g" rpm/pg-stat-monitor.spec
    sed -i "s:@@VERSION@@:${VERSION}:g" rpm/pg-stat-monitor.spec

    set_changelog rpm/pg-stat-monitor.spec

    cd ${WORKDIR}
    #
    source pg-stat-monitor.properties
    #
    tar --owner=0 --group=0 --exclude=.* -czf ${PRODUCT_FULL}.tar.gz ${PRODUCT_FULL}
    echo "UPLOAD=UPLOAD/experimental/BUILDS/${PRODUCT}/${PRODUCT_FULL}/${BRANCH}/${REVISION}/${BUILD_ID}" >> pg-stat-monitor.properties
    mkdir $WORKDIR/source_tarball
    mkdir $CURDIR/source_tarball
    cp ${PRODUCT_FULL}.tar.gz $WORKDIR/source_tarball
    cp ${PRODUCT_FULL}.tar.gz $CURDIR/source_tarball
    cd $CURDIR
    rm -rf percona-pg-stat-monitor*
    return
}

get_system(){
    if [ -f /etc/redhat-release ]; then
        GLIBC_VER_TMP="$(rpm glibc -qa --qf %{VERSION})"
        export RHEL=$(rpm --eval %rhel)
        export ARCH=$(echo $(uname -m) | sed -e 's:i686:i386:g')
        export OS_NAME="el$RHEL"
        export OS="rpm"
    else
        GLIBC_VER_TMP="$(dpkg-query -W -f='${Version}' libc6 | awk -F'-' '{print $1}')"
        export ARCH=$(uname -m)
        export OS_NAME="$(lsb_release -sc)"
        export OS="deb"
    fi
    return
}

install_deps() {
    if [ $INSTALL = 0 ]
    then
        echo "Dependencies will not be installed"
        return;
    fi
    if [ $( id -u ) -ne 0 ]
    then
        echo "It is not possible to instal dependencies. Please run as root"
        exit 1
    fi
    CURPLACE=$(pwd)
    if [ "$OS" == "rpm" ]
    then
        yum install -y https://repo.percona.com/yum/percona-release-latest.noarch.rpm
        if [[ ${PG_RELEASE} == "11" ]]; then
            percona-release enable ppg-11 release
        elif [[ $PG_RELEASE == "12" ]]; then
            percona-release enable ppg-12 release
        fi
        yum -y install git wget
        PKGLIST="percona-postgresql${PG_RELEASE}-devel"
        PKGLIST+=" clang-devel git clang llvm-devel rpmdevtools vim wget"
        PKGLIST+=" perl binutils gcc gcc-c++"
        PKGLIST+=" clang-devel llvm-devel git rpm-build rpmdevtools wget gcc make autoconf"
        if [[ "${RHEL}" -ge 8 ]]; then 
            dnf -y module disable postgresql || true
        elif [[ "${RHEL}" -eq 7 ]]; then
            PKGLIST+=" llvm-toolset-7-clang llvm-toolset-7-llvm-devel llvm5.0-devel"
            until yum -y install epel-release centos-release-scl; do
                yum clean all
                sleep 1
                echo "waiting"
            done
            until yum -y makecache; do
                yum clean all
                sleep 1
                echo "waiting"
            done
        fi
        until yum -y install ${PKGLIST}; do
            echo "waiting"
            sleep 1
        done
    else
        apt-get update
        DEBIAN_FRONTEND=noninteractive apt-get -y install lsb-release gnupg git wget

        wget https://repo.percona.com/apt/percona-release_latest.$(lsb_release -sc)_all.deb && dpkg -i percona-release_latest.$(lsb_release -sc)_all.deb
        if [[ "${PG_RELEASE}" == "11" ]]; then
            percona-release enable ppg-11 release
        elif [[ "${PG_RELEASE}" == "12" ]]; then
            percona-release enable ppg-12 release
        fi


        PKGLIST="percona-postgresql-${PG_RELEASE} percona-postgresql-common percona-postgresql-server-dev-all"

        # ---- using a community version of postgresql
        #wget --quiet -O - https://www.postgresql.org/media/keys/ACCC4CF8.asc | sudo apt-key add -
        #echo "deb http://apt.postgresql.org/pub/repos/apt/ ${PG_RELEASE}"-pgdg main | sudo tee  /etc/apt/sources.list.d/pgdg.list
        #PKGLIST="postgresql-${PG_RELEASE} postgresql-common postgresql-server-dev-all"

        apt-get update

        if [[ "${OS_NAME}" != "focal" ]]; then
            LLVM_EXISTS=$(grep -c "apt.llvm.org" /etc/apt/sources.list)
            if [ "${LLVM_EXISTS}" == 0 ]; then
                wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key|sudo apt-key add -
                echo "deb http://apt.llvm.org/${OS_NAME}/ llvm-toolchain-${OS_NAME}-7 main" >> /etc/apt/sources.list
                echo "deb-src http://apt.llvm.org/${OS_NAME}/ llvm-toolchain-${OS_NAME}-7 main" >> /etc/apt/sources.list
                apt-get update
            fi
        fi

        PKGLIST+=" debconf debhelper clang-7 devscripts dh-exec dh-systemd git wget libkrb5-dev libssl-dev"
        PKGLIST+=" build-essential debconf debhelper devscripts dh-exec dh-systemd git wget libxml-checker-perl"
        PKGLIST+=" libxml-libxml-perl libio-socket-ssl-perl libperl-dev libssl-dev libxml2-dev txt2man zlib1g-dev libpq-dev"

        until DEBIAN_FRONTEND=noninteractive apt-get -y install ${PKGLIST}; do
            sleep 1
            echo "waiting"
        done

    fi
    return;
}

get_tar(){
    TARBALL=$1
    TARFILE=$(basename $(find $WORKDIR/$TARBALL -name 'percona-pg-stat-monitor*.tar.gz' | sort | tail -n1))
    if [ -z $TARFILE ]
    then
        TARFILE=$(basename $(find $CURDIR/$TARBALL -name 'percona-pg-stat-monitor*.tar.gz' | sort | tail -n1))
        if [ -z $TARFILE ]
        then
            echo "There is no $TARBALL for build"
            exit 1
        else
            cp $CURDIR/$TARBALL/$TARFILE $WORKDIR/$TARFILE
        fi
    else
        cp $WORKDIR/$TARBALL/$TARFILE $WORKDIR/$TARFILE
    fi
    return
}

get_deb_sources(){
    param=$1
    echo $param
    FILE=$(basename $(find $WORKDIR/source_deb -name "percona-pg-stat-monitor*.$param" | sort | tail -n1))
    if [ -z $FILE ]
    then
        FILE=$(basename $(find $CURDIR/source_deb -name "percona-pg-stat-monitor*.$param" | sort | tail -n1))
        if [ -z $FILE ]
        then
            echo "There is no sources for build"
            exit 1
        else
            cp $CURDIR/source_deb/$FILE $WORKDIR/
        fi
    else
        cp $WORKDIR/source_deb/$FILE $WORKDIR/
    fi
    return
}

build_srpm(){
    if [ $SRPM = 0 ]
    then
        echo "SRC RPM will not be created"
        return;
    fi
    if [ "x$OS" = "xdeb" ]
    then
        echo "It is not possible to build src rpm here"
        exit 1
    fi
    cd $WORKDIR
    get_tar "source_tarball"
    rm -fr rpmbuild
    ls | grep -v tar.gz | xargs rm -rf
    TARFILE=$(find . -name 'percona-pg-stat-monitor*.tar.gz' | sort | tail -n1)
    SRC_DIR=${TARFILE%.tar.gz}
    #
    mkdir -vp rpmbuild/{SOURCES,SPECS,BUILD,SRPMS,RPMS}
    tar vxzf ${WORKDIR}/${TARFILE} --wildcards '*/rpm' --strip=1
    #
    cp -av rpm/* rpmbuild/SOURCES
    cp -av rpm/*.spec rpmbuild/SPECS
    #
    mv -fv ${TARFILE} ${WORKDIR}/rpmbuild/SOURCES
    rpmbuild -bs --define "_topdir ${WORKDIR}/rpmbuild" --define "dist .generic" \
        --define "version ${VERSION}" rpmbuild/SPECS/pg-stat-monitor.spec
    mkdir -p ${WORKDIR}/srpm
    mkdir -p ${CURDIR}/srpm
    cp rpmbuild/SRPMS/*.src.rpm ${CURDIR}/srpm
    cp rpmbuild/SRPMS/*.src.rpm ${WORKDIR}/srpm
    return
}

build_rpm(){
    if [ $RPM = 0 ]
    then
        echo "RPM will not be created"
        return;
    fi
    if [ "x$OS" = "xdeb" ]
    then
        echo "It is not possible to build rpm here"
        exit 1
    fi
    SRC_RPM=$(basename $(find $WORKDIR/srpm -name 'percona-pg_stat_monitor*.src.rpm' | sort | tail -n1))
    if [ -z $SRC_RPM ]
    then
        SRC_RPM=$(basename $(find $CURDIR/srpm -name 'percona-pg_stat_monitor*.src.rpm' | sort | tail -n1))
        if [ -z $SRC_RPM ]
        then
            echo "There is no src rpm for build"
            echo "You can create it using key --build_src_rpm=1"
            exit 1
        else
            cp $CURDIR/srpm/$SRC_RPM $WORKDIR
        fi
    else
        cp $WORKDIR/srpm/$SRC_RPM $WORKDIR
    fi
    cd $WORKDIR
    rm -fr rpmbuild
    mkdir -vp rpmbuild/{SOURCES,SPECS,BUILD,SRPMS,RPMS}
    cp $SRC_RPM rpmbuild/SRPMS/

    cd rpmbuild/SRPMS/
    #
    cd $WORKDIR
    RHEL=$(rpm --eval %rhel)
    ARCH=$(echo $(uname -m) | sed -e 's:i686:i386:g')
    if [ -f /opt/rh/devtoolset-7/enable ]; then
        source /opt/rh/devtoolset-7/enable
        source /opt/rh/llvm-toolset-7/enable
    fi
    export LIBPQ_DIR=/usr/pgsql-${PG_RELEASE}/
    export LIBRARY_PATH=/usr/pgsql-${PG_RELEASE}/lib/:/usr/pgsql-${PG_RELEASE}/include/
    rpmbuild --define "_topdir ${WORKDIR}/rpmbuild" --define "dist .$OS_NAME" --define "version ${VERSION}" --rebuild rpmbuild/SRPMS/$SRC_RPM

    return_code=$?
    if [ $return_code != 0 ]; then
        exit $return_code
    fi
    mkdir -p ${WORKDIR}/rpm
    mkdir -p ${CURDIR}/rpm
    cp rpmbuild/RPMS/*/*.rpm ${WORKDIR}/rpm
    cp rpmbuild/RPMS/*/*.rpm ${CURDIR}/rpm
}

build_source_deb(){
    if [ $SDEB = 0 ]
    then
        echo "source deb package will not be created"
        return;
    fi
    if [ "x$OS" = "xrpm" ]
    then
        echo "It is not possible to build source deb here"
        exit 1
    fi
    rm -rf percona-pg-stat-monitor*
    get_tar "source_tarball"
    rm -f *.dsc *.orig.tar.gz *.debian.tar.gz *.changes
    #
    TARFILE=$(basename $(find . -name 'percona-pg-stat-monitor*.tar.gz' | sort | tail -n1))
    DEBIAN=$(lsb_release -sc)
    ARCH=$(echo $(uname -m) | sed -e 's:i686:i386:g')
    tar zxf ${TARFILE}
    BUILDDIR=${TARFILE%.tar.gz}
    #

    mv ${TARFILE} percona-pg-stat-monitor_${VERSION}.orig.tar.gz
    cd ${BUILDDIR}

    dch -D unstable --force-distribution -v "${VERSION}-${DEB_RELEASE}" "Update to new percona-pg-stat-monitor${PG_RELEASE} version ${VERSION}"
    pg_buildext updatecontrol
    dpkg-buildpackage -S
    cd ../
    mkdir -p $WORKDIR/source_deb
    mkdir -p $CURDIR/source_deb
    cp *.debian.tar.* $WORKDIR/source_deb
    cp *_source.changes $WORKDIR/source_deb
    cp *.dsc $WORKDIR/source_deb
    cp *.orig.tar.gz $WORKDIR/source_deb
    cp *.debian.tar.* $CURDIR/source_deb
    cp *_source.changes $CURDIR/source_deb
    cp *.dsc $CURDIR/source_deb
    cp *.orig.tar.gz $CURDIR/source_deb
}

build_deb(){
    if [ $DEB = 0 ]
    then
        echo "source deb package will not be created"
        return;
    fi
    if [ "x$OS" = "xrmp" ]
    then
        echo "It is not possible to build source deb here"
        exit 1
    fi
    for file in 'dsc' 'orig.tar.gz' 'changes' 'debian.tar*'
    do
        get_deb_sources $file
    done
    cd $WORKDIR
    rm -fv *.deb
    #
    export ARCH=$(echo $(uname -m) | sed -e 's:i686:i386:g')
    #
    echo "DEBIAN=${OS_NAME}" >> pg-stat-monitor.properties
    echo "ARCH=${ARCH}" >> pg-stat-monitor.properties
    #
    DSC=$(basename $(find . -name '*.dsc' | sort | tail -n1))
    #
    dpkg-source -x ${DSC}
    #
    cd percona-pg-stat-monitor-${VERSION}
    sed -i "s:\. :${WORKDIR}/percona-pg-stat-monitor-${VERSION} :g" debian/rules
    dch -m -D "${OS_NAME}" --force-distribution -v "1:${VERSION}-${DEB_RELEASE}.${OS_NAME}" 'Update distribution'
    unset $(locale|cut -d= -f1)
    pg_buildext updatecontrol
    dpkg-buildpackage -rfakeroot -us -uc -b
    mkdir -p $CURDIR/deb
    mkdir -p $WORKDIR/deb
    cp $WORKDIR/*.*deb $WORKDIR/deb
    cp $WORKDIR/*.*deb $CURDIR/deb
}

CURDIR=$(pwd)
VERSION_FILE=$CURDIR/percona-pg-stat-monitor.properties
args=
WORKDIR=
SRPM=0
SDEB=0
RPM=0
DEB=0
SOURCE=0
TARBALL=0
OS_NAME=
ARCH=
OS=
REVISION=0
BRANCH="master"
INSTALL=0
RPM_RELEASE=1
DEB_RELEASE=1
REPO="https://github.com/Percona/pg_stat_monitor.git"
VERSION="1.0.0"
PG_RELEASE=11
parse_arguments PICK-ARGS-FROM-ARGV "$@"

check_workdir
get_system
install_deps
get_sources
build_srpm
build_source_deb
build_rpm
build_deb
