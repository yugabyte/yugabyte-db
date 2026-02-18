<!--
+++
private=true
+++
-->

### Dependencies

YugabyteDB Voyager requires the following dependencies on supported Ubuntu systems. These packages are automatically installed or resolved when you install Voyager using the official package.

| Dependency | Version / Constraint |
| :--------- | :------------------- |
| binutils | > 2.25 |
| debezium | = 2.5.2-\<voyager_version\> |
| libmysqlclient-dev | – |
| oracle-instantclient-basic | = 21.5.0.0.0-1 |
| oracle-instantclient-devel | = 21.5.0.0.0-1 |
| oracle-instantclient-jdbc | = 21.5.0.0.0-1 |
| oracle-instantclient-sqlplus | = 21.5.0.0.0-1 |
| oracle-instantclient-tools | = 21.5.0.0.0-1 |
| ora2pg | = 23.2-yb.2 |
| postgresql-client-17 | – |
| sqlite3 | – |

Perform the following steps to install yb-voyager using apt for Ubuntu:

{{< note title="Note" >}}
`apt` installation is only supported for Ubuntu 22. For other versions such as 18 and 20, use the install script via the Source installation option.
{{< /note >}}

1. Install the Yugabyte apt repository on your machine using the following command:

    ```sh
    wget https://software.yugabyte.com/repos/reporpms/yb-apt-repo_1.0.0_all.deb
    sudo apt-get install ./yb-apt-repo_1.0.0_all.deb
    ```

    This repository contains the `yb-voyager` Debian package and the dependencies required to run `yb-voyager`.

1. Install the `postgresql-common` repository to fetch PostgreSQL 16 using the following commands:

    ```sh
    sudo apt install -y postgresql-common
    sudo /usr/share/postgresql-common/pgdg/apt.postgresql.org.sh
    ```

1. Clean the `apt-get` cache and package lists using the following commands:

    ```sh
    sudo apt-get clean
    sudo apt-get update
    ```

1. Install yb-voyager and its dependencies using the following command:

    ```sh
    sudo apt-get install yb-voyager
    ```

    Note: If you see a failure in the install step similar to the following:

    ```output
    Depends: ora2pg (= 23.2-yb.2) but 24.3-1.pgdg22.04+1 is to be installed
    ```

    Try installing ora2pg using the following command:

    ```sh
    sudo apt-get install ora2pg=23.2-yb.2
    ```

    Then try installing yb-voyager using the following command:

    ```sh
    sudo apt-get install yb-voyager
    ```

1. Check that yb-voyager is installed using the following command:

    ```sh
    yb-voyager version
    ```

### Upgrade yb-voyager

{{< note title="Note" >}}
If you are upgrading Voyager from version 1.8.0 or earlier, you need to install the `postgresql-common` repository before the upgrade as follows:

```sh
sudo apt install -y postgresql-common
sudo /usr/share/postgresql-common/pgdg/apt.postgresql.org.sh
```

{{< /note >}}

Upgrade yb-voyager using the following command:

```sh
sudo apt-get upgrade yb-voyager
```
