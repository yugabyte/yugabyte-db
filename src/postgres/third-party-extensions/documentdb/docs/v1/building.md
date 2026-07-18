# Building and Running DocumentDB from Source

If you want to build and run DocumentDB from source (instead of using Docker), follow these steps. This guide is designed for beginners and works best on Ubuntu/Debian. For other operating systems, package names may differ.

### Prerequisites

*Recommended* use the provided devcontainer for VSCode which contains all the dependencies pre-installed.

Or install the required dependencies:

```bash
sudo apt update
sudo apt install build-essential libbson-dev postgresql-server-dev-all pkg-config rustc cargo
```

### Step 1: Build PostgreSQL Extensions

```bash
sudo make install
```

### Step 2: Build the Gateway

```bash
scripts/build_and_install_with_pgrx.sh -i -d pg_documentdb_gw_host/
```

### Step 3: Start PostgreSQL and the Gateway

```bash
scripts/start_oss_server.sh -c -g
```

### Step 4: Connect and Test

#### Using a MongoDB Client

```bash
mongosh --host localhost --port 10260 --tls --tlsAllowInvalidCertificates -u docdb_user -p Admin100
```

Try basic MongoDB commands to verify everything works.

#### Using PostgreSQL shell

```bash
psql -p 9712 -d postgres
```

### Need Help?

- Join our [Discord](https://discord.gg/vH7bYu524D)
- See [docs](https://documentdb.io/docs) for more details