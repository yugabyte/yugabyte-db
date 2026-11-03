---
title: YugabyteDB Aeon
headerTitle: YugabyteDB Aeon
linkTitle: YugabyteDB Aeon
headcontent: Fully managed YugabyteDB-as-a-Service
aliases:
  - /stable/deploy/yugabyte-cloud/
menu:
  stable_yugabyte-cloud:
    name: "Overview"
    identifier: yugabyte-cloud
    parent: yugabytedb-managed
    weight: 1
type: indexpage
breadcrumbDisable: true
cascade:
  unversioned: true
---

YugabyteDB Aeon is a fully managed YugabyteDB-as-a-Service that allows you to run YugabyteDB clusters on Amazon Web Services (AWS), Microsoft Azure, and Google Cloud Platform (GCP).

{{<tip>}}
YugabyteDB Aeon SLA service availability is [99.99%](https://www.yugabyte.com/yugabytedb-managed-service-level-agreement/#:~:text=a%20minimum%20of-,99.99,-%25%20of%20the%20time). For the current status of the service, see [Status](https://status.yugabyte.cloud/).
{{</tip>}}

{{< sections/2-boxes >}}
  {{< sections/bottom-image-box
    title="Serverless Postgres"
    description="Agentic Multitenant PostgreSQL for AI agents and bursty or idle workloads. Free up to 1 vCPU. Fully managed by Yugabyte."
    buttonText="Learn more"
    buttonUrl="serverless/"
    imageAlt="Serverless Postgres" imageUrl="/images/homepage/serverless-postgres.svg"
  >}}

  {{< sections/bottom-image-box
    title="Distributed Postgres"
    description="Deploy single- and multi-region YugabyteDB clusters on Amazon Web Services (AWS), Microsoft Azure, and Google Cloud Platform (GCP)."
    buttonText="Learn more"
    buttonUrl="distributed-postgres/"
    imageAlt="Distributed Postgres" imageUrl="/images/homepage/locally-laptop.svg"
  >}}
{{< /sections/2-boxes >}}

## Choose the right cluster

Use [Serverless Postgres](serverless/) for AI agents, a database per tenant, and workloads that are bursty or often idle. Each agent or tenant gets an isolated PostgreSQL database. The cluster scales to zero, and you can fork a zero-copy branch when an agent or a test needs its own copy of the data. Serverless Postgres is free to start.

Use [Distributed Postgres](distributed-postgres/) for production applications that must stay available as load grows, or that serve users in more than one region. You choose the fault tolerance and topology, and deploy on Amazon Web Services (AWS), Microsoft Azure, or Google Cloud Platform (GCP). Clusters are billed per vCPU.

| | [Serverless Postgres](serverless/) | [Distributed Postgres](distributed-postgres/) |
| :--- | :--- | :--- |
| Best for | AI agents, tenants, and workloads that are idle between bursts | Production applications that need high availability, more nodes, or several regions |
| Start | Free, up to 2 vCPU, 16 GiB RAM, and 100 GiB storage, shared by the account | Billed per vCPU. [Free trial](managed-freetrial/) credits are available |
| Availability | Scales to zero when idle. Active tenants share CPU fairly | Fault tolerance across nodes, availability zones, or regions |
| Topology | A shared pool. Fork zero-copy branches for separate copies of the data | Single region, replicate across regions, partition by region, and read replicas |
| Scale | Resize the vCPU and memory of an endpoint | Add or remove nodes, or change vCPUs and disk size |
| Network | Public endpoint with trusted IP addresses | VPC peering or a private link, plus IP allow lists |
| Next step | Move to a Distributed Postgres cluster without a migration | Scale the cluster you already have |

## More resources

{{< sections/3-boxes >}}
  {{< sections/3-box-card
  title="Manage your accounts"
  linkText1="Add users and accounts"
  linkUrl1="managed-security/"
  linkText2="Manage billing"
  linkUrl2="cloud-admin/cloud-billing-profile/"
  linkText3="Automation"
  linkUrl3="managed-automation/"
  >}}

  {{< sections/3-box-card
  title="More Information"
  linkText1="What's new"
  linkUrl1="release-notes/"
  linkText2="Troubleshoot"
  linkUrl2="cloud-troubleshoot/"
  linkText3="FAQ"
  linkUrl3="../faq/yugabytedb-managed-faq/"
  >}}

  {{< sections/3-box-card
  title="Support"
  linkText1="Contact Support"
  linkUrl1="https://support.yugabyte.com/hc/en-us/requests/new?ticket_form_id=360003113431"
  linkTarget1="_blank"
  linkText2="YugabyteDB Aeon Status"
  linkUrl2="https://status.yugabyte.cloud/"
    linkTarget2="_blank"
  linkText3="Join Our Community"
  linkUrl3="https://inviter.co/yugabytedb"
  linkTarget3="_blank"
  >}}
{{< /sections/3-boxes >}}

## Continue learning

{{< sections/3-boxes >}}
  {{< sections/3-box-card
  title="Build Applications"
  description="Start coding in your favorite programming language using these examples."
  buttonText="Get Started"
  buttonUrl="/stable/develop/tutorials/build-apps/"
  >}}

  {{< sections/3-box-card
  title="Yugabyte University"
  linkText1="YugabyteDB Aeon Basics"
  linkUrl1="https://university.yugabyte.com/courses/yugabytedb-managed-basics"
  linkTarget1="_blank"
  linkText2="YSQL Exercises: Basic queries"
  linkUrl2="https://university.yugabyte.com/courses/ysql-exercises-simple-queries"
  linkTarget2="_blank"
  >}}

  {{< sections/3-box-card
  title="YouTube Channel"
  description="Learn how to use YugabyteDB Aeon with this series of short tutorials."
  buttonText="Watch"
  buttonUrl="https://www.youtube.com/playlist?list=PL8Z3vt4qJTkIAYWaUOE_CIntxTHho_pBh"
  buttonTarget="_blank"
  >}}
{{< /sections/3-boxes >}}
