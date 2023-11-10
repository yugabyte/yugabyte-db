---
title: Deploy on Amazon Web Services using Terraform
headerTitle: Amazon Web Services
linkTitle: Amazon Web Services
description: Deploy YugabyteDB clusters on Amazon Web Services using Terraform.
menu:
  v2.18:
    identifier: deploy-in-aws-2-terraform
    parent: public-clouds
    weight: 630
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../cloudformation/" class="nav-link">
      <i class="icon-shell"></i>
      CloudFormation
    </a>
  </li>
  <li >
    <a href="../terraform/" class="nav-link active">
      <i class="icon-shell"></i>
      Terraform
    </a>
  </li>
  <li>
    <a href="../manual-deployment/" class="nav-link">
      <i class="icon-shell"></i>
      Manual deployment
    </a>
  </li>
</ul>

## Prerequisites

Download and install [Terraform](https://www.terraform.io/downloads.html).

Verify the installation using the `terraform` command.

```sh
$ terraform
```

You should see output similar to the following.

```output
Usage: terraform [--version] [--help] <command> [args]
...
Common commands:
    apply              Builds or changes infrastructure
    console            Interactive console for Terraform interpolations
    destroy            Destroy Terraform-managed infrastructure
    env                Workspace management
    fmt                Rewrites config files to canonical format
```

## Create a Terraform configuration file

Create a Terraform configuration file called `yugabyte-db-config.tf` and add the following details to it. The Terraform module can be found in the [terraform-aws-yugabyte GitHub repository](https://github.com/yugabyte/terraform-aws-yugabyte).

```terraform
provider "aws" {
  # Configure your AWS account credentials here.
  access_key = "ACCESS_KEY_HERE"
  secret_key = "SECRET_KEY_HERE"
  region     = "us-west-2"
}

module "yugabyte-db-cluster" {
  # The source module used for creating AWS clusters.
  source = "github.com/Yugabyte/terraform-aws-yugabyte"

  # The name of the cluster to be created, change as per need.
  cluster_name = "test-cluster"

  # Existing custom security group to be passed so that you can connect to the instances.
  # Make sure this security group allows your local machine to SSH into these instances.
  custom_security_group_id="SECURITY_GROUP_HERE"

  # AWS key pair that you want to use to ssh into the instances.
  # Make sure this key pair is already present in the noted region of your account.
  ssh_keypair = "SSH_KEYPAIR_HERE"
  ssh_key_path = "SSH_KEY_PATH_HERE"

  # Existing vpc and subnet ids where the instances should be spawned.
  vpc_id = "VPC_ID_HERE"
  subnet_ids = ["SUBNET_ID_HERE"]

  # Replication factor of the YugabyteDB cluster.
  replication_factor = "3"

  # The number of nodes in the cluster, this cannot be lower than the replication factor.
  num_instances = "3"
}
```

If you do not have a custom security group, you would need to remove the `${var.custom_security_group_id}` variable in `main.tf`, so that the `aws_instance` looks as follows:

```terraform
resource "aws_instance" "yugabyte_nodes" {
  count                       = "${var.num_instances}"
  ...
  vpc_security_group_ids      = [
    "${aws_security_group.yugabyte.id}",
    "${aws_security_group.yugabyte_intra.id}",
    "${var.custom_security_group_id}"
  ]
```

## Create a cluster

Init terraform first if you have not already done so.

```sh
$ terraform init
```

Run the following to create the instances and bring up the cluster:

```sh
$ terraform apply
```

After the cluster is created, you can go to the URL `http://<node ip or dns name>:7000` to view the UI. You can find the node's IP or DNS by running the following:

```sh
$ terraform state show aws_instance.yugabyte_nodes[0]
```

You can check the state of the nodes at any point by running the following command:

```sh
$ terraform show
```

## Verify resources created

The following resources are created by this module:

- `module.yugabyte-db-cluster.aws_instance.yugabyte_nodes`

    The AWS instances.

    For a cluster named `test-cluster`, the instances are named `yb-ce-test-cluster-n1`, `yb-ce-test-cluster-n2`, `yb-ce-test-cluster-n3`.

- `module.yugabyte-db-cluster.aws_security_group.yugabyte`

    The security group that allows the various clients to access the YugabyteDB cluster.

    For a cluster named `test-cluster`, this security group is named `yb-ce-test-cluster`, with the ports 7000, 9000, 9042, and 6379 open to all other instances in the same security group.

- `module.yugabyte-db-cluster.aws_security_group.yugabyte_intra`

    The security group that allows communication internal to the cluster.

    For a cluster named `test-cluster`, this security group is named `yb-ce-test-cluster-intra` with the ports 7100, 9100 open to all other instances in the same security group.

- `module.yugabyte-db-cluster.null_resource.create_yugabyte_universe` A local script that configures the newly created instances to form a new YugabyteDB universe.

## [Optional] Destroy the cluster

To destroy what you just created, you can run the following command:

```sh
$ terraform destroy
```
