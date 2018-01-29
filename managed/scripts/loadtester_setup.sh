# This file has the list of steps to taken to create a loadtester machine to be used by perf itest. First some manual steps:
# Of course, can to automate it, but it should be a very rare case to create one...
#
# To create a machine on GCP, for example:
#   ~/code/devops/bin/ybcloud.sh gcp --region us-west1 --zone us-west1-a instance provision
#        --instance_type n1-standard-16 --cloud_subnet subnet-us-west1 --assign_public_ip
#        loadtester-1a --num_volumes 1 --local_package_path /opt/third-party
#
# Then ssh into that machine and then create ssh key which can be copied to bitbucket and github
# ssh-keygen -t rsa -b 4096 -C "<id>@yugabyte.com"
#
# Java install: https://tecadmin.net/install-java-8-on-centos-rhel-and-fedora/
# Maven install: https://tecadmin.net/install-apache-maven-on-centos/
#

sudo yum -y install git
sudo yum -y install emacs
sudo yum -y install mvn
sudo yum -y install wget
mkdir ~/code
cd ~/code
mkdir ycsb-yb
cd ycsb-yb
git clone git@bitbucket.org:yugabyte/ycsb-yb.git .
cd ..
mkdir cqlsh
cd cqlsh
git clone git@github.com:YugaByte/cqlsh.git .

sudo wget --no-cookies --no-check-certificate --header "Cookie: gpw_e24=http%3A%2F%2Fwww.oracle.com%2F; oraclelicense=accept-securebackup-cookie" "http://download.oracle.com/otn-pub/java/jdk/8u161-b12/2f38c3b165be4555a1fa6e98c45e0808/jdk-8u161-linux-x64.tar.gz"
sudo alternatives --install /usr/bin/java java /opt/jdk1.8.0_161/bin/java 2
sudo alternatives --config java
sudo alternatives --install /usr/bin/javac javac /opt/jdk1.8.0_161/bin/javac 2
sudo alternatives --set javac /opt/jdk1.8.0_161/bin/javac

sudo wget http://www-eu.apache.org/dist/maven/maven-3/3.5.2/binaries/apache-maven-3.5.2-bin.tar.gz
sudo tar xzf apache-maven-3.5.2-bin.tar.gz
sudo ln -s apache-maven-3.5.2  maven
sudo cat "export M2_HOME=/usr/local/maven" > /etc/profile.d/maven.sh
sudo cat "export PATH=${M2_HOME}/bin:${PATH}" >> /etc/profile.d/maven.sh
source /etc/profile.d/maven.sh


# Then manually copy ./loadtest_settings.xml  to ~.m2/settings.xml on the new machine.

