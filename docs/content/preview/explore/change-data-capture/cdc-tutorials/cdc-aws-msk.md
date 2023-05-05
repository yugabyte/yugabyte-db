---
title: Amazon MSK tutorial for YugabyteDB CDC
headerTitle: Amazon MSK
linkTitle: Amazon MSK
description: Amazon MSK for Change Data Capture in YugabyteDB.
headcontent: Integrate YugabyteDB with Amazon Redshift using Amazon MKS and Debezium connector
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
menu:
  preview:
    parent: cdc-tutorials
    identifier: cdc-aws-msk
    weight: 20
type: docs
---

# Stream Data From Yugabyte CDC to AWS MSK using Debezium

Change Data Capture is a mechanism to track changes made in a database. Yugabyte Database has  added CDC feature to their core database engine. In this tutorial, we’ll learn to configure Yugabyte CDC and stream data into AWS MSK using Debezium connector.

![Diagram](/images/explore/cdc/aws_msk_images/architecture.jpg)

It’s assumed that the readers have an prior knowledge of AWS, Apache Kafka, and CDC.

Let’s start now with the setup

1.  Configuration of IAM Roles and Policies

Create a new role with the required accesses to AWS services. For demo, we’ll name it as “yb_cdc_kafka_role”. The Trusted entities should be configured as below.

_The IAM roles and Policies defined below are generic and can be fine-tuned based on your organization’s IT policies_

{“Version”: “2012–10–17”,“Statement”: [{“Effect”: “Allow”,“Principal”: {“Service”: “kafkaconnect.amazonaws.com”},“Action”: “sts:AssumeRole”}]}

Create a policy with access to the following AWS services.

1.  Apache Kafka APIs for MSK
2.  EC2
3.  MSK Connect
4.  S3
5.  CloudWatch

