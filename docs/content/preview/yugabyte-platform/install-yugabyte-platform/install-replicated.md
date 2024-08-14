---
title: Install YugabyteDB Anywhere using Replicated
headerTitle: Install YugabyteDB Anywhere using Replicated
linkTitle: Install using Replicated
description: Install YugabyteDB Anywhere using Replicated.
headContent: Install YBA software using Replicated and Docker containers
aliases:
  - /preview/yugabyte-platform/install-yugabyte-platform/install-software/airgapped/
  - /preview/yugabyte-platform/install-yugabyte-platform/install-software/replicated/
menu:
  preview_yugabyte-platform:
    parent: migrate-replicated
    identifier: install-replicated
    weight: 40
type: docs
---

{{< warning title="Replicated end of life" >}}

YugabyteDB Anywhere will end support for Replicated installation at the end of 2024.

The following instructions are provided to support legacy installations. For new installations of YugabyteDB Anywhere, use YBA Installer. Refer to [Install YugabyteDB Anywhere](../install-software/installer/).

To migrate existing Replicated YugabyteDB Anywhere installations to YBA Installer, refer to [Migrate from Replicated](../migrate-replicated/).

{{< /warning >}}

You can install YugabyteDB Anywhere (YBA) on a host machine using Replicated in both online and airgapped environments.

