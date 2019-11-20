#!/bin/bash
chmod 775 -R /opt/yugabyte-2.0.5.2
cd /opt/yugabyte-2.0.5.2/

mkdir -p /var/log/yugabyte-logs/
mv /opt/yugabyte-2.0.5.2/etc/yugabyte /etc

rm -rf /opt/yugabyte-2.0.5.2/etc
ln -s /opt/yugabyte-2.0.5.2/yugabyted /usr/bin/yugabyted
rm -f /opt/yugabyte-2.0.5.2/install.sh
