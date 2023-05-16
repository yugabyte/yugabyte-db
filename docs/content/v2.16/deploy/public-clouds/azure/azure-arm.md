---
title: Deploy on Microsoft Azure using Azure Resource Manager (ARM)
headerTitle: Microsoft Azure
linkTitle: Microsoft Azure
description: Deploy YugabyteDB on Microsoft Azure using Azure Resource Manager (ARM).
menu:
  v2.16:
    identifier: deploy-on-azure-1-azure-arm
    parent: public-clouds
    weight: 650
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../azure-arm/" class="nav-link active">
      <i class="icon-shell"></i>
      Azure ARM template
    </a>
  </li>
  <li >
    <a href="../aks/" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>
      Azure Kubernetes Service (AKS)
    </a>
  </li>
  <li>
    <a href="../terraform/" class="nav-link">
      <i class="icon-shell"></i>
      Terraform
    </a>
  </li>
</ul>

<br/>

<a href="https://portal.azure.com/#create/Microsoft.Template/uri/https%3A%2F%2Fraw.githubusercontent.com%2Fyugabyte%2Fazure-resource-manager%2Fmaster%2Fyugabyte_deployment.json" target="_blank">
    <img src="https://raw.githubusercontent.com/Azure/azure-quickstart-templates/master/1-CONTRIBUTION-GUIDE/images/deploytoazure.png"/>
</a>
<a href="http://armviz.io/#/?load=https%3A%2F%2Fraw.githubusercontent.com%2Fyugabyte%2Fazure-resource-manager%2Fmaster%2Fyugabyte_deployment.json" target="_blank">
    <img src="https://raw.githubusercontent.com/Azure/azure-quickstart-templates/master/1-CONTRIBUTION-GUIDE/images/visualizebutton.png"/>
</a>

## Prerequisites

### Create a resource group for your YugabyteDB deployment

1. Log in to your [Azure portal](https://portal.azure.com/).
1. Click Resource groups from the menu of services to access the Resource Groups blade. You will see all the resource groups in your subscription listed in tblade.
1. Click Add (+) to create a new resource group. The Create Resource Group blade appears.
1. Provide the needed information for the new resource group.
1. Click Create. The resource group might take a few seconds to create. Once it is created, you see the resource group on the Azure portal dashboard.

### Create an SSH key to get access to deployed YugabyteDB VMs

1. Open the terminal on your local computer.
1. Run the following command:

    ```sh
    $ ssh-keygen
    ```

    The utility prompts you to select a location for the keys. By default, the keys are stored in the `~/.ssh` directory with the file names `id_rsa` for the private key and `id_rsa.pub` for the public key. Using the default locations allows your SSH client to automatically find your SSH keys when authenticating.

1. Press **ENTER** to accept.

    ```output
    Generating a public/private RSA key pair.
    Enter file in which to save the key (/home/username/.ssh/id_rsa):
    ```

1. After you select a location for the key, you are prompted to enter an optional passphrase which encrypts the private key file on disk.

    ```output
    Created directory '/home/username/.ssh'.
    Enter passphrase (empty for no passphrase):
    Enter same passphrase again:
    ```

You now have a public and private key that you can use to authenticate YugabyteDB VMs.

```output
Your identification has been saved in /home/username/.ssh/id_rsa.
Your public key has been saved in /home/username/.ssh/id_rsa.pub.
The key fingerprint is:
a9:49:EX:AM:PL:E3:3e:a9:de:4e:77:11:58:b6:90:26 username@203.0.113.0
The key's randomart image is:
+--[ RSA 2048]----+
|     ..o         |
|   E o= .        |
|    o. o         |
|        ..       |
|      ..S        |
|     o o.        |
|   =o.+.         |
|. =++..          |
|o=++.            |
+-----------------+
```

## Deploy using Azure Cloud Shell

1. Launch [Azure Cloud Shell](https://shell.azure.com).

1. Clone the following repository.

    ```sh
    $ git clone https://github.com/yugabyte/azure-resource-manager.git
    ```

1. Change the current directory to the cloned GitHub repository directory

    ```sh
      $ cd azure-resource-manager
    ```

1. Use the Azure CLI command to create deployments.

    ```sh
     $ az group deployment create --resource-group <Your-Azure-Resource-Group> --template-file yugabyte_deployment.json --parameters ClusterName='<Your-Cluster-Name>'  SshUser='<Your-SSH-USER>' YBVersion='2.0.6.0' SshKeypair='<Your-SSH-USER-PublicKey-File-Contents>'
     ```

1. After the deployment creation is complete, you can describe it as shown below.

    ```sh
    $ az group deployment show -g <Your-Azure-Resource-Group> -n <Your-Deployment-Name> --query properties.outputs
    ```

    The output displays the YugabyteDB Admin URL, JDBC URL, YSQL, YCQL, and YEDIS connection string. You can use the YugabyteDB Admin URL to access the Admin portal.

## Deploy using Azure Portal

1. Clone the following repository locally.

    ```sh
    $ git clone https://github.com/yugabyte/azure-resource-manager.git
    ```

1. Create a resource group. To create a new resource group, select **Resource groups** from the [Azure portal](https://portal.azure.com/).
1. Under newly created Resource groups, select **Add**.
1. In opened marketplace, search for Template deployment (deploy using custom templates) and click **Create**.
1. Click **Build your own template in the editor**.
1. Click **Load file** in the Specify template section and upload the `yugabyte_deployment.json` file from the cloned repository.
1. Click **Save** at the bottom of the window.
1. Provide the required details.
1. Check the **Terms and Condition** checkbox and click **Purchase**.

Once deployments are completed, you can access the YugabyteDB Admin from the URL you get in the deployment output section.
