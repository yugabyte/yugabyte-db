#!/bin/bash
set -euo pipefail

function print_usage {
  echo "Usage: ./install.sh version"
  printf "Available versions..:\t"
  for package in `ls yugabundle-*`
  do
    release=`echo $package | cut -d- -f2`
    build=`echo $package | cut -d- -f3 | cut -d. -f1`
    printf "$release-$build\t"
  done
  printf "\n"
  exit 1
}

if [ $# -lt 1 ]; then
  echo "Please specify a valid version."
  print_usage
fi

if [ "$1" == "--help" ]; then
  print_usage
fi

version=$1
package=`ls yugabundle-${version}.tar.gz`
package_folder=`tar tf ${package} | sed -e 's@/.*@@' | uniq`
install_path=/opt/yugabyte

echo "Create necessary directories"
mkdir -p ${install_path}/releases/$version
mkdir -p ${install_path}/swamper_targets
mkdir -p ${install_path}/data
mkdir -p ${install_path}/third-party

echo "Unpacking package ${package} inside ${package_folder}"
tar -xvf $package

echo "Create Devops and Yugaware directories"
mkdir -p ${package_folder}/devops
mkdir -p ${package_folder}/yugaware

echo "Untarring devops package"
tar -xf ${package_folder}/devops*.tar.gz -C ./${package_folder}/devops
echo "Untarring yugaware package"
tar -xf ${package_folder}/yugaware*.tar.gz -C ./${package_folder}/yugaware

echo "Copy yugabyte release file"
cp ${package_folder}/yugabyte*.tar.gz ${install_path}/releases/$version

echo "Installing devops environment"
tar -xf ./${package_folder}/devops/yb_platform_virtualenv.tar.gz -C ./${package_folder}/devops || \
   ./${package_folder}/devops/bin/install_python_requirements.sh --use_package

if [ -L ${install_path}/yugaware ]; then
 echo "Renaming current symlink"
 mv ${install_path}/yugaware ${install_path}/yugaware_backup
fi
if [ -L ${install_path}/devops ]; then
 echo "Renaming current symlink"
 mv ${install_path}/devops ${install_path}/devops_backup
fi

echo "Creating symlinks"
ln -s $(pwd)/${package_folder}/yugaware ${install_path}/yugaware
ln -s $(pwd)/${package_folder}/devops ${install_path}/devops
