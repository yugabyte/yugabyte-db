---
title: Azure ARM
linkTitle: Azure-ARM 
description: Azure ARM
---

<a href="https://portal.azure.com/#create/Microsoft.Template/uri/https%3A%2F%2Fraw.githubusercontent.com%2Fyugabyte%2Fazure-resource-manager%2Fmaster%2Fyugabyte_deployment.json" target="_blank">
    <img src="https://raw.githubusercontent.com/Azure/azure-quickstart-templates/master/1-CONTRIBUTION-GUIDE/images/deploytoazure.png"/>
</a>
<a href="http://armviz.io/#/?load=https%3A%2F%2Fraw.githubusercontent.com%2Fyugabyte%2Fazure-resource-manager%2Fmaster%2Fyugabyte_deployment.json" target="_blank">
    <img src="https://raw.githubusercontent.com/Azure/azure-quickstart-templates/master/1-CONTRIBUTION-GUIDE/images/visualizebutton.png"/>
</a>

## Prerequisites
1. Create a resource group for our YugabyteDB deployment.
     - Login into your [Azure portal](https://portal.azure.com/).
     - Click Resource groups from the menu of services to access the Resource Groups blade. You will see all the resource groups in your subscription listed in the blade.
     - Click Add (+) to create a new resource group. The Create Resource Group blade appears.
     - Provide the needed information for the new resource group.
     - Click Create. The resource group might take a few seconds to create. Once it is created, you see the resource group on the Azure portal dashboard.

  2. Create an SSH key for our user to get access to deployed YugabyteDB VMs.
     - Open Terminal on your local computer.
     - Run the following command
        ```
        ssh-keygen
        ```     
     - The utility prompts you to select a location for the keys. By default, the keys are stored in the ~/.ssh directory with the filenames id_rsa for the private key and id_rsa.pub for the public key. Using the default locations allows your SSH client to automatically find your SSH keys when authenticating, so we recommend accepting them by pressing ENTER
       ```
       Generating a public/private RSA key pair.
       Enter file in which to save the key (/home/username/.ssh/id_rsa):
       ```
     - Once you select a location for the key, you’ll be prompted to enter an optional passphrase which encrypts the private key file on disk.
       ```
       Created directory '/home/username/.ssh'.
       Enter passphrase (empty for no passphrase):
       Enter same passphrase again:
       ```
      - After this, you will have a public and private key that you can use to authenticate YugabyteDB VM's.
        ```
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

## Deploying using Azure Cloud Shell


- Launch Cloud Shell
<a href="https://shell.azure.com" target="_blank">
    <img src="https://shell.azure.com/images/launchcloudshell.png"/>
</a>

- Clone this repo.
    ```
    $ git clone https://github.com/yugabyte/azure-resource-manager.git
    ```
- Change current directory to cloned git repo directory
  ```
    $ cd azure-resource-manager
  ```
- Use Azure CLI command to create deployments <br/>
    ```
    $ az group deployment create --resource-group <Your-Azure-Resource-Group> --template-file yugabyte_deployment.json --parameters ClusterName='<Your-Cluster-Name>' SshUser='<Your-SSH-USER>' YBVersion='2.0.6.0' SshKeypair='<Your-SSH-USER-PublicKey-File-Contents>'
    ```
- Once the deployment creation is complete, you can describe it as shown below.
    ```
    $ az group deployment show -g <Your-Azure-Resource-Group> -n <Your-Deployment-Name> --query properties.outputs
    ```
    In the output, you will get the YugabyteDB admin URL, JDBC URL, YSQL, YCQL and YEDIS connection string. You can use YugabyteDB admin URL to access admin portal.

## Deploying using Azure Portal

- Clone this repo locally.
     ```
     $ git clone https://github.com/yugabyte/azure-resource-manager.git
     ```
- First create a resource group, to create a new resource group, select Resource groups from the [Azure portal](https://portal.azure.com/).
- Under newly created Resource groups, select Add.
- In opened marketplace search for Template deployment (deploy using custom templates) and click on create.
- Now click on `Build your own template in the editor`.
- Click `Load file` button in specify template section and upload the `yugabyte_deployment.json` file from cloned repo.
-  Click on the `Save` button at the bottom of the window.
-  Now provide the required details.
-  Once details are provided, then check the Terms and Condition checkbox and click on the `Purchase` button.
-  Once deployments get compleated, you can access the YugabyteDB admin from URL you get in the deployment output section.
