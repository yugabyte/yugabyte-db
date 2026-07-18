#!/usr/bin/env bash

pushd $(dirname $0)

# Install jq if not present.
# command -v jq >/dev/null 2>&1
# if [ $? -ne 0 ];
# then
#   echo "Installing jq package"

#   . ./detect_os.sh

#   if [[ $OS == "Ubuntu" || $OS == "Debian" || $OS == "Pop!_OS" ]]; then
#     sudo apt-get -y install jq
#   elif [[ $OS == "CentOS Linux" ]]; then
#     sudo yum install epel-release -y
#     sudo yum install jq -y
#   else
#     echo "Non-supported distribution, OS version detected was $OS"
#     exit 1
#   fi
# else
#   echo "jq package already installed"
# fi

command -v yq >/dev/null 2>&1
if [ $? -ne 0 ];
then
  echo "Installing yq package"
  sudo wget https://github.com/mikefarah/yq/releases/download/v4.19.1/yq_linux_386 -O /usr/bin/yq
  sudo chmod +x /usr/bin/yq
else
  echo "yq package already installed"
fi
