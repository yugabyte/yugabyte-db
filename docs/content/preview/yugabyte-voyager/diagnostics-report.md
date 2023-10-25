---
title: YB Voyager Diagnostics reporting
linkTitle: Diagnostics reporting
description: Sending diagnostics information to Yugabyte.
menu:
  preview_yugabyte-voyager:
    identifier: diagnostics-report
    parent: yugabytedb-voyager
    weight: 104
type: docs
---

By default, YugabyteDB Voyager reports migration diagnostics to the Yugabyte diagnostics service when yb-voyager commands are executed. The diagnostics information is transmitted securely to and stored by Yugabyte.

None of your data or personally identifiable information is collected.

## Data collected

When enabled, the following data is collected while running yb-voyager:

- Migration unique ID
- Start Time
- YB Voyager Version (yb-voyager version used for the migration)
- SourceDB Type (source type of migration)
- SourceDB Version (source db version used in migration)
- Issues  (issues reported by analyze-schema without the SQL statements)
- Database Objects  (Objects migrated in the migration)
- TargetDB Version (Target YugabyteDB version)
- Total Rows (total number of rows of all tables migrated)
- Total Size (total number of size of all tables migrated)
- Largest Table Rows (maximum number of rows migrated for a table)
- Largest Table Size (maximum size of data migrated for a table)

The following is an example of a payload that is collected:

```output.json
{
    "UUID": "ba4786e8-5923-11ee-8621-06e7faf40beb",
    "start_time": "2023-09-26 11:21:53",
    "yb_voyager_version": "1.5.1",
    "last_updated_time": "2023-09-26 11:29:49",
    "source_db_type": "oracle",
    "source_db_version": "Oracle Database 19c Enterprise Edition Release 19.0.0.0.0 - Production",
    "issues": "[
        {
            "objectType": "TABLE",
            "objectName": "sales_data",
            "reason": "insufficient columns in the PRIMARY KEY constraint definition in CREATE TABLE",
            "sqlStatement": "",
            "filePath": "/home/centos/export-dir/schema/tables/table.sql",
            "suggestion": "Add all Partition columns to Primary Key",
            "GH": "<https://github.com/yugabyte/yb-voyager/issues/578>"
        }
    ]",
    "database_objects": "[
        {
            "objectType": "TABLE",
            "totalCount": 40,
            "invalidCount": 0,
            "objectNames": "order_items, foo, abcdef, char_types, interval_types, \"limit\", trade_symbol_price_historic, test, accounts, customers, date_time_types, large_table, log_mining_flush, orders, reserved_column, t1, c, \"group\", pqr, testcase, testt, \"check\", long_type, raw_type, tt, varray_table, case_sensitive, case_sensitive_column, chkk, identity_demo, lob_types, sales_data, some_meta_table, trade_symbol_price_historic1, abc, number_ps, numeric_types, sample2, t, inventories",
            "details": ""
        },
        {
            "objectType": "INDEX",
            "totalCount": 4,
            "invalidCount": 0,
            "objectNames": "inventories_warehouse_id_product_id, order_items_order_id_product_id, pqr_abc_id, pqr_c2_c3",
            "details": ""
        },
        {
            "objectType": "MVIEW",
            "totalCount": 2,
            "invalidCount": 0,
            "objectNames": "warranty_orders, my_warranty_orders",
            "details": ""
        }
    ]",
    "target_db_version": "11.2-YB-2.18.2.1-b0",
    "total_rows": 11927,
    "total_size": 225422,
    "largest_table_rows": 11907,
    "largest_table_size": 224568,
}
```

## Configure diagnostics collection

To control whether to send diagnostics to Yugabyte, you can use the `--send-diagnostics` flag or export an environment variable.

Set the flag as follows:

```sh
yb-voyager ... --send-diagnostics=[true|false]
```

The default is true.

Alternatively, you can use the following environment variable to set a value for this flag globally on the client machine where yb-voyager is running.

```sh
export YB_VOYAGER_SEND_DIAGNOSTICS=[true|false]
```
