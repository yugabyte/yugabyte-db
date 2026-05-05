# Docs Digest April 2026: Highlights of recent changes

## Explore, Develop, and YBA

- Restructured yb-master (and yb-tserver) reference page.
- Rocky Linux 9 support added for YBDB and YBA.
- Updated SSL modes behavior with YSQL Connection Manager.
- Updated follower reads documentation for YSQL.
- CDC: Fixed negative `ts_ms` value documentation; added limitation for unqualified tables in logical replication publications.
- pg_stat_monitor compatibility note added for YSQL major upgrade.
- macOS Quick Start improvements: port conflict and quarantine troubleshooting.
- Clarified "Table bad state" error handling in xCluster and DR setup.
- Upgrade limitations clarified for DB and YBA upgrades.
- S3 proxy support documented for YBA backup storage configuration.
- K8s operator helm upgrade command updated for existing universes.
- On-prem legacy manual provisioning guidance updated.
- K8s cert-manager: cert rotation not supported — limitation documented.
- Restored `--disable_version_check` flag documentation for the `restoreBackup` command.
- C# smart driver version updated to 9.0.2.2.
- YBC version updated in deploy and quick-start docs for v2025.2.
- macOS YugabyteDB Client downloads added to releases page.

## Releases

- v2025.2.2.2 (Stable)
- v2025.1.4.0
- v2024.2.9.0
- v2024.1 and v2.25 removed (EOL).

## Technical Advisories

- TA-30653 — Index inconsistency with in-place update on INCLUDE column. Affects v2024.2.0–v2024.2.8, v2025.1.0–v2025.1.3, v2025.2.0–v2025.2.1. Fixed in v2025.2.2.0; upcoming fix in v2024.2.9.0 and v2025.1.4.0.

## YugabyteDB Voyager

- Releases: v2026.4.1, v2026.4.2.
- v2026.4.1: live migration updates, CLI reference additions.

## YugabyteDB Aeon

- April 2026 release notes:
  - Disaster Recovery — GA. Available for clusters running v2025.2.2.1 or later.
  - Multi-account support — Early Access.
  - Early Access track updated to v2025.2.2.2.
- Disaster Recovery docs updated for GA.
- Multiple accounts feature docs added.

## Site-wide changes

- YCQL and YSQL statements pages reformatted for readability.
- Netlify builds configured to trigger only on `/docs` folder changes.
