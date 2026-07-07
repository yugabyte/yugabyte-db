# Case analysis — Zendesk #18850

**Customer:** Shopify  
**Subject:** Intermittent client query timeouts  
**Status:** New | **Priority:** Urgent  
**Environment:** GCP production (`globaldb`)  
**Requester:** Brandon Mcnama (brandon.mcnama@shopify.com)  
**Ticket:** https://yugabyte.zendesk.com/agent/tickets/18850

---

### Summary of issue, including timeline if available

Shopify reports **intermittent query timeouts** in production starting around **16:30 UTC on 2026-06-19**. Application clients disconnect after waiting **7 seconds** for a query response. Affected workloads include **ShopServer** and background jobs such as **`Reviews::CollectionRequestNotificationActiveJob`** (Sidekiq worker `sidekiq-worker-in-app-notifications-10`).

The customer provided a representative failing query against **`shop.orders`**: a primary-key lookup on `(user_uuid, uuid)` with `LIMIT 1`. When run manually with `EXPLAIN (ANALYZE, DIST, ...)`, the query completes in **~2.7 ms** via an **Index Scan on `orders_pkey`** (1 storage read, 1 row scanned). This suggests the timeout is **not caused by a consistently bad plan** for this statement, but rather by **intermittent latency** — e.g. cluster-wide resource pressure, lock/contention, network or RPC queuing, or other concurrent workload at the time of failure.

A log line from **2026-06-19 16:36:01 UTC** shows:

`ERROR: canceling statement due to statement timeout` (SQLSTATE **57014**)

for a `SELECT` issued by role `svc_shop_app_2` / application `puma` against `globaldb`.

**Business impact (per ticket):** Intermittent timeouts are causing client disconnects and failed background jobs, affecting application performance and data-processing availability since the incident window began.

**Prior Shopify context:** Ticket [#16003](https://yugabyte.zendesk.com/agent/tickets/16003) (closed) involved `statement_timeout` not being respected during **long sequential scans** on `shop.users` (YSQL interrupt-check gap; fix tracked in GitHub #28863, targeted for 2025.1.3). The current case differs: the provided example uses an efficient PK index scan and fails only **intermittently**, so prior RCA may not apply directly — but timeout behavior under load remains worth validating.

**Investigation gaps:** No support bundle is attached; cluster topology, YugabyteDB version, and server-side metrics for the 16:30 UTC window are unavailable. Hagen briefing confidence is **partial** (`topology`, `versions`, `incident_window`, `top_errors` all missing).

**Timeline (from available information):**

| Time (UTC) | Event |
|---|---|
| 2026-06-19 ~16:30 | Customer first observes intermittent query timeouts |
| 2026-06-19 16:36:01 | Example log: `canceling statement due to statement timeout` on `shop.orders` SELECT |
| 2026-06-19 19:43:25 | Ticket opened (urgent) |

---

### Clarifying questions

1. **Is the issue still occurring?** If yes, approximate **error rate** (timeouts per minute/hour) and whether impact is cluster-wide or isolated to specific nodes/regions/tablespaces (e.g. `us_east1_regional_rf3_1`).
2. **YugabyteDB version and build** for the affected universe(s), and whether any **upgrade, restart, gflag change, or deployment** occurred near 16:30 UTC on 2026-06-19.
3. **`statement_timeout` setting** for role `svc_shop_app_2` (and whether it differs from the 7s client-side disconnect threshold).
4. **Additional examples of queries that timed out** — especially any that differ from the PK lookup (e.g. broader scans, joins, or writes). The provided example runs in ~2.7 ms in isolation; we need statements that actually exceeded the timeout.
5. **Fresh support bundle** covering **16:00–17:00 UTC on 2026-06-19** (or ongoing if still active), so we can review tserver/master metrics, slow queries, RPC latency, and lock wait patterns.
6. **Scope of impact:** Which applications/services besides ShopServer and the cited Sidekiq job are affected? Any correlation with traffic spikes, batch jobs, or maintenance windows?
7. **Cluster health at incident time:** Any concurrent alerts (CPU saturation, disk latency, under-replicated tablets, leader elections, or `Stopping writes because bytes to flush` style backpressure)?

---

### Suggested internal next steps

1. Request bundle and version details (blocker for topology/gflag analysis).
2. Pull DB metrics for **16:30 UTC window**: CPU, IOPS, disk latency, YSQL ops latency, RPC queue times, and lock wait stats.
3. Review slow query / `pg_stat_statements` and postgres logs for competing heavy queries around timeout events.
4. Compare against Shopify #16003 only if timeouts involve long scans or cancellation lag; otherwise focus on **transient resource contention** hypotheses.
5. If bundle is provided, run WTL intake and incident-window detection for automated error fingerprinting.
