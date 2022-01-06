---
title: Connect using cloud shell
linkTitle: Cloud shell
description: Connect to Yugabyte Cloud clusters from any browser using the cloud shell
headcontent:
image: /images/section_icons/deploy/enterprise.png
menu:
  latest:
    identifier: connect-cloud-shell
    parent: cloud-connect
    weight: 10
isTocNested: true
showAsideToc: true
---

Use any browser to connect to Yugabyte Cloud by using the cloud shell. Cloud shell doesn't require a CA certificate or any special network access configured.

You have the option of using the following command line interfaces (CLIs) in the cloud shell:

- [ysqlsh](../../../admin/ysqlsh/) - YSQL shell for interacting with YugabyteDB using the [YSQL API](../../../api/ysql).
- [ycqlsh](../../../admin/ycqlsh/) - YCQL shell, which uses the [YCQL API](../../../api/ycql).

## Connect via Cloud Shell

To connect to a cluster via Cloud Shell:

1. On the **Clusters** tab, select a cluster.

1. Click **Connect**.

1. Click **Launch Cloud Shell**.

1. Enter the database name and user name.

1. Select the API to use (YSQL or YCQL) and click **Confirm**.

    The shell is displayed in a separate browser page. Cloud shell can take up to 30 seconds to be ready.

1. Enter the password for the user you specified.

The `ysqlsh` or `ycqlsh` prompt appears and is ready to use.

```output
ysqlsh (11.2-YB-2.6.1.0-b0)
SSL connection (protocol: TLSv1.2, cipher: ECDHE-RSA-AES256-GCM-SHA384, bits: 256, compression: off)
Type "help" for help.

yugabyte=#
```

```output
Connected to local cluster at 3.69.145.48:9042.
[ycqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
admin@ycqlsh:yugabyte> 
```

If you enter an incorrect password, the cloud shell session is terminated immediately and you must start a new session.

Once connected, one or more entries for the cloud shell session are added to the cluster IP allow list. After the session is closed, these are cleaned up automatically within five minutes.

{{< tip title="Cloud shell limitations" >}}

Cloud shell is updated regularly. Check the [known issues list](../../release-notes/#known-issues-in-cloud-shell) in the release notes for the most-current list of limitations and known issues.

{{< /tip >}}

## Related information

- [YSQL API](../../../api/ysql/) — Reference for supported YSQL statements, data types, functions, and operators.
- [YCQL API](../../../api/ycql/) — Reference for supported YCQL statements, data types, functions, and operators.
- [ysqlsh](../../../admin/ysqlsh/) — Overview of the CLI, syntax, and commands.
- [ycqlsh](../../../admin/ycqlsh/) — Overview of the CLI, syntax, and commands.

## Next steps

- [Add database users](../../cloud-secure-clusters/add-users/)
- [Connect an application](../connect-applications/)