{“Version”: “2012–10–17”,“Statement”: [{“Sid”: “VisualEditor0”,“Effect”: “Allow”,“Action”: “ec2:CreateNetworkInterface”,“Resource”: “arn:aws:ec2:*:*:network-interface/*”,“Condition”: {“StringEquals”: {“aws:RequestTag/AmazonMSKConnectManaged”: “true”},“ForAllValues:StringEquals”: {“aws:TagKeys”: “AmazonMSKConnectManaged”}}},{“Sid”: “VisualEditor1”,“Effect”: “Allow”,“Action”: “ec2:CreateTags”,“Resource”: “arn:aws:ec2:*:*:network-interface/*”,“Condition”: {“StringEquals”: {“ec2:CreateAction”: “CreateNetworkInterface”}}},{“Sid”: “VisualEditor2”,“Effect”: “Allow”,“Action”: [“ec2:DetachNetworkInterface”,“ec2:CreateNetworkInterfacePermission”,“ec2:DeleteNetworkInterface”,“ec2:AttachNetworkInterface”],“Resource”: “arn:aws:ec2:*:*:network-interface/*”,“Condition”: {“StringEquals”: {“ec2:ResourceTag/AmazonMSKConnectManaged”: “true”}}},{“Sid”: “VisualEditor3”,“Effect”: “Allow”,“Action”: “ec2:CreateNetworkInterface”,“Resource”: [“arn:aws:ec2:*:*:subnet/*”,“arn:aws:ec2:*:*:security-group/*”]},{“Sid”: “VisualEditor4”,“Effect”: “Allow”,“Action”: [“cloudwatch:PutDashboard”,“cloudwatch:PutMetricData”,“cloudwatch:DeleteAlarms”,“kafkaconnect:ListConnectors”,“cloudwatch:DeleteInsightRules”,“cloudwatch:StartMetricStreams”,“cloudwatch:DescribeAlarmsForMetric”,“cloudwatch:ListDashboards”,“cloudwatch:ListTagsForResource”,“kafka-cluster:AlterCluster”,“kafkaconnect:CreateWorkerConfiguration”,“cloudwatch:PutAnomalyDetector”,“kafka-cluster:Connect”,“kafkaconnect:UpdateConnector”,“cloudwatch:DescribeInsightRules”,“cloudwatch:GetDashboard”,“cloudwatch:GetInsightRuleReport”,“kafka-cluster:ReadData”,“cloudwatch:DisableInsightRules”,“cloudwatch:GetMetricStatistics”,“cloudwatch:DescribeAlarms”,“cloudwatch:GetMetricStream”,“kafka-cluster:*Topic*”,“kafkaconnect:DescribeConnector”,“cloudwatch:GetMetricData”,“cloudwatch:ListMetrics”,“cloudwatch:DeleteAnomalyDetector”,“kafkaconnect:ListWorkerConfigurations”,“cloudwatch:DescribeAnomalyDetectors”,“cloudwatch:DeleteDashboards”,“kafka-cluster:AlterGroup”,“cloudwatch:DescribeAlarmHistory”,“cloudwatch:StopMetricStreams”,“cloudwatch:DisableAlarmActions”,“kafkaconnect:DescribeWorkerConfiguration”,“kafkaconnect:CreateConnector”,“kafkaconnect:ListCustomPlugins”,“cloudwatch:DeleteMetricStream”,“cloudwatch:SetAlarmState”,“kafka-cluster:DescribeGroup”,“cloudwatch:GetMetricWidgetImage”,“kafkaconnect:DescribeCustomPlugin”,“s3:*”,“kafka-cluster:DescribeCluster”,“cloudwatch:EnableInsightRules”,“cloudwatch:PutCompositeAlarm”,“cloudwatch:PutMetricStream”,“cloudwatch:PutInsightRule”,“cloudwatch:PutMetricAlarm”,“cloudwatch:EnableAlarmActions”,“cloudwatch:ListMetricStreams”,“kafkaconnect:CreateCustomPlugin”,“kafkaconnect:DeleteConnector”,“kafkaconnect:DeleteCustomPlugin”,“kafka-cluster:WriteData”],“Resource”: “*”},{“Sid”: “VisualEditor5”,“Effect”: “Allow”,“Action”: “ec2:DescribeNetworkInterfaces”,“Resource”: “arn:aws:ec2:*:*:network-interface/*”,“Condition”: {“StringEquals”: {“ec2:ResourceTag/AmazonMSKConnectManaged”: “true”}}}]}

2. Enable CDC on Yugabyte Database

Ensure that your Yugabyte Database is up and running . To install yugabyte on your cloud virtual machine, please refer to h[ttps://docs.yugabyte.com/preview/quick-start/install/macos/](https://docs.yugabyte.com/preview/quick-start/install/macos/).

Create a test table on Yugabyte database within Public schema.

CREATE TABLE test (id INT PRIMARY KEY, name TEXT);

Enable CDC through yb-admin .Below command will enable CDC on all the schemas and tables sitting under the Yugabyte database.

./yb-admin — master_addresses <master_addresses>:7100 create_change_data_stream ysql.yugabyte

If you have a multi-node yugabyte setup, then you need to provide a Comma-separated list of  **host:port**  values of both the leader and the follower nodes as master_address argument.

A successful operation of the above command returns a message with a DB stream ID:

CDC Stream ID: 90fe97d59a504bb6acbfd6a940

For more details on CDC commands, please refer to  [https://docs.yugabyte.com/preview/admin/yb-admin/#change-data-capture-cdc-commands](https://docs.yugabyte.com/preview/admin/yb-admin/#change-data-capture-cdc-commands)

3. Configuration of AWS Security Group

Create a Security Group with inbound and outbound rules configured to ensure access to MSK cluster and Yugabyte DB . For demo, we’ll enable incoming traffic from all the ports.

![](https://miro.medium.com/v2/resize:fit:700/0*GCIXUAlFVQbvCNpX)

4. Upload Debezium connector Jar file onto S3 bucket

Download Yugabyte Debezium connector jar from  [_https://github.com/yugabyte/debezium-connector-yugabytedb/releases/download/v1.9.5.y.19/debezium-connector-yugabytedb-1.9.5.y.19.jar._](https://github.com/yugabyte/debezium-connector-yugabytedb/releases/download/v1.9.5.y.19/debezium-connector-yugabytedb-1.9.5.y.19.jar)  and upload it onto an S3 bucket.

![](https://miro.medium.com/v2/resize:fit:700/0*gk4kNo4roN6w1aSJ)

5. Configuration AWS MSK cluster

In this example, we’re creating AWS MSK cluster under same VPC as that of Yugabyte Cluster . Please note that this is a generic configuration , it might differ based your organizational IT policy.

![](https://miro.medium.com/v2/resize:fit:700/0*zcTnuwYjgMLYZszE)

For demo, we have created cluster with two zones only.

![](https://miro.medium.com/v2/resize:fit:700/0*vwLr8-tZqsxGuvuO)

Under Networking Section, select VPC and Private subnets same as that of Yugabyte Cluster . Choose the security group created in step 3 from the drop down list.

![](https://miro.medium.com/v2/resize:fit:700/0*PMGfUb7LB7CjtM1C)

Enable logging on your cluster to ease debugging . In this demo, we are using S3 bucket to store the logs.

![](https://miro.medium.com/v2/resize:fit:700/0*MOv37Ars6QWVPPiv)

The cluster is now is now configured successfully.
![](https://miro.medium.com/v2/resize:fit:700/0*1_esGoZGOpGwHnDD)