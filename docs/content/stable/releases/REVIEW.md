---
title: Review guide for release notes (not a docs page)
private: true
_build:
  render: false
  list: false
---

# Review guide: release notes and technical advisories

This guide covers `ybdb-releases/`, `yba-releases/`, and `techadvisories/`
under `docs/content/stable/releases/`. It only clarifies which product
version each file describes; what to flag is governed by the
repository-root `REVIEW.md` and `docs/REVIEW.md`, as before.

These files live under `stable/` for every series — the folder says nothing
about the version a file describes. The filename does: `vX.Y.md` is the
release-notes file for series X.Y, and each `## vX.Y.Z.W` section in it is
one patch release of that series.

## What you cannot verify in this review

The code for series X.Y lives on the `X.Y` release branch, at the build tag
named in the section (for example `2025.2.6.0-b111`). Neither is in this
checkout, and the code that is — the base branch — is a later, different
version. Therefore, for any series other than the one under development on
the base branch:

- Do not check release-note bullets against code in the checkout: flag or
  GUC names and defaults, dependency names and pinned versions, file paths,
  and described behaviour. A bullet that disagrees with the checkout may be
  correct for its series; the feature may have been renamed, reverted, or
  changed again after the series branched. Treat such claims as
  unverifiable in this review and raise no finding.
- Do not cite a checkout file and line (a requirements file, a flag
  definition) as proof that a bullet names the wrong thing. That citation
  describes the wrong version and will not reproduce for the author.
- The same-series material that *is* in this checkout, and can be used the
  way the root guide uses surrounding code, is the `docs/content/vX.Y/`
  tree (or `docs/content/stable/` for the `stable` series), other sections
  of the same file, and `docs/data/currentVersions.json`.

## Technical advisories

Advisory files state the affected versions explicitly. The checkout's code
is not evidence for any of those versions, for the same reason as above.
