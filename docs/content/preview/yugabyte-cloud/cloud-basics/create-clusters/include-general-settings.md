<!--
+++
private = true
+++
-->

### General and Database Settings

![Add Cluster Wizard - General Settings](/images/yb-cloud/cloud-addcluster-general.png)

Set the following options:

- **Cluster Name**: Enter a name for the cluster.
- **Provider**: Choose a cloud provider.
- **Security Profile**: Choose **Advanced** to configure your cluster with [added security features](../../../cloud-secure-clusters/#security-profile). Advanced security requires that the cluster be deployed in a VPC and has scheduled backups, and does not allow [public access](../../../cloud-secure-clusters/add-connections/).
- **Database Version**: Dedicated clusters are deployed using a [stable](../../../../faq/yugabytedb-managed-faq/#what-version-of-yugabytedb-does-my-cluster-run-on) release. If you have arranged a custom build with Yugabyte, it is also listed here.
- **Enhanced Postgres Compatibility**: Select this option to enable [Enhanced PostgreSQL Compatibility Mode](../../../../explore/ysql-language-features/postgresql-compatibility/) (database v2024.1.0 or later only).
