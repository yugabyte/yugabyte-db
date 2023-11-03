---
title: Amazon MSK tutorial for YugabyteDB CDC
headerTitle: Amazon MSK
linkTitle: Amazon MSK
description: Amazon MSK for Change Data Capture in YugabyteDB.
headcontent: Stream data from YugabyteDB CDC to Amazon MSK using Debezium
menu:
  stable:
    parent: cdc-tutorials
    identifier: cdc-aws-msk
    weight: 10
type: docs
---

Amazon Managed Streaming for Apache Kafka (Amazon MSK) is a fully managed, highly available, and secure Apache Kafka service offered by Amazon Web Services (AWS). Using Amazon MSK, you can build and run applications using Apache Kafka without having to manage and operate your own Kafka clusters.

This tutorial describes how to configure Yugabyte CDC and stream data into Amazon MSK using Debezium connector, and assumes some familiarity with AWS, Apache Kafka, and CDC.

![Architecture of YugabyteDB to MSK using Debezium](/images/explore/cdc/aws_msk_images/architecture.jpg)

### Configure IAM roles and policies

Create a new role with the required accesses to AWS services.

The following example uses the name `yb_cdc_kafka_role`. The IAM roles and policies are generic and can be fine-tuned based on your organization IT policies. Configure the Trusted entities as follows:

```json
{
  "Version": "2012–10–17",
  "Statement": [
    {
      "Effect": "Allow",
      "Principal": {
        "Service": "kafkaconnect.amazonaws.com"
      },
      "Action": "sts:AssumeRole"
    }
  ]
}
```

Create a policy with access to the following AWS services:

- Apache Kafka APIs for MSK
- EC2
- MSK Connect
- S3
- CloudWatch

```json
{
  "Version": "2012–10–17",
  "Statement": [
    {
      "Sid": "VisualEditor0",
      "Effect": "Allow",
      "Action": "ec2:CreateNetworkInterface",
      "Resource": "arn:aws:ec2:*:*:network-interface/*",
      "Condition": {
        "StringEquals": {
          "aws:RequestTag/AmazonMSKConnectManaged": "true"
        },
        "ForAllValues:StringEquals": {
          "aws:TagKeys": "AmazonMSKConnectManaged"
        }
      }
    },
    {
      "Sid": "VisualEditor1",
      "Effect": "Allow",
      "Action": "ec2:CreateTags",
      "Resource": "arn:aws:ec2:*:*:network-interface/*",
      "Condition": {
        "StringEquals": {
          "ec2:CreateAction": "CreateNetworkInterface"
        }
      }
    },
    {
      "Sid": "VisualEditor2",
      "Effect": "Allow",
      "Action": [
        "ec2:DetachNetworkInterface",
        "ec2:CreateNetworkInterfacePermission",
        "ec2:DeleteNetworkInterface",
        "ec2:AttachNetworkInterface"
      ],
      "Resource": "arn:aws:ec2:*:*:network-interface/*",
      "Condition": {
        "StringEquals": {
          "ec2:ResourceTag/AmazonMSKConnectManaged": "true"
        }
      }
    },
    {
      "Sid": "VisualEditor3",
      "Effect": "Allow",
      "Action": "ec2:CreateNetworkInterface",
      "Resource": [
        "arn:aws:ec2:*:*:subnet/*",
        "arn:aws:ec2:*:*:security-group/*"
      ]
    },
    {
      "Sid": "VisualEditor4",
      "Effect": "Allow",
      "Action": [
        "cloudwatch:PutDashboard",
        "cloudwatch:PutMetricData",
        "cloudwatch:DeleteAlarms",
        "kafkaconnect:ListConnectors",
        "cloudwatch:DeleteInsightRules",
        "cloudwatch:StartMetricStreams",
        "cloudwatch:DescribeAlarmsForMetric",
        "cloudwatch:ListDashboards",
        "cloudwatch:ListTagsForResource",
        "kafka-cluster:AlterCluster",
        "kafkaconnect:CreateWorkerConfiguration",
        "cloudwatch:PutAnomalyDetector",
        "kafka-cluster:Connect",
        "kafkaconnect:UpdateConnector",
        "cloudwatch:DescribeInsightRules",
        "cloudwatch:GetDashboard",
        "cloudwatch:GetInsightRuleReport",
        "kafka-cluster:ReadData",
        "cloudwatch:DisableInsightRules",
        "cloudwatch:GetMetricStatistics",
        "cloudwatch:DescribeAlarms",
        "cloudwatch:GetMetricStream",
        "kafka-cluster:*Topic*",
        "kafkaconnect:DescribeConnector",
        "cloudwatch:GetMetricData",
        "cloudwatch:ListMetrics",
        "cloudwatch:DeleteAnomalyDetector",
        "kafkaconnect:ListWorkerConfigurations",
        "cloudwatch:DescribeAnomalyDetectors",
        "cloudwatch:DeleteDashboards",
        "kafka-cluster:AlterGroup",
        "cloudwatch:DescribeAlarmHistory",
        "cloudwatch:StopMetricStreams",
        "cloudwatch:DisableAlarmActions",
        "kafkaconnect:DescribeWorkerConfiguration",
        "kafkaconnect:CreateConnector",
        "kafkaconnect:ListCustomPlugins",
        "cloudwatch:DeleteMetricStream",
        "cloudwatch:SetAlarmState",
        "kafka-cluster:DescribeGroup",
        "cloudwatch:GetMetricWidgetImage",
        "kafkaconnect:DescribeCustomPlugin",
        "s3:*",
        "kafka-cluster:DescribeCluster",
        "cloudwatch:EnableInsightRules",
        "cloudwatch:PutCompositeAlarm",
        "cloudwatch:PutMetricStream",
        "cloudwatch:PutInsightRule",
        "cloudwatch:PutMetricAlarm",
        "cloudwatch:EnableAlarmActions",
        "cloudwatch:ListMetricStreams",
        "kafkaconnect:CreateCustomPlugin",
        "kafkaconnect:DeleteConnector",
        "kafkaconnect:DeleteCustomPlugin",
        "kafka-cluster:WriteData"
      ],
      "Resource": "*"
    },
    {
      "Sid": "VisualEditor5",
      "Effect": "Allow",
      "Action": "ec2:DescribeNetworkInterfaces",
      "Resource": "arn:aws:ec2:*:*:network-interface/*",
      "Condition": {
        "StringEquals": {
          "ec2:ResourceTag/AmazonMSKConnectManaged": "true"
        }
      }
    }
  ]
}
```

