---
title: Install YugabyteDB Anywhere
headerTitle: Install YugabyteDB Anywhere
linkTitle: Install software
description: Install the YugabyteDB Anywhere software.
menu:
  v2.16_yugabyte-platform:
    parent: install-yugabyte-platform
    identifier: install-software-1-default
    weight: 77
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../default/" class="nav-link active">
      <i class="fa-solid fa-cloud"></i>Default</a>
  </li>

  <li>
    <a href="../kubernetes/" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>Kubernetes</a>
  </li>

  <li>
    <a href="../airgapped/" class="nav-link">
      <i class="fa-solid fa-link-slash"></i>Airgapped</a>
  </li>

  <li>
    <a href="../openshift/" class="nav-link">
      <i class="fa-brands fa-redhat"></i>OpenShift</a>
  </li>

</ul>

<br>YugabyteDB universes and clusters are created and managed using YugabyteDB Anywhere. The default option is to install YugabyteDB Anywhere on a host machine that is connected to the Internet.

## Install Replicated

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

## Set up HTTPS (optional)

Launch the Replicated UI via http://< yugabyte-platform-host-public-ip >:8800. Expect to see a warning stating that the connection to the server is not yet private. This condition is resolved once HTTPS for the Replicated Admin Console is set up. Proceed by clicking **Continue to Setup > ADVANCED** to bypass the warning and access the Replicated Admin Console, as per the following illustration:

![Replicated SSL warning](/images/replicated/replicated-warning.png)

You can provide your own custom SSL certificate and a hostname, as per the following illustration:

![Replicated HTTPS setup](/images/replicated/replicated-https.png)

It is recommended that you start with using a self-signed certificate, and then add the custom SSL certificate later. Note that in this case you connect to the Replicated Admin Console using an IP address, as per following illustration:

![Replicated Self Signed Cert](/images/replicated/replicated-selfsigned.png)

## Upload the license file

When prompted, upload the Yugabyte license file that you received from Yugabyte, as per the following illustration:

![Replicated License Upload](/images/replicated/replicated-license-upload.png)

If you are prompted to choose an installation type, choose **Online**.

If you are offered a choice of software versions, select the one that meets your requirements.

## Secure Replicated

Add a password to protect the Replicated Admin Console, which is different from the Admin Console for YugabyteDB used by YugabyteDB Anywhere, as per the following illustration:

![Replicated Password](/images/replicated/replicated-password.png)

## Perform preflight checks

Replicated performs a set of preflight checks to ensure that the host is set up correctly for YugabyteDB Anywhere, as shown in the following illustration:

![Replicated Checks](/images/replicated/replicated-checks.png)

Click **Continue** to configure YugabyteDB Anywhere.

If the preflight check fails, review the [Troubleshoot YugabyteDB Anywhere](../../../troubleshoot/) to resolve the issue.

## Set the TLS version for Nginx frontend

Specify TLS versions via **Application config**, as shown in the following illustration:

![Application Config](/images/replicated/application-config.png)

The recommended TLS version is 1.2.

## Set up HTTP/HTTPS proxy

YBA sometimes initiates HTTP or HTTPS connections to other servers. For example, HTTP or HTTPS connections (depending on your setup) maybe utilized in one or more of the following (not limited to) situations:

1. Contact a public cloud provider to create VMs.
1. Deposit backups on a public cloud provider's object storage service.
1. Contact an external load balancer.

You can set up HTTP and HTTPS proxies via **Application config**, and select **Enable Proxy** as per the following illustration:

![Enable Proxy](/images/replicated/enable-proxy.png)

When completing the **Enable Proxy** settings, keep in mind the following:

- If your proxy is using the default ports for each protocol, then set the ports for the HTTP and HTTPS proxies to the default, 80 and 443 respectively, instead of 8080 and 8443 as shown in the preceding illustration.

- If you have only one proxy set up (HTTP or HTTPS), then set the same values for both. This configuration sets operating system environment variables and Java system properties.

- The help text for each field shows which Java system property or environment variable gets set by the field. System properties have the "-D" prefix. For example "Specify -Dhttps.proxyPort".

- The **no proxy** fields (HTTP no proxy setting, HTTP no proxy setting for Java) are lists of exception hosts, provided as a comma-delimited list of addresses or hostnames. Include the following addresses:
  - The Docker gateway address (172.17.0.1 by default).
  - The address of any previously-specified web proxy.
  - Any other IP addresses that you deem safe to bypass the proxy.

- The Java fields can accept values as Java system properties, including the use of pipe ("|") as a field separator. Refer to [Java Networking and Proxies](https://docs.oracle.com/javase/8/docs/technotes/guides/net/proxies.html) for more details about the properties.

- There is no authoritative source for the format of environment variables; YBA follows the community standards for setting proxy where two environment variables get exported with lowercase and uppercase formats for the fields. For example, if you enter "http://my.Proxy.host:8080" for **HTTP Proxy setting**, then two environment variables gets exported as follows:

    ```sh
    HTTP_PROXY = http://my.Proxy.host:8080
    http_proxy = http://my.Proxy.host:8080
    ```
