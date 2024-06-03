---
title: Troubleshoot install and upgrade issues
headerTitle:
linkTitle: Install and upgrade issues
description: Troubleshoot issues encountered when installing or upgrading YugabyteDB Anywhere.
menu:
  v2.16_yugabyte-platform:
    identifier: install-upgrade-yp-issues
    parent: troubleshoot-yp
    weight: 10
type: docs
---

You might encounter issues during installation and upgrade of YugabyteDB Anywhere.

If you experience difficulties while troubleshooting, contact [Yugabyte Support](https://support.yugabyte.com).

## Firewall enabled the YugabyteDB Anywhere host

If your host has firewall managed by firewalld enabled, then Docker Engine might not be able to connect to the host. To open the ports using firewall exceptions, execute the following command:

```sh
sudo firewall-cmd --zone=trusted --add-interface=docker0
sudo firewall-cmd --zone=public --add-port=80/tcp
sudo firewall-cmd --zone=public --add-port=443/tcp
sudo firewall-cmd --zone=public --add-port=8800/tcp
sudo firewall-cmd --zone=public --add-port=5432/tcp
sudo firewall-cmd --zone=public --add-port=9000/tcp
sudo firewall-cmd --zone=public --add-port=9090/tcp
sudo firewall-cmd --zone=public --add-port=32769/tcp
sudo firewall-cmd --zone=public --add-port=32770/tcp
sudo firewall-cmd --zone=public --add-port=9880/tcp
sudo firewall-cmd --zone=public --add-port=9874-9879/tcp
```


## Create mount paths on the nodes

You can create mount paths on the nodes with private IP addresses `10.1.13.150`, `10.1.13.151`, and `10.1.13.152` by executing the following command:

```sh
for IP in 10.1.12.103 10.1.12.104 10.1.12.105;
do
  ssh $IP mkdir -p /mnt/data0;
done
```

## Firewall enabled for nodes

You can add firewall exceptions on the nodes with private IP addresses `10.1.13.150`, `10.1.13.151`, and `10.1.13.152` by executing the following command:

```sh
for IP in 10.1.12.103 10.1.12.104 10.1.12.105;
do
  ssh $IP firewall-cmd --zone=public --add-port=7000/tcp;
  ssh $IP firewall-cmd --zone=public --add-port=7100/tcp;
  ssh $IP firewall-cmd --zone=public --add-port=9000/tcp;
  ssh $IP firewall-cmd --zone=public --add-port=9100/tcp;
  ssh $IP firewall-cmd --zone=public --add-port=11000/tcp;
  ssh $IP firewall-cmd --zone=public --add-port=12000/tcp;
  ssh $IP firewall-cmd --zone=public --add-port=9300/tcp;
  ssh $IP firewall-cmd --zone=public --add-port=9042/tcp;
  ssh $IP firewall-cmd --zone=public --add-port=6379/tcp;
done
```

## Configure load balancer for Helm charts

You might experience the following issues related to your load balancer configuration:

- If there are issues with accessing YugabyteDB Anywhere through a load balancer, you can define the Cross-Origin Resource Sharing (CORS) domain configuration by setting the [additionAllowedCorsOrigins](https://github.com/yugabyte/charts/blob/master/stable/yugaware/values.yaml#L66) value to the new domain involved. For example, you would add the following to the appropriate Helm command:

  ```properties
   --set additionAllowedCorsOrigins:'https://mylbdomain'
  ```

- If the default Amazon Web Services (AWS) load balancer brought up in Amazon Elastic Kubernetes Service (EKS) by the YugabyteDB Anywhere Helm chart is not suitable for your setup, you can use the following settings to customize the [AWS load balancer controller](https://kubernetes-sigs.github.io/aws-load-balancer-controller/v2.2/guide/service/annotations/) behavior:
  - `aws-load-balancer-scheme` can be set to `internal` or `internet-facing` string value.
  - `aws-load-balancer-backend-protocol` and `aws-load-balancer-healthcheck-protocol` should be set to the `http` string value.

  Consider the following sample configuration:

  ```properties
  service.beta.kubernetes.io/aws-load-balancer-type: "ip"
  service.beta.kubernetes.io/aws-load-balancer-scheme: "internet-facing"
  service.beta.kubernetes.io/aws-load-balancer-backend-protocol: "http"
  service.beta.kubernetes.io/aws-load-balancer-healthcheck-protocol: "http"
  ```



<!--

For YugabyteDB Anywhere HTTPS configuration, you should set your own key or certificate. If you do provide this setting, the default public key is used, creating a potential security risk.

-->
