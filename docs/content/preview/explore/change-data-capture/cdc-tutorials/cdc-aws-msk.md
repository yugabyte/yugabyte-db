---
title: Amazon MSK tutorial for YugabyteDB CDC
headerTitle: Amazon MSK
linkTitle: Amazon MSK
description: Amazon MSK for Change Data Capture in YugabyteDB.
headcontent: Stream data from YugabyteDB CDC to Amazon MSK using Debezium
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
menu:
  preview:
    parent: cdc-tutorials
    identifier: cdc-aws-msk
    weight: 20
type: docs
---

This tutorial describes how to configure Yugabyte CDC and stream data into Amazon MSK using Debezium connector.

The tutorial assumes some familiarity with AWS, Apache Kafka, and CDC.

![Architecture of YugabyteDB to MSK using Debezium](/images/explore/cdc/aws_msk_images/architecture.jpg)

### Configure IAM roles and policies

Create a new role with the required accesses to AWS services. This following example uses the name `yb_cdc_kafka_role`. The IAM roles and policies are generic and can be fine-tuned based on your organization IT policies. Configure the Trusted entities as follows:

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

Create a test table:

```sql
CREATE TABLE test (id INT PRIMARY KEY, name TEXT);
```

Enable CDC using yb-admin. The following command enables CDC on all the schemas and tables in the Yugabyte database.

```sh
./bin/yb-admin — master_addresses <master_addresses>:7100 create_change_data_stream ysql.yugabyte
```

If you have a multi-node YugabyteDB setup, you need to provide a comma-separated list of **host:port** values of both the leader and the follower nodes as the `master_addresses` argument.

If successful, the command returns the CDC stream ID:

```output
CDC Stream ID: 90fe97d59a504bb6acbfd6a940
```

For more information on CDC commands, refer to [Change data capture commands](../../../../admin/yb-admin/#change-data-capture-cdc-commands).

### Configure the AWS Security Group

Create a Security Group with inbound and outbound rules configured to ensure access to the MSK cluster and YugabyteDB. In this example, enable incoming traffic from all the ports.

![Edit Inbound Rules](https://miro.medium.com/v2/resize:fit:700/0*GCIXUAlFVQbvCNpX)

### Upload Debezium connector Jar file to the S3 bucket

Download the YugabyteDB Debezium connector jar from the [repository](https://github.com/yugabyte/debezium-connector-yugabytedb/releases/download/v1.9.5.y.19/debezium-connector-yugabytedb-1.9.5.y.19.jar) and upload it to an S3 bucket.

![Upload to S3](https://miro.medium.com/v2/resize:fit:700/0*gk4kNo4roN6w1aSJ)

### Configure the Amazon MSK cluster

This example creates an Amazon MSK cluster in same VPC as that of the YugabyteDB cluster. Note that this is a generic configuration, it might differ based your organizational IT policy.

![MSK cluster settings](https://miro.medium.com/v2/resize:fit:700/0*zcTnuwYjgMLYZszE)

This example creates a cluster with two zones.

![Amazon Brokers](https://miro.medium.com/v2/resize:fit:700/0*vwLr8-tZqsxGuvuO)

Under Networking Section, select VPC and Private subnets same as that of Yugabyte Cluster . Choose the security group created in step 3 from the drop down list.

![Amazon Security Groups](https://miro.medium.com/v2/resize:fit:700/0*PMGfUb7LB7CjtM1C)

Enable logging on your cluster to ease debugging . In this demo, we are using S3 bucket to store the logs.

![Amazon S3 logging](https://miro.medium.com/v2/resize:fit:700/0*MOv37Ars6QWVPPiv)

The cluster is now is now configured successfully.

![Amazon MSK Create Cluster](https://miro.medium.com/v2/resize:fit:700/0*1_esGoZGOpGwHnDD)
