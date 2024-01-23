---
title: Going global
headerTitle: "Chapter 4: Going global with YugabyteDB"
linkTitle: Going global
description: Scaling read and write workloads across distant locations with geo-partitioning and other design patterns for global application
menu:
  stable:
    identifier: chapter4-going-global
    parent: tutorials-build-and-learn
    weight: 5
type: docs
---

The popularity of the YugaPlus service kept growing rapidly. In the United States alone, millions of users watched the content daily. And with this level of growth, the YugaPlus team faced the next set of challenges. The team noticed that most of the negative feedback of the service responsiveness was originating from the East Coast. At the same time, those who lived in the West Coast were always positive about the YugaPlus experience. Eventually, the team found the root cause for the poor user experience in the East Coast of the country. The YugabyteDB cluster was deployed a cloud region on the West Coast and the requests of those living in the East Coast incurred much higher latency.

Eventually, the YugaPlus team decided to explore various design patterns for global applications and picked one of the patterns that helped them to provide the same level of experience to all the users in the USA regardless of their location...

In this chapter you'll learn:

* Various design patterns for global applications
* How to use the geo-partitioned cluster in practice
