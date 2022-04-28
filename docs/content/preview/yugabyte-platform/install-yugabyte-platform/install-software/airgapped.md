---
title: Install YugabyteDB Anywhere software - Airgapped
headerTitle: Install YugabyteDB Anywhere software - Airgapped
linkTitle: Install software
description: Install YugabyteDB Anywhere software in your on-premises, airgapped environment.
menu:
  preview:
    parent: install-yugabyte-platform
    identifier: install-software-3-airgapped
    weight: 77
isTocNested: true
showAsideToc: true
---
<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../default/" class="nav-link">
      <i class="fas fa-cloud"></i>Default</a>
  </li>

  <li>
    <a href="../kubernetes/" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>Kubernetes</a>
  </li>

  <li>
    <a href="../airgapped/" class="nav-link active">
      <i class="fas fa-unlink"></i>Airgapped</a>
  </li>

  <li>
    <a href="../openshift/" class="nav-link">
      <i class="fas fa-cubes"></i>OpenShift</a>
  </li>

</ul>

## Install Replicated

On a computer connected to the Internet, perform the following steps:

- Make a directory for downloading the binaries by executing the following command:

  ```sh
  sudo mkdir /opt/downloads
  ```

- Change the owner user for the directory by executing the following command:

  ```sh
  sudo chown -R ubuntu:ubuntu /opt/downloads
  ```

- Change to the directory by executing the following command:

  ```sh
  cd /opt/downloads
  ```

- Download the `replicated.tar.gz` file by executing the following command:

  ```sh
  wget --trust-server-names https://get.replicated.com/airgap
  ```

- Download the `yugaware` binary and change the following number, as required:

  ```sh
  wget https://downloads.yugabyte.com/releases/{{< yb-version version="preview">}}/yugaware-{{< yb-version version="preview" format="build">}}-linux-x86_64.airgap
  ```

- Switch to the following directory:

  ```sh
  cd /opt/downloads
  ```

- Extract the `replicated` binary, as follows:

  ```sh
  tar xzvf replicated.tar.gz
  ```

- Install Replicated. If multiple options appear, select the `eth0` network interface, as follows:

  ```sh
  cat ./install.sh | sudo bash -s airgap
  ```

The `yugaware` binary is installed using the Replicated UI after the Replicated installation completes.

After Replicated finishes installing, ensure that it is running by executing the following command:

```sh
sudo docker ps
```

You should see an output similar to the following:


![Replicated successfully installed](/images/replicated/replicated-success.png)

The next step is to install YugabyteDB Anywhere.

## Install required packages

Liza: the following might need to replace the bullet point "Having YugabyteDB Anywhere airgapped install package. Contact Yugabyte Support for more information." In https://docs.yugabyte.com/preview/yugabyte-platform/install-yugabyte-platform/prerequisites/#airgapped-hosts



You need to install a number of packages using the package manager. Since the cluster nodes are airgapped, they do not have access to package repositories, and hence airgapped cluster creation fails. For now, we should provide a list of packages that should be installed either on the AMI, or available on an  accessible package repository, for installation to work. Below is a list of the necessary packages, along with some extra information about when it is necessary. All of this applies to 2.13.1.

1. Chrony (if the user toggles Use TimeSync on during universe creation)
2. Python-minimal (only on Ubuntu 18.04)
3. Python-setuptools (only on Ubuntu 18.04)
4. Python-six/Python2-six (the Python2 version of Six)
5. policycoreutils-python (only on CentOS 7 and Oracle Linux 8)
6. selinux-policy (only on Oracle Linux 8, **must be on an accessible package repository**)
7. locales (only on Ubuntu)

In 2.12, there is an additional requirement:

1. ntpd (if the user toggles Use TimeSync off during universe creation)

Some of these may change over time as we clean up some of these requirements like Python 2, I'll update accordingly.

## Set Up HTTPS (optional)

Launch the Replicated UI via [http://yugaware-host-public-ip:8800](http://yugaware-host-public-ip:8800). Expect to see a warning stating that the connection to the server is not yet private. This condition is resolved once HTTPS for the Replicated Admin Console is set up in the next step. Proceed by clicking **Continue to Setup** **>** **ADVANCED** to bypass the warning and access the **Replicated Admin Console**, as shown in the following illustration:

![Replicated SSL warning](/images/replicated/replicated-warning.png)

You can provide your own custom SSL certificate and a hostname, as shown in the following illustration:

![Replicated HTTPS setup](/images/replicated/replicated-https.png)

It is recommended that you start with using a self-signed certificate, and then add the custom SSL certificate later. Note that in this case you connect to the Replicated Admin Console using an IP address, as shown in the following illustration:

![Replicated Self Signed Cert](/images/replicated/replicated-selfsigned.png)

## Upload the License File

Upload the Yugabyte license file that you received from [Yugabyte](https://www.yugabyte.com/platform/#request-trial-form), as shown in the following illustration:

![Replicated License Upload](/images/replicated/replicated-license-upload.png)

Two options to install YugabyteDB Anywhere are presented as shown in the following illustrations:

![Replicated License Air-gapped Install](/images/replicated/replicated-license-airgapped-install-option.png)

![Replicated License Air-gapped Path](/images/replicated/replicated-license-airgapped-path.png)

![Replicated License Air-gapped Progress](/images/replicated/replicated-license-airgapped-progress.png)

## Secure Replicated

Add a password to protect the Replicated Admin Console, which is different from the Admin Console for YugabyteDB used by YugabyteDB Anywhere, as shown in the following illustration:

![Replicated Password](/images/replicated/replicated-password.png)

## Preflight checks

Replicated performs a set of preflight checks to ensure that the host is set up correctly for YugabyteDB Anywhere, as shown in the following illustration:

![Replicated Checks](/images/replicated/replicated-checks.png)

Click **Continue** to configure YugabyteDB Anywhere.

If the preflight check fails, review the [Troubleshoot YugabyteDB Anywhere](../../../troubleshoot/) to resolve the issue.

## Set the TLS Version for Nginx Frontend

Specify TLS versions via **Application config**, as shown in the following illustration:

![Application Config](/images/replicated/application-config.png)

The recommended TLS version is 1.2.
