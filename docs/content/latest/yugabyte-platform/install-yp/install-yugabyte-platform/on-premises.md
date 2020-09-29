---
title: Install Yugabyte Platform - On-premises
headerTitle: Install Yugabyte Platform - On-premises
linkTitle: 3. Install Yugabyte Platform
description: Install Yugabyte Platform in your on-premises environment.
menu:
  latest:
    identifier: install-yp-5-on-premises
    parent: install-yugabyte-platform
    weight: 77
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="/latest/yugabyte-platform/install-yp/install-yugabyte-platform/aws" class="nav-link">
      <i class="fab fa-aws" aria-hidden="true"></i>
      AWS
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/install-yp/install-yugabyte-platform/gcp" class="nav-link">
      <i class="fab fa-google" aria-hidden="true"></i>
      GCP
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/install-yp/install-yugabyte-platform/azure" class="nav-link">
       <i class="icon-azure" aria-hidden="true"></i>
      Azure
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/install-yp/install-yugabyte-platform/kubernetes" class="nav-link">
       <i class="fas fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/install-yp/install-yugabyte-platform/on-premises" class="nav-link active">
       <i class="fas fa-building" aria-hidden="true"></i>
      On-premises
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/install-yp/install-yugabyte-platform/air-gapped" class="nav-link">
       <i class="fas fa-unlink" aria-hidden="true"></i>
      Air-gapped
    </a>
  </li>

</ul>

YugabyteDB universes and clusters are created and managed using the Yugabyte Platform. The default option to install Yugabyte Platform on a host machine that is connected to the Internet.

## Step 1. Install Replicated

Connect to the Yugabyte Platform instance and do the following.

- Install Replicated.

```sh
$ curl -sSL https://get.replicated.com/docker | sudo bash
```

**NOTE**: If you are behind a proxy, you would need to run the following:

- Install Replicated behind a proxy.

```sh
$ curl -x http://<proxy_address>:<proxy_port> https://get.replicated.com/docker | sudo bash
```

- After Replicated install completes, make sure it is running.

You can do this as shown below.

```sh
$ sudo docker ps
```

You should see an output similar to the following.

![Replicated successfully installed](/images/replicated/replicated-success.png)

## Step 2. Install Yugabyte Platform

### Set up HTTPS for Replicated

Launch Replicated UI by going to [http://yugaware-host-public-ip:8800](http://yugaware-host-public-ip:8800). The warning shown next states that the connection to the server is not private (yet). You will address this warning as soon after setting up HTTPS for the Replicated Admin Console in the next step. Click **Continue to Setup** and then **ADVANCED** to bypass this warning and go to the Replicated Admin Console.

![Replicated SSL warning](/images/replicated/replicated-warning.png)

You can provide your own custom SSL certificate along with a hostname.

![Replicated HTTPS setup](/images/replicated/replicated-https.png)

The simplest option is use a self-signed cert for now and add the custom SSL certificate later. Note that you will have to connect to the Replicated Admin Console only using IP address (as noted below).

![Replicated Self Signed Cert](/images/replicated/replicated-selfsigned.png)

### Upload license file

Now upload the Yugabyte license file received from Yugabyte Support.

![Replicated License Upload](/images/replicated/replicated-license-upload.png)

If you are asked to choose an installation type, choose `Online`.

![Replicated License Online Install](/images/replicated/replicated-license-online-install-option.png)

![Replicated License Online Progress](/images/replicated/replicated-license-progress.png)

### Secure Replicated

The next step is to add a password to protect the Replicated Admin Console (this is different from the Yugabyte Platform console.

![Replicated Password](/images/replicated/replicated-password.png)

### Preflight checks

Replicated will perform a set of preflight checks to ensure that the host is set up correctly for the Yugabyte Platform.

![Replicated Checks](/images/replicated/replicated-checks.png)

Clicking **Continue** above will bring you to Yugabyte Platform configuration.

In case the preflight check fails, review the [Troubleshoot Yugabyte Platform](../../../troubleshoot/) to identify the resolution.