### Enable CDC on YugabyteDB

Ensure that YugabyteDB is up and running. To install YugabyteDB on your cloud virtual machine, refer to [Quick start](../../../../quick-start/linux/).

1. Create a test table:

    ```sql
    CREATE TABLE test (id INT PRIMARY KEY, name TEXT);
    ```

1. Enable CDC using the yb-admin [create_change_data_stream](../../../../admin/yb-admin/#create-change-data-stream) command to enable CDC on all the schemas and tables in the YugabyteDB database as follows:

    ```sh
    ./yb-admin -master_addresses <master_addresses>:7100 \
         create_change_data_stream ysql.yugabyte \
        -certs_dir_name /home/yugabyte/yugabyte-tls-config/
    ```

    If you have a multi-node YugabyteDB setup, you need to provide a comma-separated list of **host:port** values of both the leader and the follower nodes as the `master_addresses` argument.

    If successful, the command returns the CDC stream ID:

    ```output
    CDC Stream ID: 90fe97d59a504bb6acbfd6a940
    ```

For more information on CDC commands, refer to [Change data capture commands](../../../../admin/yb-admin/#change-data-capture-cdc-commands).

### Configure the AWS Security Group

Create a Security Group with inbound and outbound rules configured to ensure access to the MSK cluster and YugabyteDB. For this example, enable incoming traffic from all the ports.

![Edit Inbound Rules](/images/explore/cdc/aws_msk_images/edit-inbound-rules.png)

### Upload Debezium connector Jar file to the S3 bucket

Download the YugabyteDB Debezium connector jar from the [repository](https://github.com/yugabyte/debezium-connector-yugabytedb/releases/download/v1.9.5.y.19/debezium-connector-yugabytedb-1.9.5.y.19.jar) and upload it to an S3 bucket.

![Upload to S3](/images/explore/cdc/aws_msk_images/upload-to-s3.png)

### Configure the Amazon MSK cluster

For this example, create an Amazon MSK cluster in the same VPC as that of the YugabyteDB cluster. Note that this is a generic configuration, and it may differ based on your organizational IT policy.

1. Navigate to **Cluster Settings**.

    ![MSK cluster settings](/images/explore/cdc/aws_msk_images/msk-cluster-settings.png)

1. Create a cluster with two zones.

    ![Amazon Brokers](/images/explore/cdc/aws_msk_images/amazon-brokers.png)

1. In the **Networking** section, select the same VPC and Private subnets as used by the YugabyteDB cluster.

1. Choose the security group you created previously.

    ![Amazon Security Groups](/images/explore/cdc/aws_msk_images/amazon-security-groups.png)

1. Enable logging on your cluster to help with debugging. This example uses the S3 bucket to store the logs.

    ![Amazon S3 logging](/images/explore/cdc/aws_msk_images/amazon-s3-logging.png)

The cluster is now configured successfully.

![Amazon MSK Create Cluster](/images/explore/cdc/aws_msk_images/amazon-msk-create-cluster.png)
