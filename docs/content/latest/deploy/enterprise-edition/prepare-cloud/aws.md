
## 1. Create a new security group (optional)

In order to access YugaWare from outside the AWS environment, you would need to enable access by assigning an appropriate seciruty group to the YugaWare machine. You will at minimum need to:

- Access the YugaWare instance over ssh (port tcp:22)
- Check, manage and upgrade YugaWare (port tcp:8800)
- View the YugaWare console ui (port tcp:80)

Let us create a security group enabling all of that!

Go to `EC2` -> `Security Groups`, click on `Create Security Group` and add the following values:

- Enter `yugaware-sg` as the name (you can change the name if you want).
- Add a description (eg: `Security group for YugaWare access`).
- Add the appropriate ip addresses to the `Source IP ranges` field. To allow access from any machine, add `0.0.0.0/0` but note that this is not very secure.
- Add the ports `22`, `8800`, `80` to the `Port Range` field. The `Protocol` must be `TCP`.

You should see something like the screenshot below, click `Create` next.

![Create security group](/images/ee/aws-setup/yugaware-aws-create-sg.png)

## 2. Provision instance for YugaWare

Create an instance to run YugaWare. In order to do so, go to `EC2` -> `Instances` and click on `Launch Instance`. Fill in the following values.


- Change the boot disk image to `Ubuntu 16.04` and continue to the next step.
![Pick OS Image](/images/ee/aws-setup/yugaware-create-instance-os.png)

- Choose `c5.xlarge` (4 vCPUs are recommended for production) as the machine type. Continue to the next step.

- Choose the VPC, subnet and other settings as appropriate. Make sure to enable the `Auto-assign Public IP` setting, otherwise this machine would not be accessible from outside AWS. Continue to the next step.

- Increase the root storage volume size to at least `100GiB`. Continue to the next step.

- Add a tag to name the machine. You can set key to `Name` and value to `yugaware-1`. Continue to the next step.

- Select the `yugaware-sg` security group we created in the previous step (or the custom name you chose when setting up the security groups). Launch the instance.

- Pick an existing key pair (or create a new one) in order to access the machine. Make sure you have the ssh access key. This is important to enable `ssh` access to this machine. In this example, we will assume the key pair is `~/.ssh/yugaware.pem`.

Finally, click `Launch` to launch the YugaWare server. You should see a machine being created as shown in the image below.

![Pick OS Image](/images/ee/aws-setup/yugaware-machine-creation.png)

