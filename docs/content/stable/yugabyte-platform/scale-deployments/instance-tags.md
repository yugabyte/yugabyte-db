---
title: Create and edit instance tags
headerTitle: Create and edit instance tags
linkTitle: Configure instance tags
description: Use YugabyteDB Anywhere to create and edit instance tags.
aliases:
  - /stable/manage/enterprise-edition/instance-tags/
  - /stable/yugabyte-platform/manage-deployments/instance-tags/
menu:
  stable_yugabyte-platform:
    identifier: instance-tags
    parent: scale-deployments
    weight: 50
type: docs
---

The instances created on a cloud provider can be assigned special metadata to help manage, bill, or audit the resources.

On Amazon Web Services (AWS), they are referred to as
[instance tags](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/Using_Tags.html). In the context of YugabyteDB Anywhere, these tags are known as user tags.

You can define user tags when you create a new universe, as well as modify or delete tags of an existing universe. These tags are represented by key-value pairs.

## Add user tags

To add or edit user tags, navigate to the universe and do the following:

1. {{<tags/ui/new>}} Click **Settings > Advanced > Other Advanced Settings**, and under **User Tags** click **Edit**.

    {{<tags/ui/classic>}} Click **Actions > Edit Universe**, under **User Tags**.

1. Click **Add User Tag** to add tags.

1. Edit the tags.

You can define the tags in any order.

**Name** is the only key that can have `templated` tags, so it can be made of different parts filled in at runtime to determine node and instance names, based on the following guidelines:

- The parts of the template should be enclosed between `${` and `}`.
- The reserved keywords that can be specified: `universe`, `instance-id`, `zone`, and `region`.
- Templated tag value must have `instance-id` at the minimum.
- Order of the parts of template does not matter.

## Cloud provider tags and user tags

User tags are reflected on the cloud provider's instances page.

For example, for AWS, navigate to **EC2 > Running Instances** in the correct availability zone and search for instances that have `test-tags` in their name. You should see the following under **Tags** of those instances:

![Instances with tags](/images/ee/inst-tags-aws-1.png)

`yb-server-type` and `launched-by` are reserved names.

Suppose you modified the existing user tags, as per the following illustration:

![Instances with tags](/images/ee/inst-tags-2.png)

The following changes have been made:

- `Billing` was modified.
- `MyInfo` was deleted.
- `NewInfo` was added.
- `Department` was not changed.

Note that you cannot change the **Name** key field.

Once again, you can confirm via the cloud provider list of instances that the tags have been updated correctly, as per the following illustration:

![Edited instances with tags](/images/ee/inst-tags-aws-2.png)
