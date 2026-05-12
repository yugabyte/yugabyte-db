---
title: Deploy YugabyteDB clusters in public clouds
headerTitle: Public clouds
linkTitle: Public clouds
description: Deploy YugabyteDB clusters in public clouds, including Amazon Web Services (AWS), Google Cloud Platform (GCP), and Microsoft Azure.
headcontent: Deploy YugabyteDB in public clouds
aliases:
  - /deploy/public-clouds/
menu:
  stable:
    identifier: public-clouds
    parent: deploy
    weight: 40
type: indexpage
---

For information on deploying on Kubernetes on cloud providers, see [Deploy on Kubernetes](../kubernetes/).

{{<index/block>}}

  {{<index/item
    title="Amazon Web Services (AWS)"
    body="Manual deployment and CloudFormation template for automated deployment."
    href="aws/cloudformation/"
    icon="fa-brands fa-aws">}}

  {{<index/item
    title="Google Cloud Platform (GCP)"
    body="Automated deployment using Google Deployment Manager."
    href="gcp/gcp-deployment-manager/"
    icon="fa-brands fa-google">}}

  {{<index/item
    title="Microsoft Azure"
    body="Automated deployment using Azure Resource Manager."
    href="azure/azure-arm/"
    icon="fa-brands fa-microsoft">}}

{{</index/block>}}
