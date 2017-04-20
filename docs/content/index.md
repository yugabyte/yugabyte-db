---
date: 2016-03-08T21:07:13+01:00
title: Welcome to YugaByte Docs
type: index
weight: 0
---

## Introduction

YugaByte is a new cloud native, tunably consistent, multi-model NoSQL database that’s built ground-up for today’s mission-critical Online Transaction Processing (OLTP) and Hybrid Transactional & Analytical (HTAP) applications. It brings together the best of two worlds (strong consistency from SQL/NewSQL and high availability & multi-model from NoSQL) while adding the critical aspect of cloud native for modern technical operations. It’s designed with 3 foundational principles in mind: operational simplicity, developer productivity and customer delight. 

![YugaByte Design Principles](/images/design-principles.png)

## Features

### Cloud native technical operations

- **Simple scalability**: Linear, fast & reliable scalability from 1 to 1000s nodes with automatic sharding and rebalancing
- **Zero downtime and zero data loss operations**: Highly available under any unplanned infrastructure failure or any planned software/hardware upgrade
- **Multi-data center deployments simplified**: 1-click geo-redundant deployments across multiple availability zones & regions on public/private/hybrid clouds as well as across multiple on-premises data centers 
- **Online cross-cloud mobility**: move across cloud infrastructure providers without any lock-in
- **Hardware flexibility**: seamlessly move from one type of compute and storage to another for cost and performance reasons

### Agile application development

- **Apache Cassandra & Redis compatible**: Choose from 2 popular NoSQL options, [Apache Cassandra Query Language (CQL)](https://docs.datastax.com/en/cql/3.1/cql/cql_reference/cqlReferenceTOC.html) and [Redis] (https://redis.io/commands)
- **Tunable consistency**: Strongly consistent writes and linearizable reads as default, additional tunable bounded staleness options for reads
- **Multi-model**: Support for flexible schema, time series and key-value workloads with converged cache benefits


## Enterprise apps best served

Three types of mission-critical OLTP/HTAP enterprise applications are best served by YugaByte. These map to the three types of data models offered.

### Flexible schema

OLTP applications (such as retail product catalog, unified customer profile, global identity, transactional systems-of-record, centralized configuration management etc.) that require a semi-structured yet easily changeable schema. These applications can benefit heavily from YugaByte’s ability to add/remove attributes in the schema in a completely online manner without any hidden performance issues. Such applications use YugaByte’s CQL support.

### Time series

HTAP applications (such as application/infrastructure monitoring as well as industrial/consumer Internet-of-Things) that require high write throughput for append-only workloads deployed on cost-efficient tiered storage. These applications also use YugaByte’s CQL support.

### Key-value

Mission-critical key-value applications (such as financial transaction processing, stock quotes and sports feeds) that require low latency reads and writes without the overhead of explicit memory management. Such applications use YugaByte’s Redis API support.
