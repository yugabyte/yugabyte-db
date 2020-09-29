---
title: Configure Yugabyte Platform
headerTitle: Configure Yugabyte Platform
linkTitle: 3. Configure Yugabyte Platform
description: Configure Yugabyte Platform.
aliases:
  - /latest/deploy/enterprise-edition/configure-admin-console/
menu:
  latest:
    identifier: configure-yugabyte-platform
    parent: configure-yp
    weight: 115
isTocNested: true
showAsideToc: true
---

Configuring Yugabyte Platform is straightforward. A randomly-generated password for the Yugabyte Platform configuration database is already pre-filled in the Admin Console. You can make a note of it for future use or change it to a new password of your choice. Additionally, the location of the directory on the Yugabyte Platform host where all Yugabyte Platform data will be stored is pre-filled (`/opt/yugabyte`).  Click **Save** on this page to go to the Replicated Dashboard.

![Replicated Yugabyte Platform Config](/images/replicated/replicated-yugaware-config.png)

For airgapped installations, all the containers powering the Yugabyte Platform application are already available with Replicated. For non-air-gapped installations, these containers will be downloaded from the Quay.io Registry when the Dashboard is first launched. Replicated will automatically start the Yugabyte Platform as soon as all the container images are available.

![Replicated Dashboard](/images/replicated/replicated-dashboard.png)

To see the release history of the Yugabyte Platform (aka YugaWare) application, click **View release history**.

![Replicated Release History](/images/replicated/replicated-release-history.png)

After starting the Yugabyte Platform, you must register a new tenant by following the instructions in the section below.

## Register tenant

Go to [http://yugaware-host-public-ip/register](http://yugaware-host-public-ip/register) to register a tenant account. Note that by default Yugabyte Platform runs as a single-tenant application.

![Register](/images/ee/register.png)

After you click **Submit**, you are automatically logged into the YugabyteDB Admin Console. You can then proceed to [configuring cloud providers using the YugabyteDB Admin Console](../configure-providers/).

## Log in

By default, [http://yugaware-host-public-ip](http://yugaware-host-public-ip) redirects to [http://yugaware-host-public-ip/login](http://yugaware-host-public-ip/login). Login to the application using the credentials you had provided during the Register customer step.

![Login](/images/ee/login.png)

Click on the top right drop-down list or go directly to [http://yugaware-host-public-ip/profile](http://yugaware-host-public-ip/profile) to change the profile of the customer provided during the Register customer step.

![Profile](/images/ee/profile.png)

Next step is to configure one or more cloud providers in the YugabyteDB Admin Console as documented [here](../configure-providers/).

## Uninstall

Stop and remove the Yugabyte Platform on Replicated first.

```sh
$ /usr/local/bin/replicated apps
```

Replace <appid> with the application ID of Yugabyte Platform from the command above.

```sh
$ /usr/local/bin/replicated app <appid> stop
```

Remove the Yugabyte Platform application.

```sh
$ /usr/local/bin/replicated app <appid> rm
```

Remove all Yugabyte Platform containers.

```sh
$ docker images | grep "yuga" | awk '{print $3}' | xargs docker rmi -f
```

Delete the mapped directory.

```sh
$ rm -rf /opt/yugabyte
```

Nex, uninstall Replicated itself by following instructions documented [here](https://help.replicated.com/docs/native/customer-installations/installing-via-script/#removing-replicated).
