---
title: Iterative cutover
linkTitle: Iterative cutover
description: Perform iterative cutovers with live migration with fall-back.
headContent: With live migration with fall-back
menu:
  stable_yugabyte-voyager:
    identifier: itertive-cutover
    parent: reference-voyager
    weight: 105
type: docs
---

Iterative cutover extends [live migration with fall-back](../../migrate/live-fall-back/) by allowing multiple rounds of cutover between a PostgreSQL source and YugabyteDB target. Instead of treating a fall-back (cutover to source) as a terminal event, you can restart source-to-target data migration, creating a new CDC-only streaming iteration without re-importing the initial snapshot.

This is useful in the following cases:

- Validate your application on YugabyteDB with production traffic. If you find issues after cutover, fall back to the source, address the problems, and cut over to the target again without restarting the migration from scratch.
- Reduce migration risk by retaining the ability to retry the cutover multiple times. You can keep iterating until the cutover succeeds.
- Avoid repeating expensive initial snapshot export and import. Each new iteration only streams CDC changes from the point the previous iteration left off.

Iterative cutover only works with live migration with fall-back (that is, cutover to target was run with `--prepare-for-fall-back true`) running on a PostgreSQL source.

Iterative cutover is not supported for fall forward or Oracle/MySQL sources. 

### Perform iterative cutover

To perform iterative cutovers, do the following:

1. Start live migration with fall-back, and follow the standard live migration with fall-back steps.

1. [Cut over to target](../../migrate/live-fall-back/#cutover-to-the-target) using `prepare-for-fall-back` as usual.

1. When performing a [cutover to source](../../migrate/live-fall-back/#cutover-to-the-source-optional), add the restart flag.

    <br/>{{< tabpane text=true >}}

{{% tab header="Config file" lang="config" %}}

Add the following to your configuration file:

```yaml
...
initiate-cutover-to-source:
  restart-data-migration-source-target: true
...
```

Then run the following command:

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager initiate cutover to source --config-file <path-to-config-file>
```

{{% /tab %}}

{{% tab header="CLI" lang="cli" %}}

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager initiate cutover to source --export-dir <EXPORT_DIR> --restart-data-migration-source-target true
```

{{% /tab %}}

    {{< /tabpane >}}

    This tells Voyager to start a new forward iteration after the fall-back completes. If you omit this flag or set it to false, the fall-back is terminal and no new iteration is created.

1. [Monitor the new iteration](../../migrate/live-fall-back/#get-data-migration-report). After fall-back completes, Voyager automatically starts the next iteration.

1. Cut over to target again. When the new iteration has caught up, initiate cutover to target with `--prepare-for-fall-back` true.

1. Repeat steps 2-5 as many times as needed.

1. Final cutover. When satisfied, cut over to target without `--prepare-for-fall-back`, or cut over to target with `--prepare-for-fall-back` and then run `end migration`.

### Monitor iteration

When iterations exist, the [cutover status](../cutover-archive/cutover/#cutover-status) includes rows for each iteration.

By default, the [data migration report](../../migrate/live-fall-back/#get-data-migration-report) aggregates event counts (inserts, updates, deletes) across all iterations into a single view per table.

For detailed per-iteration data migration reporting, add the `--include-detailed-iterations-stats` flag. This adds an ITERATION NUMBER column showing per-iteration statistics and a CUMULATIVE FINAL_ROW_COUNT (row count up to that iteration). Iteration 0 includes snapshot rows; subsequent iterations show only change events.

### Archive changes with iterations

The [archive changes](../../migrate/live-fall-back/#archive-changes-optional) command supports iterative migrations. When iterations are present, the command processes the parent export directory and, at the end, provides the directory location for archiving the next iteration. Use the `--archive-dir` flag to specify the directory location.

### End migration with iterations

The [end migration](../../migrate/live-fall-back/#end-migration) command handles iterative migrations by ending each iteration in sequence (1 through N), then ending the parent migration (iteration 0). Specifically, the command does the following:

- Iterates through each iteration (1 through N), ending each one and cleaning up its migration state.

- Ends the parent migration (iteration 0), cleaning up source and target database state.

- If `--save-migration-reports` is enabled, saves all reports. This includes a consolidated data migration report with detailed iteration statistics.

    The consolidated report is saved to `{BACKUP-DIR}/reports/data-migration-report.json`. Backup data for each iteration is saved in a subdirectory of the backup directory (`{BACKUP-DIR}/live-data-migration-iterations/live-data-migration-iteration-N/`), with data, schema, and logs subfolders. 

