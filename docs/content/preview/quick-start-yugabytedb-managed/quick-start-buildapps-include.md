<!--
+++
private = true
+++
-->

Choose the language you want to use to build your application.

{{< tabpane text=true >}}

  {{% tab header="Java" lang="java" %}}

The [Java application](https://github.com/yugabyte/yugabyte-simple-java-app) connects to a YugabyteDB cluster using the topology-aware [Yugabyte JDBC driver](/preview/drivers-orms/java/) and performs basic SQL operations. Use the application as a template to get started with YugabyteDB in Java.

The application requires the following:

- Java Development Kit (JDK) 1.8, or later, is installed. JDK installers for Linux and macOS can be downloaded from [Oracle](http://jdk.java.net/), [Adoptium (OpenJDK)](https://adoptium.net/), or [Azul Systems (OpenJDK)](https://www.azul.com/downloads/?package=jdk). Homebrew users on macOS can install using `brew install openjdk`.
- [Apache Maven](https://maven.apache.org/index.html) 3.3 or later.

To build and run the application, do the following:

1. Clone the sample application to your computer:

    ```sh
    git clone https://github.com/YugabyteDB-Samples/yugabyte-simple-java-app.git && cd yugabyte-simple-java-app
    ```

1. Provide connection parameters.

    (You can skip this step and use the defaults if your cluster is running locally and listening on 127.0.0.1:5433.)

    The application needs to establish a connection to the YugabyteDB cluster. To do this:

    - Open the `app.properties` file located in the application `src/main/resources/` folder.

    - Set the following configuration parameters:

        - **host** - the host name of your YugabyteDB cluster. For local clusters, use the default (127.0.0.1). For YugabyteDB Managed, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
        - **port** - the port number for the driver to use (the default YugabyteDB YSQL port is 5433).
        - **database** - the name of the database you are connecting to (the default is `yugabyte`).
        - **dbUser** and **dbPassword** - the username and password for the YugabyteDB database. For local clusters, use the defaults (`yugabyte`). For YugabyteDB Managed, use the credentials in the credentials file you downloaded.

    - YugabyteDB Managed [requires SSL connections](/preview/yugabyte-cloud/cloud-secure-clusters/cloud-authentication/#ssl-modes-in-ysql), so you need to set the following additional parameters:

        - **sslMode** - the SSL mode to use; use `verify-full`.
        - **sslRootCert** - the full path to the YugabyteDB Managed [cluster CA certificate](/preview/yugabyte-cloud/cloud-secure-clusters/cloud-authentication/).

    - Save the file.

1. Build the application.

    ```sh
    $ mvn clean package
    ```

1. Start the application.

    ```sh
    $ java -cp target/yugabyte-simple-java-app-1.0-SNAPSHOT.jar SampleApp
    ```

If you are running the application on a free or single node cluster, the driver displays a warning that the load balance failed and will fall back to a regular connection.

You should see output similar to the following:

```output
>>>> Successfully connected to YugabyteDB!
>>>> Successfully created DemoAccount table.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 10000
name = John, age = 28, country = Canada, balance = 9000

>>>> Transferred 800 between accounts.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 9200
name = John, age = 28, country = Canada, balance = 9800
```

You have successfully executed a basic Java application that works with YugabyteDB.

[Explore the application logic](/preview/tutorials/build-apps/java/cloud-ysql-yb-jdbc/#explore-the-application-logic)

  {{% /tab %}}

  {{% tab header="Go" lang="go" %}}

The [Go application](https://github.com/yugabyte/yugabyte-simple-go-app) connects to a YugabyteDB cluster using the [Go PostgreSQL driver](/preview/drivers-orms/go/) and performs basic SQL operations. Use the application as a template to get started with YugabyteDB in Go.

The application requires the following:

- [Go](https://go.dev/dl/) (tested with version 1.17.6).

To build and run the application, do the following:

1. Clone the sample application to your computer:

    ```sh
    git clone https://github.com/YugabyteDB-Samples/yugabyte-simple-go-app.git && cd yugabyte-simple-go-app
    ```

1. Provide connection parameters.

    (You can skip this step and use the defaults if your cluster is running locally and listening on 127.0.0.1:5433.)

    The application needs to establish a connection to the YugabyteDB cluster. To do this:

    - Open the `sample-app.go` file.

    - Set the following configuration parameter constants:

        - **host** - the host name of your YugabyteDB cluster. For local clusters, use the default (127.0.0.1). For YugabyteDB Managed, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
        - **port** - the port number for the driver to use (the default YugabyteDB YSQL port is 5433).
        - **dbName** - the name of the database you are connecting to (the default is `yugabyte`).
        - **dbUser** and **dbPassword** - the username and password for the YugabyteDB database. For local clusters, use the defaults (`yugabyte`). For YugabyteDB Managed, use the credentials in the credentials file you downloaded.

    - YugabyteDB Managed [requires SSL connections](/preview/yugabyte-cloud/cloud-secure-clusters/cloud-authentication/#ssl-modes-in-ysql), so you need to set the following additional parameters:

        - **sslMode** - the SSL mode to use; use `verify-full`.
        - **sslRootCert** - the full path to the YugabyteDB Managed [cluster CA certificate](/preview/yugabyte-cloud/cloud-secure-clusters/cloud-authentication/).

    - Save the file.

1. Initialize the `GO111MODULE` variable.

    ```sh
    $ export GO111MODULE=auto
    ```

1. Import the Go PostgreSQL driver.

    ```sh
    $ go get github.com/lib/pq
    ```

1. Start the application.

    ```sh
    $ go run sample-app.go
    ```

You should see output similar to the following:

```output
>>>> Successfully connected to YugabyteDB!
>>>> Successfully created table DemoAccount.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 10000
name = John, age = 28, country = Canada, balance = 9000
>>>> Transferred 800 between accounts.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 9200
name = John, age = 28, country = Canada, balance = 9800
```

You have successfully executed a basic Go application that works with YugabyteDB.

[Explore the application logic](/preview/tutorials/build-apps/go/cloud-ysql-go/#explore-the-application-logic)

  {{% /tab %}}

  {{% tab header="Python" lang="python" %}}

The [Python application](https://github.com/yugabyte/yugabyte-simple-python-app) connects to a YugabyteDB cluster using the [Python psycopg2 PostgreSQL database adapter](/preview/drivers-orms/python/) and performs basic SQL operations. Use the application as a template to get started with YugabyteDB in Python.

The application requires the following:

- Python 3.6 or later (Python 3.9.7 or later if running macOS on Apple silicon).

To build and run the application, do the following:

1. Clone the sample application to your computer:

    ```sh
    git clone https://github.com/YugabyteDB-Samples/yugabyte-simple-python-app.git && cd yugabyte-simple-python-app
    ```

1. Provide connection parameters.

    (You can skip this step and use the defaults if your cluster is running locally and listening on 127.0.0.1:5433.)

    The application needs to establish a connection to the YugabyteDB cluster. To do this:

    - Open the `sample-app.py` file.

    - Set the following configuration parameter constants:

        - **host** - the host name of your YugabyteDB cluster. For local clusters, use the default (127.0.0.1). For YugabyteDB Managed, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
        - **port** - the port number for the driver to use (the default YugabyteDB YSQL port is 5433).
        - **dbName** - the name of the database you are connecting to (the default is `yugabyte`).
        - **dbUser** and **dbPassword** - the username and password for the YugabyteDB database. For local clusters, use the defaults (`yugabyte`). For YugabyteDB Managed, use the credentials in the credentials file you downloaded.

    - YugabyteDB Managed [requires SSL connections](/preview/yugabyte-cloud/cloud-secure-clusters/cloud-authentication/#ssl-modes-in-ysql), so you need to set the following additional parameters:

        - **sslMode** - the SSL mode to use; use `verify-full`.
        - **sslRootCert** - the full path to the YugabyteDB Managed cluster CA certificate.

    - Save the file.

1. Install psycopg2 PostgreSQL database adapter.

    ```sh
    $ pip3 install psycopg2-binary
    ```

1. Start the application.

    ```sh
    $ python3 sample-app.py
    ```

You should see output similar to the following:

```output
>>>> Successfully connected to YugabyteDB!
>>>> Successfully created table DemoAccount.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 10000
name = John, age = 28, country = Canada, balance = 9000
>>>> Transferred 800 between accounts.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 9200
name = John, age = 28, country = Canada, balance = 9800
```

You have successfully executed a basic Python application that works with YugabyteDB.

[Explore the application logic](/preview/tutorials/build-apps/python/cloud-ysql-python/#explore-the-application-logic)

  {{% /tab %}}

  {{% tab header="Node.js" lang="nodejs" %}}

The [Node.js application](https://github.com/yugabyte/yugabyte-simple-node-app) connects to a YugabyteDB cluster using the [node-postgres driver](/preview/drivers-orms/nodejs/) and performs basic SQL operations. Use the application as a template to get started with YugabyteDB in Node.js.

The application requires the following:

- The latest version of [Node.js](https://nodejs.org/en/download/).

To build and run the application, do the following:

1. Clone the sample application to your computer:

    ```sh
    git clone https://github.com/YugabyteDB-Samples/yugabyte-simple-node-app.git && cd yugabyte-simple-node-app
    ```

1. Provide connection parameters.

    (You can skip this step and use the defaults if your cluster is running locally and listening on 127.0.0.1:5433.)

    The application needs to establish a connection to the YugabyteDB cluster. To do this:

    - Open the `sample-app.js` file.

    - Set the following configuration parameter constants:

        - **host** - the host name of your YugabyteDB cluster. For local clusters, use the default (127.0.0.1). For YugabyteDB Managed, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
        - **port** - the port number for the driver to use (the default YugabyteDB YSQL port is 5433).
        - **database** - the name of the database you are connecting to (the default is `yugabyte`).
        - **user** and **password** - the username and password for the YugabyteDB database. For local clusters, use the defaults (`yugabyte`). For YugabyteDB Managed, use the credentials in the credentials file you downloaded.

    - YugabyteDB Managed [requires SSL connections](/preview/yugabyte-cloud/cloud-secure-clusters/cloud-authentication/#ssl-modes-in-ysql), so you need to set the following additional parameters:

        - **ssl** - To enable `verify-ca` SSL mode, the `rejectUnauthorized` property is set to `true` to require root certificate chain validation; replace `path_to_your_root_certificate` with the full path to the YugabyteDB Managed [cluster CA certificate](/preview/yugabyte-cloud/cloud-secure-clusters/cloud-authentication/).

    - Save the file.

1. Install the node-postgres module.

    ```sh
    npm install pg
    ```

1. Install the [async](https://github.com/caolan/async) utility:

    ```sh
    npm install --save async
    ```

1. Start the application.

    ```sh
    $ node sample-app.js
    ```

You should see output similar to the following:

```output
>>>> Successfully connected to YugabyteDB!
>>>> Successfully created table DemoAccount.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 10000
name = John, age = 28, country = Canada, balance = 9000
>>>> Transferred 800 between accounts.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 9200
name = John, age = 28, country = Canada, balance = 9800
```

You have successfully executed a basic Node.js application that works with YugabyteDB.

[Explore the application logic](/preview/tutorials/build-apps/nodejs/cloud-ysql-node/#explore-the-application-logic)

  {{% /tab %}}

  {{% tab header="C" lang="c" %}}

The [C application](https://github.com/yugabyte/yugabyte-simple-c-app) connects to a YugabyteDB cluster using the [libpq driver](/preview/reference/drivers/ysql-client-drivers/#libpq) and performs basic SQL operations. Use the application as a template to get started with YugabyteDB in C.

The application requires the following:

- 32-bit (x86) or 64-bit (x64) architecture machine. (Use [Rosetta](https://support.apple.com/en-us/HT211861) to build and run on Apple silicon.)
- gcc 4.1.2 or later, or clang 3.4 or later installed.
- OpenSSL 1.1.1 or later (used by libpq to establish secure SSL connections).
- [libpq](/preview/reference/drivers/ysql-client-drivers/#libpq). Homebrew users on macOS can install using `brew install libpq`. You can download the PostgreSQL binaries and source from [PostgreSQL Downloads](https://www.postgresql.org/download/).

To build and run the application, do the following:

1. Clone the sample application to your computer:

    ```sh
    git clone https://github.com/YugabyteDB-Samples/yugabyte-simple-c-app.git && cd yugabyte-simple-c-app
    ```

1. Provide connection parameters.

    (You can skip this step and use the defaults if your cluster is running locally and listening on 127.0.0.1:5433.)

    The application needs to establish a connection to the YugabyteDB cluster. To do this:

    - Open the `sample-app.c` file.

    - Set the following configuration-related macros:

        - **HOST** - the host name of your YugabyteDB cluster. For local clusters, use the default (127.0.0.1). For YugabyteDB Managed, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
        - **PORT** - the port number for the driver to use (the default YugabyteDB YSQL port is 5433).
        - **DB_NAME** - the name of the database you are connecting to (the default is `yugabyte`).
        - **USER** and **PASSWORD** - the username and password for the YugabyteDB database. For local clusters, use the defaults (`yugabyte`). For YugabyteDB Managed, use the credentials in the credentials file you downloaded.

    - YugabyteDB Managed [requires SSL connections](/preview/yugabyte-cloud/cloud-secure-clusters/cloud-authentication/#ssl-modes-in-ysql), so you need to set the following additional parameters:

        - **SSL_MODE** - the SSL mode to use; use `verify-full`.
        - **SSL_ROOT_CERT** - the full path to the YugabyteDB Managed [cluster CA certificate](/preview/yugabyte-cloud/cloud-secure-clusters/cloud-authentication/).

    - Save the file.

1. Build the application with gcc or clang.

    ```sh
    gcc sample-app.c -o sample-app -I<path-to-libpq>/libpq/include -L<path-to-libpq>/libpq/lib -lpq
    ```

1. Replace `<path-to-libpq>` with the path to the libpq installation; for example, `/usr/local/opt`.

1. Start the application.

    ```sh
    $ ./sample-app
    ```

You should see output similar to the following:

```output
>>>> Successfully connected to YugabyteDB!
>>>> Successfully created table DemoAccount.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 10000
name = John, age = 28, country = Canada, balance = 9000
>>>> Transferred 800 between accounts.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 9200
name = John, age = 28, country = Canada, balance = 9800
```

You have successfully executed a basic C application that works with YugabyteDB.

[Explore the application logic](/preview/tutorials/build-apps/c/cloud-ysql-c/#explore-the-application-logic)

  {{% /tab %}}

  {{% tab header="C++" lang="cpp" %}}

The [C++ application](https://github.com/yugabyte/yugabyte-simple-cpp-app) connects to a YugabyteDB cluster using the [libpqxx driver](/preview/reference/drivers/ysql-client-drivers/#libpqxx) and performs basic SQL operations. Use the application as a template to get started with YugabyteDB in C++.

The application requires the following:

- 32-bit (x86) or 64-bit (x64) architecture machine. (Use [Rosetta](https://support.apple.com/en-us/HT211861) to build and run on Apple silicon.)
- gcc 4.1.2 or later, or clang 3.4 or later installed.
- OpenSSL 1.1.1 or later (used by libpq and libpqxx to establish secure SSL connections).
- [libpq](/preview/reference/drivers/ysql-client-drivers/#libpq). Homebrew users on macOS can install using `brew install libpq`. You can download the PostgreSQL binaries and source from [PostgreSQL Downloads](https://www.postgresql.org/download/).
- [libpqxx](/preview/reference/drivers/ysql-client-drivers/#libpqxx). Homebrew users on macOS can install using `brew install libpqxx`. To build the driver yourself, refer to [Building libpqxx](https://github.com/jtv/libpqxx#building-libpqxx).

To build and run the application, do the following:

1. Clone the sample application to your computer:

    ```sh
    git clone https://github.com/YugabyteDB-Samples/yugabyte-simple-cpp-app.git && cd yugabyte-simple-cpp-app
    ```

1. Provide connection parameters.

    (You can skip this step and use the defaults if your cluster is running locally and listening on 127.0.0.1:5433.)

    The application needs to establish a connection to the YugabyteDB cluster. To do this:

    - Open the `sample-app.cpp` file.

    - Set the following configuration-related constants:

        - **HOST** - the host name of your YugabyteDB cluster. For local clusters, use the default (127.0.0.1). For YugabyteDB Managed, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
        - **PORT** - the port number for the driver to use (the default YugabyteDB YSQL port is 5433).
        - **DB_NAME** - the name of the database you are connecting to (the default is `yugabyte`).
        - **USER** and **PASSWORD** - the username and password for the YugabyteDB database. For local clusters, use the defaults (`yugabyte`). For YugabyteDB Managed, use the credentials in the credentials file you downloaded.

    - YugabyteDB Managed [requires SSL connections](/preview/yugabyte-cloud/cloud-secure-clusters/cloud-authentication/#ssl-modes-in-ysql), so you need to set the following additional parameters:

        - **SSL_MODE** - the SSL mode to use; use `verify-full`.
        - **SSL_ROOT_CERT** - the full path to the YugabyteDB Managed [cluster CA certificate](/preview/yugabyte-cloud/cloud-secure-clusters/cloud-authentication/).

    - Save the file.

1. Build the application with gcc or clang.

    ```sh
    g++ -std=c++17 sample-app.cpp -o sample-app -lpqxx -lpq \
    -I<path-to-libpq>/libpq/include -I<path-to-libpqxx>/libpqxx/include \
    -L<path-to-libpq>/libpq/lib -L<path-to-libpqxx>/libpqxx/lib
    ```

1. Replace `<path-to-libpq>` with the path to the libpq installation, and `<path-to-libpqxx>` with the path to the libpqxx installation; for example, `/usr/local/opt`.

1. Start the application.

    ```sh
    $ ./sample-app
    ```

You should see output similar to the following:

```output
>>>> Successfully connected to YugabyteDB!
>>>> Successfully created table DemoAccount.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 10000
name = John, age = 28, country = Canada, balance = 9000
>>>> Transferred 800 between accounts.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 9200
name = John, age = 28, country = Canada, balance = 9800
```

You have successfully executed a basic C++ application that works with YugabyteDB.

[Explore the application logic](/preview/tutorials/build-apps/cpp/cloud-ysql-cpp/#explore-the-application-logic)

  {{% /tab %}}

  {{% tab header="C#" lang="csharp" %}}

The [C# application](https://github.com/yugabyte/yugabyte-simple-csharp-app) connects to a YugabyteDB cluster using the [Npgsql driver](/preview/drivers-orms/csharp/) and performs basic SQL operations. Use the application as a template to get started with YugabyteDB in C#.

The application requires the following:

- [.NET 6.0 SDK](https://dotnet.microsoft.com/en-us/download) or later.

To build and run the application, do the following:

1. Clone the sample application to your computer:

    ```sh
    git clone https://github.com/YugabyteDB-Samples/yugabyte-simple-csharp-app.git && cd yugabyte-simple-csharp-app
    ```

    The `yugabyte-simple-csharp-app.csproj` file includes the following package reference to include the driver:

    ```cpp
    <PackageReference Include="npgsql" Version="6.0.3" />
    ```

1. Provide connection parameters.

    (You can skip this step and use the defaults if your cluster is running locally and listening on 127.0.0.1:5433.)

    The application needs to establish a connection to the YugabyteDB cluster. To do this:

    - Open the `sample-app.cs` file.

    - Set the following configuration-related parameters:

        - **urlBuilder.Host** - the host name of your YugabyteDB cluster. For local clusters, use the default (127.0.0.1). For YugabyteDB Managed, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
        - **urlBuilder.Port** - the port number for the driver to use (the default YugabyteDB YSQL port is 5433).
        - **urlBuilder.Database** - the name of the database you are connecting to (the default is `yugabyte`).
        - **urlBuilder.Username** and **urlBuilder.Password** - the username and password for the YugabyteDB database. For local clusters, use the defaults (`yugabyte`). For YugabyteDB Managed, use the credentials in the credentials file you downloaded.
    - YugabyteDB Managed [requires SSL connections](/preview/yugabyte-cloud/cloud-secure-clusters/cloud-authentication/#ssl-modes-in-ysql), so you need to set the following additional parameters:
        - **urlBuilder.SslMode** - the SSL mode to use; use `SslMode.VerifyFull`.
        - **urlBuilder.RootCertificate** - the full path to the YugabyteDB Managed [cluster CA certificate](/preview/yugabyte-cloud/cloud-secure-clusters/cloud-authentication/).

    - Save the file.

1. Build and run the application.

    ```sh
    dotnet run
    ```

You should see output similar to the following:

```output
>>>> Successfully connected to YugabyteDB!
>>>> Successfully created table DemoAccount.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 10000
name = John, age = 28, country = Canada, balance = 9000
>>>> Transferred 800 between accounts.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 9200
name = John, age = 28, country = Canada, balance = 9800
```

You have successfully executed a basic C# application that works with YugabyteDB.

[Explore the application logic](/preview/tutorials/build-apps/csharp/cloud-ysql-csharp/#explore-the-application-logic)

  {{% /tab %}}

  {{% tab header="Ruby" lang="ruby" %}}

The [Ruby application](https://github.com/yugabyte/yugabyte-simple-ruby-app) connects to a YugabyteDB cluster using the [Ruby Pg driver](/preview/reference/drivers/ysql-client-drivers/#pg) and performs basic SQL operations. Use the application as a template to get started with YugabyteDB in Ruby.

The application requires the following:

- Ruby 3.1 or later.
- OpenSSL 1.1.1 or later (used by libpq and pg to establish secure SSL connections).
- [libpq](/preview/reference/drivers/ysql-client-drivers/#libpq). Homebrew users on macOS can install using `brew install libpq`. You can download the PostgreSQL binaries and source from [PostgreSQL Downloads](https://www.postgresql.org/download/).
- [Ruby pg](/preview/reference/drivers/ysql-client-drivers/#pg). To install Ruby pg, run the following command:

    ```sh
    gem install pg -- --with-pg-include=<path-to-libpq>/libpq/include --with-pg-lib=<path-to-libpq>/libpq/lib
    ```

    Replace `<path-to-libpq>` with the path to the libpq installation; for example, `/usr/local/opt`.

To build and run the application, do the following:

1. Clone the sample application to your computer:

    ```sh
    git clone https://github.com/YugabyteDB-Samples/yugabyte-simple-ruby-app.git && cd yugabyte-simple-ruby-app
    ```

1. Provide connection parameters.

    (You can skip this step and use the defaults if your cluster is running locally and listening on 127.0.0.1:5433.)

    The application needs to establish a connection to the YugabyteDB cluster. To do this:

    - Open the `sample-app.rb` file.

    - Set the following configuration-related parameters:

        - **host** - the host name of your YugabyteDB cluster. For local clusters, use the default (127.0.0.1). For YugabyteDB Managed, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
        - **port** - the port number for the driver to use (the default YugabyteDB YSQL port is 5433).
        - **dbname** - the name of the database you are connecting to (the default is `yugabyte`).
        - **user** and **password** - the username and password for the YugabyteDB database. For local clusters, use the defaults (`yugabyte`). For YugabyteDB Managed, use the credentials in the credentials file you downloaded.

    - YugabyteDB Managed [requires SSL connections](/preview/yugabyte-cloud/cloud-secure-clusters/cloud-authentication/#ssl-modes-in-ysql), so you need to set the following additional parameters:

        - **sslmode** - the SSL mode to use; use `verify-full`.
        - **sslrootcert** - the full path to the YugabyteDB Managed [cluster CA certificate](/preview/yugabyte-cloud/cloud-secure-clusters/cloud-authentication/).

    - Save the file.

1. Make the application file executable.

    ```sh
    chmod +x sample-app.rb
    ```

1. Run the application.

    ```sh
    $ ./sample-app.rb
    ```

You should see output similar to the following:

```output
>>>> Successfully connected to YugabyteDB!
>>>> Successfully created table DemoAccount.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 10000
name = John, age = 28, country = Canada, balance = 9000
>>>> Transferred 800 between accounts.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 9200
name = John, age = 28, country = Canada, balance = 9800
```

You have successfully executed a basic Ruby application that works with YugabyteDB.

[Explore the application logic](/preview/tutorials/build-apps/ruby/cloud-ysql-ruby/#explore-the-application-logic)

  {{% /tab %}}

  {{% tab header="Rust" lang="rust" %}}

The [Rust application](https://github.com/yugabyte/yugabyte-simple-rust-app) connects to a YugabyteDB cluster using the [Rust-Postgres driver](/preview/reference/drivers/ysql-client-drivers/#rust-postgres) and performs basic SQL operations. Use the application as a template to get started with YugabyteDB in Rust.

The application requires the following:

- [Rust](https://www.rust-lang.org/tools/install) development environment. The sample application was created for Rust 1.58 but should work for earlier and later versions.

To build and run the application, do the following:

1. Clone the sample application to your computer:

    ```sh
    git clone https://github.com/YugabyteDB-Samples/yugabyte-simple-rust-app.git && cd yugabyte-simple-rust-app
    ```

1. Provide connection parameters.

    (You can skip this step and use the defaults if your cluster is running locally and listening on 127.0.0.1:5433.)

    The application needs to establish a connection to the YugabyteDB cluster. To do this:

    - Open the `sample-app.rs` file in the `src` directory.

    - Set the following configuration-related constants:

        - **HOST** - the host name of your YugabyteDB cluster. For local clusters, use the default (127.0.0.1). For YugabyteDB Managed, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
        - **PORT** - the port number for the driver to use (the default YugabyteDB YSQL port is 5433).
        - **DB_NAME** - the name of the database you are connecting to (the default is `yugabyte`).
        - **USER** and **PASSWORD** - the username and password for the YugabyteDB database. For local clusters, use the defaults (`yugabyte`). For YugabyteDB Managed, use the credentials in the credentials file you downloaded.

    - YugabyteDB Managed [requires SSL connections](/preview/yugabyte-cloud/cloud-secure-clusters/cloud-authentication/#ssl-modes-in-ysql), so you need to set the following additional parameters:

        - **SSL_MODE** - the SSL mode to use; use `SslMode::Require`.
        - **SSL_ROOT_CERT** - the full path to the YugabyteDB Managed [cluster CA certificate](/preview/yugabyte-cloud/cloud-secure-clusters/cloud-authentication/).

    - Save the file.

1. Build and run the application.

    ```sh
    $ cargo run
    ```

The driver is included in the dependencies list of the `Cargo.toml` file and installed automatically the first time you run the application.

You should see output similar to the following:

```output
>>>> Successfully connected to YugabyteDB!
>>>> Successfully created table DemoAccount.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 10000
name = John, age = 28, country = Canada, balance = 9000
>>>> Transferred 800 between accounts.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 9200
name = John, age = 28, country = Canada, balance = 9800
```

You have successfully executed a basic Rust application that works with YugabyteDB.

[Explore the application logic](/preview/tutorials/build-apps/rust/cloud-ysql-rust/#explore-the-application-logic)

  {{% /tab %}}

  {{% tab header="PHP" lang="php" %}}

The [PHP application](https://github.com/yugabyte/yugabyte-simple-php-app) connects to a YugabyteDB cluster using the [php-pgsql](/preview/reference/drivers/ysql-client-drivers/#php-pgsql) driver and performs basic SQL operations. Use the application as a template to get started with YugabyteDB in PHP.

The application requires the following:

- PHP runtime. The sample application was created using PHP 8.1 but should work with earlier and later versions. Homebrew users on macOS can install PHP using `brew install php`.
- [php-pgsql driver](/preview/reference/drivers/ysql-client-drivers/#php-pgsql).
  - On macOS, Homebrew automatically installs the driver with `brew install php`.
  - Ubuntu users can install the driver using the `sudo apt-get install php-pgsql` command.
  - CentOS users can install the driver using the `sudo yum install php-pgsql` command.

To build and run the application, do the following:

1. Clone the sample application to your computer:

    ```sh
    git clone https://github.com/YugabyteDB-Samples/yugabyte-simple-php-app.git && cd yugabyte-simple-php-app
    ```

1. Provide connection parameters.

    (You can skip this step and use the defaults if your cluster is running locally and listening on 127.0.0.1:5433.)

    The application needs to establish a connection to the YugabyteDB cluster. To do this:

    - Open the `sample-app.php` file.

    - Set the following configuration-related constants:

        - **HOST** - the host name of your YugabyteDB cluster. For local clusters, use the default (127.0.0.1). For YugabyteDB Managed, select your cluster on the **Clusters** page, and click **Settings**. The host is displayed under **Connection Parameters**.
        - **PORT** - the port number for the driver to use (the default YugabyteDB YSQL port is 5433).
        - **DB_NAME** - the name of the database to connect to (the default is `yugabyte`).
        - **USER** and **PASSWORD** - the username and password for the YugabyteDB database. For local clusters, use the defaults (`yugabyte`). For YugabyteDB Managed, use the credentials in the credentials file you downloaded.

    - YugabyteDB Managed [requires SSL connections](/preview/yugabyte-cloud/cloud-secure-clusters/cloud-authentication/#ssl-modes-in-ysql), so you need to set the following additional parameters:

        - **SSL_MODE** - the SSL mode to use; use `verify-full`.
        - **SSL_ROOT_CERT** - the full path to the YugabyteDB Managed [cluster CA certificate](/preview/yugabyte-cloud/cloud-secure-clusters/cloud-authentication/).

    - Save the file.

1. Run the application.

    ```sh
    $ php sample-app.php
    ```

You should see output similar to the following:

```output
>>>> Successfully connected to YugabyteDB!
>>>> Successfully created table DemoAccount.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 10000
name = John, age = 28, country = Canada, balance = 9000
>>>> Transferred 800 between accounts.
>>>> Selecting accounts:
name = Jessica, age = 28, country = USA, balance = 9200
name = John, age = 28, country = Canada, balance = 9800
```

You have successfully executed a basic PHP application that works with YugabyteDB.

[Explore the application logic](/preview/tutorials/build-apps/php/cloud-ysql-php/#explore-the-application-logic)

  {{% /tab %}}

{{< /tabpane >}}
