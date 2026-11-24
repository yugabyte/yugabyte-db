# Filing core DB issues

These rules apply to every issue about the core database (`src/`, `java/`,
and the build and tooling around them). What goes in the issue body depends
on the component; [ysql.md](ysql.md) covers YSQL.

Agents: confirm with the user before filing an issue or posting a comment.

## Where to file

GitHub issues are public and permanent. An issue is **sensitive** if it can't
be described without customer data or schema, personal data, credentials or
internal-only information. If that part can be replaced with a synthetic
repro or a placeholder, it isn't sensitive. An unfixed security
vulnerability is always sensitive, however it is described.

- **Not sensitive:** a GitHub issue, using the component's issue template. It
  syncs automatically to a ticket in the JIRA `DB` (core database) project.
- **Sensitive, Yugabyte employee, security vulnerability:** a JIRA ticket of
  type `Vulnerability`, never a GitHub issue. See
  [`src/AGENTS.md`](../../src/AGENTS.md) § Security and customer-sensitive
  changes for what it holds.
- **Sensitive, Yugabyte employee, anything else:** a ticket in the JIRA `DB`
  project, marked as containing sensitive information. That keeps it off
  GitHub entirely.
- **Sensitive, external, security vulnerability:** a report to
  `security@yugabyte.com`, per the
  [vulnerability disclosure policy](https://docs.yugabyte.com/stable/secure/vulnerability-disclosure-policy/).
- **Sensitive, external, anything else:** that address doesn't take bug
  reports. Scrub it into a synthetic repro and file a GitHub issue; if it
  can't be scrubbed, open a support ticket.

## Check for duplicates

Before anything else, search existing issues (open and closed) for the same
symptom.

- **Same problem, same repro:** don't file; use the existing issue.
- **Same problem, materially different repro** (a different trigger,
  setting or environment): don't file a new issue. Instead, add a comment to
  the existing one with the same information a new issue would carry.
- **Not a duplicate:** file a new issue. Link any related issues the search
  turned up.

For a sensitive issue filed in JIRA, don't comment on the existing issue: file
the JIRA ticket anyway and mention the existing issue's number in it.

## Links

Internal URLs and hostnames never go in a GitHub issue; see the
confidentiality rules in [`src/AGENTS.md`](../../src/AGENTS.md). Internal
ticket keys such as `DB-1234` are fine.

Even then, write the issue so an external reader can follow it without
internal references: describe what a ticket or log showed instead of only
pointing to it.

## Title

Keep the title succinct: the component prefix the template gives (for
example `[YSQL]`), then the symptom or what is asked for, in a few words.
Leave the mechanism, versions and repro details for the body.
