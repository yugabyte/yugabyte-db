---
title: Install YugabyteDB Anywhere
headerTitle: Install YugabyteDB Anywhere
linkTitle: Install software
description: Install the YugabyteDB Anywhere software.
menu:
  v2.14_yugabyte-platform:
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
      <i class="fa-solid fa-cubes" aria-hidden="true"></i>Kubernetes</a>
  </li>

  <li>
    <a href="../airgapped/" class="nav-link">
      <i class="fa-solid fa-link-slash"></i>Airgapped</a>
  </li>

  <li>
    <a href="../openshift/" class="nav-link">
      <i class="fa-solid fa-cubes"></i>OpenShift</a>
  </li>

</ul>

YugabyteDB universes and clusters are created and managed using YugabyteDB Anywhere. The default option is to install YugabyteDB Anywhere on a host machine that is connected to the Internet.

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
