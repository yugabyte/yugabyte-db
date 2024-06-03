
$pg12_script = <<SCRIPT
  add-apt-repository --yes "deb [trusted=yes] https://apt.postgresql.org/pub/repos/apt/ $(lsb_release -s -c)-pgdg-snapshot main 16"
  apt-get update
  apt-get install -y postgresql-12 postgresql-server-dev-12 make gcc
  sudo -u postgres createdb foo
SCRIPT

$pg14_script = <<SCRIPT
  add-apt-repository --yes "deb [trusted=yes] https://apt.postgresql.org/pub/repos/apt/ $(lsb_release -s -c)-pgdg-snapshot main 16"
  apt-get update
  apt-get install -y postgresql-14 postgresql-server-dev-14 make gcc
  sudo -u postgres createdb foo
SCRIPT

$pg16_script = <<SCRIPT
  dnf install -y https://download.postgresql.org/pub/repos/yum/reporpms/EL-9-x86_64/pgdg-redhat-repo-latest.noarch.rpm
  dnf -y install yum-utils
  yum-config-manager --enable pgdg16-updates-testing
  dnf -y install gcc ccache redhat-rpm-config postgresql16-server postgresql16-contrib postgresql16-devel
  export PATH="/usr/pgsql-16/bin:$PATH"
SCRIPT


Vagrant.configure("2") do |config|

  config.vm.define "pg12" do |pg12|
    pg12.vm.box = "ubuntu/jammy64"
    pg12.vm.hostname = "pg12"
    pg12.vm.network :forwarded_port, guest: 5432, host: 54312
    pg12.vm.provision "shell", inline: $pg12_script
  end

  config.vm.define "pg14" do |pg14|
    pg14.vm.box = "ubuntu/jammy64"
    pg14.vm.hostname = "pg14"
    pg14.vm.network :forwarded_port, guest: 5432, host: 54314
    pg14.vm.provision "shell", inline: $pg14_script
  end

  config.vm.define "pg16" do |pg16|
    pg16.vm.box = "CrunchyData/rocky9"
    pg16.vm.hostname = "pg16"
    pg16.vm.network :forwarded_port, guest: 5432, host: 54316
    pg16.vm.provision "shell", inline: $pg16_script
  end

end

