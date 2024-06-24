---
title: Install YugabyteDB Anywhere software - Airgapped
headerTitle: Install YugabyteDB Anywhere software - Airgapped
linkTitle: Install software
description: Install YugabyteDB Anywhere software in your on-premises, airgapped environment.
menu:
  v2.16_yugabyte-platform:
    parent: install-yugabyte-platform
    identifier: install-software-3-airgapped
    weight: 77
type: docs
---
<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../default/" class="nav-link">
      <i class="fa-solid fa-cloud"></i>Default</a>
  </li>

  <li>
    <a href="../kubernetes/" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>Kubernetes</a>
  </li>

  <li>
    <a href="../airgapped/" class="nav-link active">
      <i class="fa-solid fa-link-slash"></i>Airgapped</a>
  </li>

  <li>
    <a href="../openshift/" class="nav-link">
      <i class="fa-brands fa-redhat"></i>OpenShift</a>
  </li>

</ul>

## Prerequisites

If Docker is not installed on the host computer, you need to install a recent version that matches the minimum requirements outlined in [Installing Docker in Airgapped Environments](https://community.replicated.com/t/installing-docker-in-airgapped-environments/81).

If access to the Docker repositories for your Linux distribution is not available on the host computer, you may have to manually transfer the necessary RPM or DEB packages whose locations are specified in [Installing Docker in Airgapped Environments](https://community.replicated.com/t/installing-docker-in-airgapped-environments/81).

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
  wget https://downloads.yugabyte.com/releases/{{<yb-version version="v2.16">}}/yugaware-{{<yb-version version="v2.16" format="build">}}-linux-x86_64.airgap
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

## Set Up HTTPS (optional)

Launch the Replicated UI via [http://yugaware-host-public-ip:8800](http://yugaware-host-public-ip:8800). Expect to see a warning stating that the connection to the server is not yet private. This condition is resolved once HTTPS for the Replicated Admin Console is set up in the next step. Proceed by clicking **Continue to Setup** **>** **ADVANCED** to bypass the warning and access the **Replicated Admin Console**, as shown in the following illustration:

![Replicated SSL warning](/images/replicated/replicated-warning.png)

You can provide your own custom SSL certificate and a hostname, as shown in the following illustration:

![Replicated HTTPS setup](/images/replicated/replicated-https.png)

It is recommended that you start with using a self-signed certificate, and then add the custom SSL certificate later. Note that in this case you connect to the Replicated Admin Console using an IP address, as shown in the following illustration:

![Replicated Self Signed Cert](/images/replicated/replicated-selfsigned.png)

## Upload the License File

Upload the Yugabyte license file that you received from Yugabyte Support, as shown in the following illustration:

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

## Set up HTTP/HTTPS proxy

YugabyteDB Anywhere sometimes initiates HTTP or HTTPS connections to other servers. For example, HTTP or HTTPS connections (depending on your setup) can be used to do the following, or more:

- Contact a public cloud provider to create VMs.
- Deposit backups on a public cloud provider's object storage service.
- Contact an external load balancer.

You can set up YBA to use an HTTP/HTTPS proxy server via **Application config**, and select **Enable Proxy** as per the following illustration:

![Enable Proxy](/images/replicated/enable-proxy.png)

When completing the **Enable Proxy** settings, keep in mind the following:

- If your proxy is using the default ports for each protocol, then set the ports for the HTTP and HTTPS proxies to the default, 80 and 443 respectively, instead of 8080 and 8443 as shown in the preceding illustration.

- If you have only one proxy set up (HTTP or HTTPS), then set the same values for both.

- This configuration sets operating system environment variables and Java system properties. The help text for each field shows which Java system property or environment variable gets set by the field. System properties have the "-D" prefix. For example "Specify -Dhttps.proxyPort".

- The **no proxy** fields (HTTP no proxy setting, HTTP no proxy setting for Java) are lists of exception hosts, provided as a comma-delimited list of addresses or hostnames. Include the following addresses:
  - The Docker gateway address (172.17.0.1 by default).
  - The address of any previously-specified web proxy.
  - Any other IP addresses that you deem safe to bypass the proxy.

- These settings comprehensively govern all network connections that YBA initiates. For example, if you specify a proxy server for HTTP, all unencrypted connections initiated by YBA will be affected. If you want YBA to bypass the proxy server when connecting to database universe nodes, then you must explicitly specify the database universe node IP addresses as exception hosts (also known as "no proxy").

- Because some YBA network connections are driven by YBA's Java process, while others are driven outside of Java (for example, via Python or a Linux shell execution), each (Java and non-Java) has its own separate configurable parameters.

- The Java fields can accept values as Java system properties, including the use of pipe ("|") as a field separator. Refer to [Java Networking and Proxies](https://docs.oracle.com/javase/8/docs/technotes/guides/net/proxies.html) for more details about the properties.

- YugabyteDB Anywhere follows community standards for setting proxy environment variables, where two environment variables are exported with lowercase and uppercase names. For example, if you enter "http://my.Proxy.host:8080" for **HTTP Proxy setting**, then two environment variables are exported as follows:

    ```sh
    HTTP_PROXY = http://my.Proxy.host:8080
    http_proxy = http://my.Proxy.host:8080
    ```
