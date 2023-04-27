---
title: Build global applications
headerTitle: Build global applications
linkTitle: Build global applications
description: Build globally distributed applications.
headcontent: Learn how to design globally distributed applications using simple patterns
image: /images/section_icons/quick_start/sample_apps.png
menu:
  preview:
    identifier: build-global-apps
    parent: develop
    weight: 201
type: indexpage
---

In today's fast-paced world, the internet and cloud technology have revolutionized the way people interact and operate. The growing interconnectivity of the global community has made applications accessible to users worldwide, allowing for seamless communication and collaboration on a global scale.

Consequently, a new class of applications must be developed to cater to the diverse needs of users. These applications must be built in multiple locations for various reasons, such as:

- Keep user's data in a certain geographical boundary
- Provide low latency for users across the globe
- Be resilient to failures to provide un-interrupted service to users

Let's understand the reasons why today's applications have to be global in detail.

## The need for global applications

### High Availability

Although public clouds have come a long way since the inception of AWS in 2006, region and zone outages are still fairly common, happening once or twice a year (cf. [AWS Outages](https://en.wikipedia.org/wiki/Timeline_of_Amazon_Web_Services#Amazon_Web_Services_outages), [Google Outages](https://en.wikipedia.org/wiki/Google_services_outages#:~:text=During%20eight%20episodes%2C%20one%20in,Google%20service%20in%20August%202013)). You must run your applications in multiple locations so that you can provide uninterrupted service to your users.

{{<tip>}}
To make your global applications fault-tolerant and highly available, see  [Basic Design Patterns](./design-patterns-basic)
{{</tip>}}

### Compliance

To comply with data residency laws in each country, companies operating in that country must ensure that the data of their citizens is stored on servers located within that country (for example, the [GDPR](https://en.wikipedia.org/wiki/General_Data_Protection_Regulation)). This means that companies need to design their applications to split data across geographies accordingly.

{{<tip>}}
To understand various paradigms that can help you with complying to data residency laws, see [Compliance Patterns](./global-performance)
{{</tip>}}

### Globally distributed users

When designing today's applications (eg. email, e-commerce websites, or broadcasting events like the Olympics), it's essential to consider that users could be located in various geographies. For instance, if your application is hosted in data centers located in the US, users in Europe might encounter high latency when trying to access your application. To provide the best user experience, it's crucial to run your applications closer to your users.

{{<tip>}}
To enhance the performance of your global applications, see  [Performance Patterns](./global-performance)
{{</tip>}}


## Application Design Patterns

Running applications in different data centers with data split across them is not a trivial task. But YugabyteDB can be deployed in various configurations like single-region multi-zone configuration or multi-region multi-zone. You can leverage some of our battle-tested design paradigms, which offer solutions to common problems faced in these scenarios. By adopting such design patterns, your application development can be significantly accelerated. These proven paradigms offer solutions that can save time and resources that would otherwise be spent reinventing the wheel. For example,

| Pattern | Description |
| ------- | ----------- |
| [Read Replicas](./design-patterns-basic#unidirectional-async-replication-with-read-replicas) | Separate follower cluster for reducing read latency |
| [Stretch cluster](./design-patterns-basic#sync-replication-with-stretch-cluster) | Distribute your cluster across different regions |
| [Multi Master](./design-patterns-basic#bidirectional-async-replication-with-xcluster) | Dual cluster than can handle writes |
| [Standby Cluster](./design-patterns-basic#unidirectional-async-replication-with-xcluster) | Second cluster than can be used for deploy testing |
| [Tables in a different location](./design-patterns-compliance#pinning-tables-to-local-geographies) | Place tables in different geographies |
| [Rows in a different location](./design-patterns-compliance#pinning-partitions-to-local-geographies) | Split your table and place specific rows in a different geography |
| [Automatic load balancing](./global-apps-smart-driver#topology-aware-load-balancing) | Load balance your cluster with no cost |

Adopting such design patterns can vastly accelerate your application development. These are proven paradigms that would save without having to reinvent solutions.

{{<tip>}}
For more design patterns, see  [Design Patterns for global applications](./design-patterns)
{{</tip>}}

