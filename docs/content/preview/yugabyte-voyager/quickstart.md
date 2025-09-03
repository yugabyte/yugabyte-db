---
title: YugabyteDB Voyager Quick start
headerTitle: Quick start
linkTitle: Quick start
headcontent: Get started with YugabyteDB Voyager migration in minutes
description: Complete a simple database migration using YugabyteDB Voyager in the quickest possible way.
menu:
  preview_yugabyte-voyager:
    identifier: quickstart-voyager
    parent: yugabytedb-voyager
    weight: 100
type: docs
showRightNav: true
---

This quick start guide walks you through migrating a sample PostgreSQL database to YugabyteDB using YugabyteDB Voyager in the fastest way possible. You'll complete a full migration in under 15 minutes.

## Prerequisites

Before you start, ensure that you have the following:

- Java 17 installed
- 2+ CPU cores and 4GB+ RAM
- Network access to both source and target databases
- Sudo access on the machine where you'll run Voyager

## Install YugabyteDB Voyager

Installing YugabyteDB Voyager involves downloading and setting up the migration tool on your system:

{{< tabpane text=true >}}

{{% tab header="Ubuntu/Debian" lang="ubuntu" %}}

```bash
# Add YugabyteDB repository
wget -qO - https://downloads.yugabyte.com/repos/repo.gpg | sudo apt-key add -
echo "deb https://downloads.yugabyte.com/repos/apt/ubuntu focal main" | sudo tee /etc/apt/sources.list.d/yugabyte.list

# Install yb-voyager
sudo apt update
sudo apt install -y yb-voyager
```

{{% /tab %}}

{{% tab header="RHEL/CentOS" lang="rhel" %}}

```bash
# Add YugabyteDB repository
sudo yum install -y https://downloads.yugabyte.com/repos/repo.rpm

# Install yb-voyager
sudo yum install -y yb-voyager
```

{{% /tab %}}

{{% tab header="macOS" lang="macos" %}}

```bash
# Install using Homebrew
brew install yb-voyager
```

{{% /tab %}}

{{% tab header="Docker" lang="docker" %}}

```bash
# Pull and run yb-voyager in Docker
docker pull yugabytedb/yb-voyager
alias yb-voyager='docker run --rm -v $(pwd):/workspace -w /workspace yugabytedb/yb-voyager'
```

{{% /tab %}}

{{< /tabpane >}}

Verify the installation:

```bash
yb-voyager version
```

## Set up source database

For this quick start, you can use a sample PostgreSQL database. If you don't have PostgreSQL installed:

```bash
# Install PostgreSQL (Ubuntu/Debian)
sudo apt install -y postgresql postgresql-contrib

# Start PostgreSQL service
sudo systemctl start postgresql
sudo systemctl enable postgresql
```

### Create sample data

```bash
# Connect to PostgreSQL as postgres user
sudo -u postgres psql

# Create sample database and user
CREATE DATABASE sample_db;
CREATE USER ybvoyager PASSWORD 'password';
GRANT ALL PRIVILEGES ON DATABASE sample_db TO ybvoyager;

# Connect to sample database
\c sample_db

# Create sample tables with data
CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    name VARCHAR(100) NOT NULL,
    email VARCHAR(100) UNIQUE NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE orders (
    id SERIAL PRIMARY KEY,
    user_id INTEGER REFERENCES users(id),
    product_name VARCHAR(100) NOT NULL,
    amount DECIMAL(10,2) NOT NULL,
    order_date TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

-- Insert sample data
INSERT INTO users (name, email) VALUES
    ('John Doe', 'john@example.com'),
    ('Jane Smith', 'jane@example.com'),
    ('Bob Johnson', 'bob@example.com');

INSERT INTO orders (user_id, product_name, amount) VALUES
    (1, 'Laptop', 999.99),
    (1, 'Mouse', 29.99),
    (2, 'Keyboard', 79.99),
    (3, 'Monitor', 299.99);

-- Grant necessary permissions for migration
GRANT SELECT ON ALL TABLES IN SCHEMA public TO ybvoyager;
GRANT USAGE ON ALL SEQUENCES IN SCHEMA public TO ybvoyager;

\q
```

## Set up target YugabyteDB

### Option A: Local YugabyteDB (Recommended for quick start)

```bash
# Download and start YugabyteDB locally
wget https://downloads.yugabyte.com/releases/2.21.0.0/yugabyte-2.21.0.0-b1-linux-x86_64.tar.gz
tar -xzf yugabyte-2.21.0.0-b1-linux-x86_64.tar.gz
cd yugabyte-2.21.0.0/

# Start YugabyteDB cluster
./bin/yugabyted start
```

### Option B: YugabyteDB Aeon (Alternative)

