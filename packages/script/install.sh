#!/bin/bash
useradd -r yugabyte
chown -R yugabyte:yugabyte /opt/yugabyte
cd /opt/yugabyte/
./bin/post_install.sh
mkdir -p /var/log/yugabyte-logs/
mv /opt/yugabyte/etc/yugabyte /etc
rm -rf /opt/yugabyte/etc
ln -s /opt/yugabyte/yugabyted /usr/bin/yugabyted
rm -f /opt/yugabyte/install.sh
