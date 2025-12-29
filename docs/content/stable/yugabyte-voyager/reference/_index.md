---
title: Reference for YugabyteDB Voyager
headerTitle: Reference
linkTitle: Reference
headcontent: Command line interfaces (CLIs), data modeling strategies, and data type mapping reference.
description: Learn about the CLI Reference, data modeling strategies, and data type mapping reference using YugabyteDB Voyager.
type: indexpage
showRightNav: true
menu:
  stable_yugabyte-voyager:
    identifier: reference-voyager
    parent: yugabytedb-voyager
    weight: 105
---

{{<index/block>}}

  {{<index/item
    title="yb-voyager CLI"
    body="yb-voyager CLI commands, arguments, and SSL connectivity."
    href="yb-voyager-cli//"
    icon="/images/section_icons/architecture/concepts.png">}}

  {{<index/item
    title="Datatype mapping"
    body="Data type mapping from different source databases to YugabyteDB."
    href="datatype-mapping-mysql/"
    icon="/images/section_icons/reference/connectors/ecosystem-integrations.png">}}

  {{<index/item
    title="Tune performance"
    body="Optimize migration job performance using tuneable parameters with yb-voyager."
    href="performance/"
    icon="fa-thin fa-chart-line-up">}}

   {{<index/item
    title="Diagnostics reporting"
    body="Monitor migration diagnostics securely with yb-voyager commands."
    href="diagnostics-report/"
    icon="fa-sharp fa-thin fa-stethoscope">}}

  {{<index/item
    title="Configuration file"
    body="Use a YAML-based configuration file to simplify and standardize migrations."
    href="configuration-file/"
    icon="/images/section_icons/manage/export_import.png">}}

{{</index/block>}}
