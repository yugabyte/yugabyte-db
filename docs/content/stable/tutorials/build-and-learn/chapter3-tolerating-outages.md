---
title: Tolerating outages
headerTitle: "Chapter 3: Tolerating outages with YugabyteDB"
linkTitle: Tolerating outages
description: Make the YugaPlus service highly-available by using the smart driver and deploying YugabyteDB across several data centers. 
menu:
  stable:
    identifier: chapter3-tolerating-outages
    parent: tutorials-build-and-learn
    weight: 4
type: docs
---

It was one busy evening, when the YugaPlus team was alarmed about a service outage. The streaming platform was down and customers began discussing the incident on social media. In a few minutes the team discovered that one of the virtual machines (VM) was impacted by a cloud-level outage. That VM and a database node that was running on it became unavailable. But, even though the YugabyteDB cluster lost one of the nodes, there was no data loss, and the cluster was ready to continue serving the application workload. However, the application instance was connected to the database node that was no longer available.

Eventually, the team restored the YugaPlus service by connecting the application layer to the remaining nodes. Also, they decided to explore how to improve the high availability characteristics of the service to prevent all sorts of possible outages in the future...

In this chapter you'll learn:

* Smart driver
* How to tolerate zone or region-level outages
