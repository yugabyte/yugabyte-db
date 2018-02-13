
## Prerequisites

- Download and install terraform from [here](https://www.terraform.io/downloads.html). Verify by the `terraform` command, it should print a help message that looks similar to that shown below.


```sh
$ terraform
Usage: terraform [--version] [--help] <command> [args]
...
Common commands:
    apply              Builds or changes infrastructure
    console            Interactive console for Terraform interpolations
    destroy            Destroy Terraform-managed infrastructure
    env                Workspace management
    fmt                Rewrites config files to canonical format
```


## Create a terraform config file

Create a working directory, and download the yugabyte

Create a terraform config file called `yugabyte-db-config.tf` and add following details to it. The terraform module can be found in the [terraform-aws-yugabyte github repository](https://github.com/YugaByte/terraform-aws-yugabyte).

```sh
provider "aws" {
  access_key = "ACCESS_KEY_HERE"
  secret_key = "SECRET_KEY_HERE"
  region     = "us-west-2"
}

module "yugabyte-db-cluster" {
  source = "github.com/YugaByte/terraform-aws-yugabyte"

  # The name of the cluster to be created.
  cluster_name = "CLUSTER_NAME_HERE"

  # A custom security group to be passed so that we can connect to the nodes.
  custom_security_group_id="SECURITY_GROUP_HERE"

  # AWS key pair.
  ssh_keypair = "SSH_KEYPAIR_HERE"
  ssh_key_path = "SSH_KEY_PATH_HERE"

  # The vpc and subnet ids where the nodes should be spawned.
  vpc_id = "VPC_ID_HERE"
  subnet_ids = ["SUBNET_ID_HERE"]

  # Replication factor.
  replication_factor = "3"

  # The number of nodes in the cluster, this cannot be lower than the replication factor.
  num_instances = "3"
}
```

**NOTE:** If you do not have a custom security group, you would need to remove the `${var.custom_security_group_id}` variable in `main.tf`, so that the `aws_instance` looks as follows:

```sh
resource "aws_instance" "yugabyte_nodes" {
  count                       = "${var.num_instances}"
  ...
  vpc_security_group_ids      = [
    "${aws_security_group.yugabyte.id}",
    "${aws_security_group.yugabyte_intra.id}",
    "${var.custom_security_group_id}"
  ]

```

## Spinning up the cluster

Init terraform first if you have not already done so.

```sh
$ terraform init
```

Now run the following to create the instances and bring up the cluster.

```sh
$ terraform apply
```

Once the cluster is created, you can go to the URL `http://<node ip or dns name>:7000` to view the UI. You can find the node's ip or dns by running the following:

```sh
terraform state show aws_instance.yugabyte_nodes[0]
```

You can access the cluster UI by going to any of the following URLs.

You can check the state of the nodes at any point by running the following command.

```
$ terraform show
```


## Resources created

The following resources are created by this module:

- `module.yugabyte-db-cluster.aws_instance.yugabyte_nodes` The AWS instances.

- `module.yugabyte-db-cluster.aws_security_group.yugabyte` The security group that allows the various clients to access the YugaByte cluster.

- `module.yugabyte-db-cluster.aws_security_group.yugabyte_intra` The security group that allows communication internal to the cluster.

- `module.yugabyte-db-cluster.null_resource.create_yugabyte_universe` A local script that starts the various processes to start the cluster.


## Destroy the cluster (optional)

To destroy what we just created, you can run the following command.

```
$ terraform destroy
```
