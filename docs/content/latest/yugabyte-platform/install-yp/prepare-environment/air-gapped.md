---
title: Prepare the on-premises, airgapped host environment
headerTitle: Prepare the on-premises, airgapped host environment
linkTitle: 1. Prepare the environment
description: Prepare the on-premises, airgapped host environment.
aliases:
  - /latest/deploy/enterprise-edition/install-admin-console/airgapped
menu:
  latest:
    identifier: prepare-environment-6-air-gapped
    name: 1. Prepare the environment
    parent: prepare-environment
    weight: 55
isTocNested: true
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="/latest/yugabyte-platform/install-yp/prepare-environment/aws" class="nav-link">
      <i class="fab fa-aws" aria-hidden="true"></i>
      AWS
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/install-yp/prepare-environment/gcp" class="nav-link">
       <i class="fab fa-google" aria-hidden="true"></i>
      GCP
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/install-yp/prepare-environment/azure" class="nav-link">
      <i class="icon-azure" aria-hidden="true"></i>
      Azure
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/install-yp/prepare-environment/kubernetes" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/install-yp/prepare-environment/on-premises" class="nav-link">
      <i class="fas fa-building" aria-hidden="true"></i>
      On-premises
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/install-yp/prepare-environment/air-gapped" class="nav-link active">
      <i class="fas fa-unlinked" aria-hidden="true"></i>
      Air-gapped
    </a>
  </li>

</ul>

An air-gapped host has either no or a restricted path to inbound or outbound Internet traffic at all.

## Prerequisites

### 1. Whitelist endpoints

In order to install Replicated and the Yugabyte Platform on a host with no Internet connectivity at all, you have to first download the binaries on a machine that has Internet connectivity and then copy the files over to the appropriate host. In case of restricted connectivity, the following endpoints have to be whitelisted to ensure that they are accessible from the host marked for installation.

```sh
https://downloads.yugabyte.com
https://download.docker.com
```

### 2. Install Docker Engine

A supported version of Docker Engine (`docker-engine`) (currently 1.7.1 to 17.03.1-ce) needs to be installed on the host. If you do not have docker-engine installed, follow the instructions [here](https://help.replicated.com/docs/kb/supporting-your-customers/installing-docker-in-airgapped/) to first install Docker Engine on an air-gapped host. After Docker Engine is installed, perform the following steps to install Replicated and then Yugabyte Platform.

## Step 1 — Install Replicated

On a machine connected to the Internet, perform the following steps.

Make a directory for downloading the binaries.

```sh
$ sudo mkdir /opt/downloads
```

Change the owner user for the directory.

```sh
$ sudo chown -R ubuntu:ubuntu /opt/downloads
```

Change to the directory.

```sh
$ cd /opt/downloads
```

Download the `replicated.tar.gz` file.

```sh
$ wget https://downloads.yugabyte.com/replicated.tar.gz
```

Download the `yugaware` binary. Change this number as needed.

```sh
$ wget https://downloads.yugabyte.com/yugaware-2.1.2.0-b10.airgap
```

Change to the directory.

```sh
$ cd /opt/downloads
```

Extract the `replicated` binary.

```sh
$ tar xzvf replicated.tar.gz
```

Install Replicated. If multiple options appear, select the `eth0` network interface. The `yugaware` binary will be installed using the replicated UI after the replicated installation completes.

```sh
$ cat ./install.sh | sudo bash -s airgap
```

After Replicated finishes installing, make sure it is running.

```sh
$ sudo docker ps
```

You should see an output similar to the following.

![Replicated successfully installed](/images/replicated/replicated-success.png)

Next, install Yugabyte Platform as described in step 2.

## Step 2 — Install Yugabyte Platform using Replicated

### Set up HTTPS for Replicated

Launch the Replicated UI by going to [http://yugaware-host-public-ip:8800](http://yugaware-host-public-ip:8800). The warning shown next states that the connection to the server is not private (yet). We will address this warning as soon as you configure HTTPS for the Replicated Admin Console in the next step. Click **Continue to Setup** and then **ADVANCED** to bypass this warning and go to the **Replicated Admin Console**.

![Replicated SSL warning](/images/replicated/replicated-warning.png)

You can provide your own custom SSL certificate along with a hostname.

![Replicated HTTPS setup](/images/replicated/replicated-https.png)

The simplest option is use a self-signed cert for now and add the custom SSL certificate later. Note that you will have to connect to the Replicated Admin Console only using IP address (as noted below).

![Replicated Self Signed Cert](/images/replicated/replicated-selfsigned.png)

### Upload license file

Now, upload the Yugabyte license file received from Yugabyte Support.

![Replicated License Upload](/images/replicated/replicated-license-upload.png)

Two options to install Yugabyte Platform are presented.

![Replicated License Air-gapped Install](/images/replicated/replicated-license-airgapped-install-option.png)

![Replicated License Air-gapped Path](/images/replicated/replicated-license-airgapped-path.png)

![Replicated License Air-gapped Progress](/images/replicated/replicated-license-airgapped-progress.png)

### Secure Replicated

The next step is to add a password to protect the Replicated Admin Console (for Replicated use only and differs from the Yugabyte Platform console).

![Replicated Password](/images/replicated/replicated-password.png)

### Preflight checks

Replicated will perform a set of preflight checks to ensure that the host is set up correctly for Yugabyte Platform.

![Replicated Checks](/images/replicated/replicated-checks.png)

Clicking **Continue** above will bring you to the Yugabyte Platform configuration.

In case the preflight check fails, review the [Troubleshoot Yugabyte Platform](../../../troubleshoot/) to identify the resolution.
