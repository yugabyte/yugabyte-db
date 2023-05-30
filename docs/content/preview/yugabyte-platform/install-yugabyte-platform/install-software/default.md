---
title: Install YugabyteDB Anywhere
headerTitle: Install YugabyteDB Anywhere
linkTitle: Install YBA software
description: Install the YugabyteDB Anywhere software.
headContent: Install YBA software using Replicated and Docker containers
aliases:
  - /preview/yugabyte-platform/install-yugabyte-platform/install-software/
menu:
  preview_yugabyte-platform:
    parent: install-yugabyte-platform
    identifier: install-software-1-default
    weight: 77
type: docs
---

Use the following instructions to install YugabyteDB Anywhere software. For guidance on which method to choose, see [YBA Prerequisites](../../prerequisites/default/).

Note: For higher availability, one or more additional YugabyteDB Anywhere instances can be separately installed, and then configured later to serve as passive warm standby servers. See [Enable High Availability](../../../administer-yugabyte-platform/high-availability/) for more information.

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../default/" class="nav-link active">
      <i class="fa-solid fa-cloud"></i>Replicated</a>
  </li>

  <li>
    <a href="../airgapped/" class="nav-link">
      <i class="fa-solid fa-link-slash"></i>Replicated - Airgapped</a>
  </li>

  <li>
    <a href="../kubernetes/" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>Kubernetes</a>
  </li>

  <li>
    <a href="../openshift/" class="nav-link">
      <i class="fa-brands fa-redhat"></i>OpenShift</a>
  </li>

  <li>
    <a href="../installer/" class="nav-link">
      <i class="fa-solid fa-building"></i>YBA Installer</a>
  </li>

</ul>

Install YugabyteDB Anywhere on a host machine that is connected to the Internet using Replicated.

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

## Set the TLS version for Yugaware frontend

Specify TLS versions via **Application config**, as shown in the following illustration:

![Application Config](/images/replicated/application-config-tls.png)

The recommended TLS version is 1.2.

## Set HTTP/HTTPS proxy

You can setup HTTP and HTTPS proxy via **Application config** and selecting **Enable Proxy** as per the following illustration:

The following information describes details about the **Enable Proxy** setting and some considerations.

- If your proxy is using default ports for protocol, then use the defaults for HTTP and HTTPS, 80 and 443 respectively, instead of 8080 and 8443 as shown in the above illustration.

- If you have only one proxy setup (HTTP or HTTPS), then set the same values for both. This configuration ends up setting OS environment variables or java system properties.

- Below each field, you can notice which java system property or environment variable gets set by the field. System properties will have the "-D" prefix. For example "Specify -Dhttps.proxyPort".

- Note that the fields listed with "No proxy" refers to a list of exception hosts, in which the earlier-specified web proxy should be bypassed. You may also want to add any other IP addresses that you deem safe to bypass the proxy.

- For the fields "HTTP no proxy setting" and "HTTP no proxy setting for Java", you need to add the docker gateway address (which is 172.17.0.1 by default).

- Fields with Java system properties include a slightly different format like the use of pipe ("|") as a field separator. Refer to [Java Networking and Proxies](https://docs.oracle.com/javase/8/docs/technotes/guides/net/proxies.html) for more details about the properties.

- There is no authoritative source for the format of environment variables; YBA follows the community standards for setting proxy. So you have to set both UPPERCASE and lowercase for each environment variable.
