---
title: Review guide for the stable docs tree (not a docs page)
private: true
_build:
  render: false
  list: false
---

# Review guide: `docs/content/stable/`

This folder is not a version. It is the alias target for whichever series has
`"alias": "stable"` in `docs/data/currentVersions.json` — read that file
before reviewing anything in this tree; as of this writing the alias points at
v2026.1. When the alias moves to a new series, update this file in the same
PR.

Consequences for review:

- Pages in this tree describe the `stable` series, but the code in this checkout is
  the base branch, which runs ahead of the `stable` series' release branch.
  Per `docs/REVIEW.md`, code in the checkout is not evidence about this
  series: do not flag a page in this tree because `master` code, a flag default, or
  a dependency pin in the checkout differs from what the page says.
- The in-checkout counterpart for cross-checks is this tree itself, plus
  `docs/data/currentVersions.json`. Contradiction with the code in this
  checkout, by contrast, is unverifiable in this review.
- `releases/` under this folder is the exception to "this tree is one
  series": release files there cover every series, and
  `docs/content/stable/releases/REVIEW.md` governs them.
