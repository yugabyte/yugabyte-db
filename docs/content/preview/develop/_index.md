---
title: Develop applications
headerTitle: Develop
linkTitle: Develop
description: Build YugabyteDB application that use ecosystem integrations and GraphQL.
headcontent: Get started building applications based on YugabyteDB
image: /images/section_icons/index/develop.png
type: indexpage
---

## Data modeling

Although YugabyteDB is fully SQL compatible, modeling data for a distributed database is quite different from modeling for a monolithic database like MySQL or PostgreSQL. This is because the table data is distributed across different nodes. You must understand how to model your data for efficient storage and retrieval from a distributed system.

{{<lead link="./data-modeling/">}}
To understand how to model your data for YugabyteDB, see [Distributed data modeling](./data-modeling/).
{{</lead>}}

## Global applications

Today's applications have to cater to users distributed across the globe. Running applications across multiple data centers while providing the best user experience is no trivial task. Yugabyte provides some battle-tested design patterns for your global applications.

{{<lead link="./build-global-apps/">}}
To learn more about building global applications, see [Build global applications](./build-global-apps/).
{{</lead>}}

## Multi-cloud applications

A multi-cloud strategy provides the flexibility to use the optimal computing environment for each specific workload, helps avoid vendor lock-in, lets you place data close to the users, and can minimize cost by choosing optimal pricing and performance of various cloud providers. You can also opt for a hybrid model as your path to migration onto the cloud.

{{<lead link="./multi-cloud/">}}
To understand how to build a multi-cloud setup with YugabyteDB, see [Build multi-cloud applications](./multi-cloud/).
{{</lead>}}

## Application development

Although building scalable applications on top of YugabyteDB is straightforward, you need to understand certain fundamental concepts like transactions, search, and more to make the best use of them.

{{<lead link="./learn/">}}
To learn how to build applications on top of YugabyteDB, see [Learn app development](./learn/).
{{</lead>}}

## Best practices

Use these best practices to build distributed applications on top of YugabyteDB; this includes a list of techniques that you can adopt to make your application perform its best.

{{<lead link="./best-practices-ysql">}}
For more details, see [Best practices](./best-practices-ysql).
{{</lead>}}

## Drivers and ORMs

To communicate with YugabyteDB, applications need to use drivers. Applications can also be built using Object-Relational mappings, a technique used to communicate with the database using object-oriented techniques. We've tested various drivers and ORMs in multiple languages with the optimal configurations to get your applications up and running.

{{<lead link="../drivers-orms/">}}
For the list of drivers and ORMs with sample code, see [Drivers and ORMs](../drivers-orms/).
{{</lead>}}

## Quality of service

Although YugabyteDB can scale horizontally when needed, it also includes safety measures and settings such as rate-limiting, admission control, transaction priorities, and more, to ensure applications can maintain a high quality of service for all users when the systems comes under heavy load.

{{<lead link="./quality-of-service/">}}
To learn more about how to use rate-limiting and other features, see [Quality of service](./quality-of-service/).
{{</lead>}}

## Cloud-native development

Cloud-native development refers to building and running applications that fully exploit the advantages of cloud computing without needing to install any software on your development machine. Two prominent tools for cloud-native development environments are Gitpod and GitHub Codespaces. Both provide cloud-based development environments, but they have their own features and use cases.

{{<lead link="./gitdev/">}}
To learn more about how to use browser-based IDEs, see [Cloud-native development](./gitdev/).
{{</lead>}}

## Tutorials

Yugabyte provides multiple step-by-step guides for building scalable and fault-tolerant applications with YugabyteDB using your favorite programming language, services, and frameworks, including Kafka, Gen-AI, and more.

{{<lead link="../tutorials/">}}
For step-by-step guides for various frameworks, see [Tutorials](../tutorials/).
{{</lead>}}
