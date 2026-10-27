---
title: Review guide for the v2025.1 docs tree (not a docs page)
private: true
_build:
  render: false
  list: false
---

# Review guide: `docs/content/v2025.1/`

Every page in this tree describes the **v2025.1** series only. The code for
that series lives on the `2025.1` release branch and its `2025.1.x.y-bN`
build tags — none of which are in this checkout. The code in this checkout
is the base branch (`master`), a later version.

Consequences for review:

- Do not flag a page in this tree because code in the checkout differs from it:
  flag/GUC names and defaults, dependency names and versions, file paths,
  and behaviour may all have changed on the base branch after v2025.1
  branched. Such claims are unverifiable in this review; per
  `docs/REVIEW.md`, raise no finding and cite no checkout file as disproof.
- Do not suggest bringing a page in line with `master`, with
  `docs/content/stable/`, or with any other version tree. Differences
  between version trees are expected; each tree is correct or wrong only
  against its own series.
- The in-checkout counterparts for cross-checks are this tree itself and
  the v2025.1 sections of `docs/content/stable/releases/` — other v2025.1
  documents, not the shipped code.