1. Sign up at [YugabyteDB Aeon](https://cloud.yugabyte.com)
2. Create a free cluster
3. Note the connection details (host, port, username, password)

### Create target database and user

```bash
# Connect to YugabyteDB
./bin/ysqlsh -h 127.0.0.1 -p 5433 -U yugabyte

# Create target database
CREATE DATABASE target_db;

# Create migration user
CREATE USER ybvoyager SUPERUSER PASSWORD 'password';

\q
```

## Create migration configuration

Create a configuration file to simplify the migration process. You can use the provided example configuration file as a template:

{{< note title="Configuration file template" >}}
A complete configuration file template is available in the [YugabyteDB Voyager repository](https://github.com/yugabyte/yb-voyager/blob/{{< yb-voyager-release >}}/yb-voyager/config-templates/offline-migration.yaml). Copy and modify it for your specific migration needs.
{{< /note >}}

```bash
# Create migration directory
mkdir -p ~/voyager-quickstart
cd ~/voyager-quickstart

# Create configuration file
# You can also download the template from the Voyager repository and modify it:
# wget https://raw.githubusercontent.com/yugabyte/yb-voyager/{{< yb-voyager-release >}}/yb-voyager/config-templates/offline-migration.yaml -O migration-config.yaml

cat > migration-config.yaml << EOF
# Global settings
export-dir: /home/$(whoami)/voyager-quickstart/export-dir
log-level: info

# Source database (PostgreSQL)
source:
  db-type: postgresql
  db-host: localhost
  db-port: 5432
  db-name: sample_db
  db-schema: public
  db-user: ybvoyager
  db-password: 'password'

# Target database (YugabyteDB)
target:
  db-host: 127.0.0.1
  db-port: 5433
  db-name: target_db
  db-user: ybvoyager
  db-password: 'password'

# Export schema settings
export-schema:
  parallel-jobs: 2

# Export data settings
export-data:
  parallel-jobs: 2

# Import data settings
import-data:
  parallel-jobs: 4
EOF
```

## Run the migration

Execute the complete migration process as folows:

### 1. Export schema

```bash
yb-voyager export schema --config-file migration-config.yaml
```

### 2. Analyze schema (optional but recommended)

```bash
yb-voyager analyze-schema --config-file migration-config.yaml
```

### 3. Import schema

```bash
yb-voyager import schema --config-file migration-config.yaml
```

### 4. Export data

```bash
yb-voyager export data --config-file migration-config.yaml
```

### 5. Import data

```bash
yb-voyager import data --config-file migration-config.yaml
```

### 6. Finalize schema

```bash
yb-voyager finalize-schema-post-data-import --config-file migration-config.yaml
```

## Verify migration

Verify that your data has been successfully migrated:

```bash
# Connect to target YugabyteDB
./bin/ysqlsh -h 127.0.0.1 -p 5433 -U ybvoyager -d target_db

# Check tables
\dt

# Verify data
SELECT COUNT(*) FROM users;
SELECT COUNT(*) FROM orders;

# Check sample data
SELECT u.name, o.product_name, o.amount
FROM users u
JOIN orders o ON u.id = o.user_id;

\q
```

## Clean up (optional)

Clean up migration artifacts:

```bash
yb-voyager end migration --config-file migration-config.yaml \
  --backup-schema-files true \
  --backup-data-files true \
  --save-migration-reports true
```

## What's next?

- [Migration Assessment](../migrate/assess-migration/): Get detailed recommendations for production migrations
- [Live Migration](../migrate/live-migrate/): Migrate without downtime
- [Performance Tuning](../reference/performance/): Optimize migration speed
- [Bulk Data Loading](../migrate/bulk-data-load/): Import from CSV files

### Production considerations

- Security: Use SSL connections and strong passwords
- Performance: Tune parallel jobs based on your hardware
- Monitoring: Use the yugabyted UI to monitor migration progress
- Backup: Always backup your source database before migration

## Troubleshooting

### Common issues

**Connection refused**: Ensure that both source and target databases are running and accessible.

**Permission denied**: Verify that the migration user has the required permissions on both databases.

**Out of disk space**: Ensure that you have at least 1.5x the source database size available for the export directory.

**Java version issues**: Ensure that you're using Java 17 (not higher versions).

### Get help

- Check the [troubleshooting guide](../voyager-troubleshoot/)
- Review [known issues](../known-issues/)
- Join the [YugabyteDB Community Slack](https://yugabyte.com/slack)

## Migration checklist

Before running a production migration:

- [ ] Source database user has required permissions
- [ ] Target YugabyteDB cluster is properly sized
- [ ] Export directory has sufficient disk space
- [ ] Network connectivity between all components
- [ ] SSL certificates configured (if required)
- [ ] Backup of source database completed
- [ ] Migration tested in non-production environment
- [ ] Rollback plan prepared
