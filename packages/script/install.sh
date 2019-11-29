#!/bin/bash
set -e -x
getent passwd yugabyte > /dev/null 

if [ $? -eq 0 ]; then
    echo "Yugabyte user exists"
else
    useradd -r yugabyte
fi

if [ -d "/opt/yugabyte/" ]; then
  chown -R yugabyte:yugabyte /opt/yugabyte
  cd /opt/yugabyte/
  ./bin/post_install.sh
  mkdir -p /var/log/yugabyte/
  mv /opt/yugabyte/etc/yugabyte /etc
  rm -rf /opt/yugabyte/etc
  ln -s /opt/yugabyte/yugabyted /usr/bin/yugabyted
  ln -s /opt/yugabyte/bin/cqlsh /usr/bin/cqlsh
  ln -s /opt/yugabyte/postgres/bin/ysqlsh /usr/bin/ysqlsh
  rm -f /opt/yugabyte/install.sh
fi

