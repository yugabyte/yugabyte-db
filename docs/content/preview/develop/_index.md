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

Although YugabyteDB is fully SQL compatible, modeling your data for a distributed database is quite different from modeling for a monolithic database like MySQL or PostgreSQL. This is because the table data is distributed across different nodes. You must understand how to model your data for efficient storage and retrieval from a distributed system.

{{<lead link="./data-modeling/">}}
To understand how to model your data for YugabyteDB, see [Distributed data modeling](./data-modeling/)
{{</lead>}}

## Global applications

Today's applications have to cater to users distributed across the globe. Running applications across multiple data centers and at the same time providing the best user experience is no trivial task. We have come up with some battle-tested design patterns for your global applications.

{{<lead link="./build-global-apps/">}}
To learn more about building global applications, see [Build global applications](./build-global-apps/)
{{</lead>}}

## Multi-cloud applications

A multi-cloud strategy provides you with the flexibility to use the optimal computing environment for each specific workload helps avoid vendor lock-in, lets you place data close to the users and can minimize cost choosing optimal pricing and performance of various cloud providers. You can also opt for a hybrid model as your path to migration onto the cloud.

{{<lead link="./multi-cloud/">}}
To understand how easily you can do a multi-cloud setup with YugabyteDB, see [Build multi-cloud applications](./multi-cloud/)
{{</lead>}}

## Application development

Building scalable applications on top of YugabyteDb is very simple. Yet you need to understand certain fundamental concepts like Transactions, search and so on and ensure that you make the best use of them.

{{<lead link="./learn/">}}
To learn how to build applications on top of YugabyteDB, see [Learn app development](./learn/)
{{</lead>}}

## Best practices

Although building applications on top of YugabyteDB is very simple, you must follow certain tips and tricks for your applications to perform well in a distributed environment. We have compiled a list of best practices that you can adopt to make your application perform the best.

{{<lead link="./best-practices-ysql">}}
For the list of interesting tips, see [Best practices](./best-practices-ysql)
{{</lead>}}

## Drivers and ORMs

To communicate with YugabyteDB, applications need to use drivers. Applications can also be built Object-Relational mappings, a technique used to communicate with the database using object-oriented techniques. We've tested various drivers and ORMs in multiple languages and have come up with the optimal configurations for you to use swiftly.

{{<lead link="../drivers-orms/">}}
For the list of the various drivers and ORMs, see [Drivers and ORMs](../drivers-orms/)
{{</lead>}}

## Quality of service

Although YugabyteDB can scale horizontally when needed, it has a lot of safety measures and knobs such as rate-limitting, admission control, transaction priorities etc, that helps it to maintain a high quality of service for all users when the systems come under sudden heavy load.

{{<lead link="./quality-of-service/">}}
To learn more about how to use rate-limiting and other features to keep the system under heavy loads, see [Quality of service](./quality-of-service/)
{{</lead>}}

## Cloud-native development

Cloud-native development refers to building and running applications that fully exploit the advantages of cloud computing without needing to install any software on your development machine. Two prominent tools for cloud-native development environments are Gitpod and GitHub Codespaces. Both provide cloud-based development environments, but they have their own features and use cases.

{{<lead link="./gitdev/">}}
To learn more about how to use browser-based IDEs, see [Cloud-native development](./gitdev/)
{{</lead>}}

## Tutorials

We have come up with multiple step-by-step guides for building scalable and fault-tolerant applications with YugabyteDB using your favorite programming language, services, and frameworks like Kafka, Gen-AI etc.

{{<lead link="../tutorials/">}}
For the list of step-by-step guides for various frameworks, see [Tutorials](../tutorials/)
{{</lead>}}