Installation starts with installing Replicated on a Linux host. Replicated installs the [docker-engine](https://docs.docker.com/engine/), the Docker container runtime, and then pulls its own container images from the [Replicated.com container registry](https://help.replicated.com/docs/native/getting-started/docker-registries/). YugabyteDB Anywhere then becomes a managed application of Replicated, which starts by pulling the YugabyteDB Anywhere (`yugaware`) container images from Quay.io for the very first time.

## Prerequisites

Installing using Replicated requires the following:

- Ability to install and configure [docker-engine](https://docs.docker.com/engine/).
- Ability to install and configure [Replicated](https://www.replicated.com/install-options/), which is a containerized application itself and needs to pull containers from its own [Replicated.com container registry](https://help.replicated.com/docs/native/getting-started/docker-registries/).
- Ability to pull Yugabyte container images from the [Quay.io](https://quay.io/) container registry (this will be done by Replicated automatically).

### Supported Linux distributions

YugabyteDB Anywhere is supported on all Linux distributions that Replicated supports. This includes, but is not limited to the following:

- CentOS 7
- AlmaLinux 8
- AlmaLinux 9
- Ubuntu 18
- Ubuntu 20
- RedHat Enterprise Linux 7
- RedHat Enterprise Linux 8
- SUSE Linux Enterprise Server (SLES) 15 SP4 {{<badge/ea>}}

### Hardware requirements

A node running YugabyteDB Anywhere is expected to meet the following requirements:

- 4 cores
- 8 GB memory
- 265+ GB disk space (see following table)

| Path | Usage | Free Space Required |
| :--- | :--- | :--- |
| / | Assuming /opt/yugabyte shares a file system with / | 50 GB |
| /tmp | Used during Install and Upgrade | 10 GB<sup>1</sup> |
| /opt/yugabyte | Database configuration and Prometheus logs | 150+ GB<sup>1,2</sup> |
| /var/lib/docker | Running YBA components | 40 GB<sup>1</sup> |
| /var/lib/replicated | Images and staging | 15 GB<sup>1</sup> |

<sup>1</sup> Where two or more of these paths share a file system, the free space required on that file system is the sum of the free space requirements.

<sup>2</sup> YugabyteDB Anywhere installations managing many nodes, or universes with many tables may require more than 150 GB to retain Prometheus logs, depending on retention. Installations managing fewer objects may need as little as 50-100 GB for /opt/yugabyte.

### Prepare the host

YugabyteDB Anywhere uses Replicated scheduler for software distribution and container management. You need to ensure that the host can pull containers from the [Replicated Docker Registries](https://help.replicated.com/docs/native/getting-started/docker-registries/).

Replicated installs a compatible Docker version if it is not pre-installed on the host. The currently supported Docker version is 20.10.n.

#### Airgapped hosts

Installing YugabyteDB Anywhere on airgapped hosts (without access to any Internet traffic (inbound or outbound)) requires the following additional configuration:

- Whitelist endpoints: To install Replicated and YugabyteDB Anywhere on a host with no Internet connectivity, you have to first download the binaries on a computer that has Internet connectivity, and then copy the files over to the appropriate host. In case of restricted connectivity, whitelist the following endpoints to ensure that they are accessible from the host marked for installation:

  - `https://downloads.yugabyte.com`
  - `https://download.docker.com`

- Ensure that Docker Engine version 20.10.n is available. If it is not installed, you need to follow the procedure described in [Installing Docker in airgapped](https://community.replicated.com/t/installing-docker-in-airgapped-environments/81).

    If you want to set up Docker on Amazon Linux OS, perform the following:

    1. Install Docker using the following commands:

        ```sh
        sudo yum install docker
        sudo systemctl daemon-reload
        ```

    1. Start Docker using the following command:

        ```sh
        sudo systemctl start docker.service
        ```

- Ensure that the following ports are open on the YugabyteDB Anywhere host:
  - 8800 – HTTP access to the Replicated UI
  - 80 or 443 – HTTP and HTTPS access to the YugabyteDB Anywhere UI, respectively
  - 22 – SSH
- Obtain the YugabyteDB Anywhere airgapped install package. Contact {{% support-platform %}} for more information.
- Sign the Yugabyte license agreement. Contact {{% support-platform %}} for more information.

## Install Replicated

{{< tabpane text=true >}}

  {{% tab header="Online" lang="Online" %}}

The first step is to connect to the host instance and then install Replicated by executing the following command:

```sh
curl -sSL https://get.replicated.com/docker | sudo bash
```

If you are installing Replicated behind a proxy, you need to execute the following command:

```sh
curl -x http://<proxy_address>:<proxy_port> https://get.replicated.com/docker | sudo bash
```

After the Replicated installation completes, verify that it is running by executing the following command:

```sh
sudo docker ps --format "{{.ID}}: {{.Image}}: {{.Command}}: {{.Ports}}"
```

You should see an output similar to the following:

![Replicated successfully installed](/images/replicated/replicated-success.png)

  {{% /tab %}}

  {{% tab header="Airgapped" lang="Airgapped" %}}

If Docker is not installed on the host computer, you need to install a recent version that matches the minimum requirements outlined in [Installing Docker in Airgapped Environments](https://community.replicated.com/t/installing-docker-in-airgapped-environments/81).

If access to the Docker repositories for your Linux distribution is not available on the host computer, you may have to manually transfer the necessary RPM or DEB packages whose locations are specified in [Installing Docker in Airgapped Environments](https://community.replicated.com/t/installing-docker-in-airgapped-environments/81).

Refer to [Airgapped hosts](#airgapped-hosts) for more details on preparing your host machine.

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
  wget https://downloads.yugabyte.com/releases/{{<yb-version version="preview">}}/yugaware-{{<yb-version version="preview" format="build">}}-linux-x86_64.airgap
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

  {{% /tab %}}

{{< /tabpane >}}

## Set up HTTPS (optional)

Launch the Replicated UI via [http://yugaware-host-public-ip:8800](http://yugaware-host-public-ip:8800). Expect to see a warning stating that the connection to the server is not yet private. This condition is resolved once HTTPS for the Replicated Admin Console is set up in the next step. Proceed by clicking **Continue to Setup > ADVANCED** to bypass the warning and access the **Replicated Admin Console**, as shown in the following illustration:

![Replicated SSL warning](/images/replicated/replicated-warning.png)

You can provide your own custom SSL certificate and a hostname, as per the following illustration:

![Replicated HTTPS setup](/images/replicated/replicated-https.png)

It is recommended that you start with using a self-signed certificate, and then add the custom SSL certificate later. Note that in this case you connect to the Replicated Admin Console using an IP address, as per following illustration:

![Replicated Self Signed Cert](/images/replicated/replicated-selfsigned.png)

## Upload the license file

Upload the Yugabyte license file that you received from Yugabyte Support, as shown in the following illustration:

![Replicated License Upload](/images/replicated/replicated-license-upload.png)

When prompted to choose the installation type, do one of the following:

- **Online** - If you are performing an online installation, choose the **Online** installation type and click **Continue**. If you are offered a choice of software versions, select the one that meets your requirements.

- **Airgapped** - If you are performing an airgapped installation, choose the **Airgapped** installation type, enter the absolute path to the YugabyteDB Anywhere airgapped install package that you obtained from Yugabyte Support, and click **Continue**.

## Secure Replicated

Add a password to protect the Replicated Admin Console, which is different from the Admin Console for YugabyteDB used by YugabyteDB Anywhere, as per the following illustration:

![Replicated Password](/images/replicated/replicated-password.png)

## Perform preflight checks

Replicated performs a set of preflight checks to ensure that the host is set up correctly for YugabyteDB Anywhere, as shown in the following illustration:

![Replicated Checks](/images/replicated/replicated-checks.png)

Click **Continue** to configure YugabyteDB Anywhere.

If the preflight check fails, review the [Troubleshoot YugabyteDB Anywhere](../../../troubleshoot/) to resolve the issue.

## Set the TLS version for Yugaware frontend

Under **Application config**, specify TLS versions as shown in the following illustration:

![Application Config](/images/replicated/application-config-tls.png)

The recommended TLS version is 1.2.

Optionally, you can specify a Support Origin URL to provide an alternate hostname or IP address to whitelist for the CORS filter. For example, for a load balancer.

## Set up HTTP/HTTPS proxy

YugabyteDB Anywhere sometimes initiates HTTP or HTTPS connections to other servers. For example, HTTP or HTTPS connections (depending on your setup) can be used to do the following, or more:

- Contact a public cloud provider to create VMs.
- Deposit backups on a public cloud provider's object storage service.
- Contact an external load balancer.

You can set up YBA to use an HTTP/HTTPS proxy server via **Application config**, and select **Enable Proxy** as per the following illustration:

![Enable Proxy](/images/replicated/enable-proxy.png)

When completing the **Enable Proxy** settings, keep in mind the following:

- If your proxy is using the default ports for each protocol, then set the ports for the HTTP and HTTPS proxies to the default (80 and 443 respectively), instead of 8080 and 8443 as shown in the preceding illustration.

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
