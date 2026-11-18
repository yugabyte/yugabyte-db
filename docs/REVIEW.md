# Documentation review guide

This guide applies to every file under `docs/`. It is instructions for the
reviewer (human or AI), not a published docs page.

It changes only one thing: which version of the product a docs file
describes, and therefore what evidence can settle a question about it. It
does not change what the reviewer should flag or not flag — severity, style
and lint ownership, re-flagging, and every other review rule in the
repository-root `REVIEW.md` apply to docs exactly as before.

## The one fact the path does not tell you

Docs for several product versions live side by side in this one checkout, but
the code in this checkout belongs to a single version: the base branch (usually
`master`) merged with the PR. File location therefore does **not** say which
code version a page describes:

| Path | Describes | Code that would settle it |
|---|---|---|
| `docs/content/stable/**` | The series whose `alias` is `stable` in `docs/data/currentVersions.json` | That series' release branch |
| `docs/content/v2025.2/**`, `v2025.1/**`, `v2024.2/**` | That named series | The same-named release branch |
| `docs/content/stable/releases/{ybdb,yba}-releases/vX.Y.md` | Release series X.Y, regardless of the `stable/` prefix | Branch `X.Y`, and the build tag named in each release section |

Technical advisories under `docs/content/stable/releases/techadvisories/`
state the affected versions explicitly; the same rule applies to them.

## Evidence rules

- Code in this checkout (`src/`, `managed/`, `python/`, requirements and
  pin files, flag/GUC definitions, and so on) is evidence about the base
  branch only. It is **not** evidence about any released series, including
  the current `stable` series, whose branch may lag `master`.
- A claim whose truth depends on a release branch or build tag that is not in
  the checkout is **unverifiable in this review**: do not flag it, and do not
  cite a checkout file and line as if it disproved it. This includes feature
  and flag names, flag/GUC defaults, dependency names and pinned versions,
  and behaviour that may have been renamed, reverted, or further changed on
  the base branch after the series branched.
- Code **changed by this PR** is evidence: if a PR changes code and its docs
  together, the two disagreeing is a finding.
- In-checkout evidence you may use: the same series' own docs tree (for
  `stable`, the tree named by the `stable` alias in
  `docs/data/currentVersions.json`), other sections of the same file,
  `docs/data/currentVersions.json`, and the shortcode/layout definitions
  under `docs/layouts/`.
