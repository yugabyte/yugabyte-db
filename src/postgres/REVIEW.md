# Review conventions for src/postgres

These apply to changes under `src/postgres/` in addition to the repo-root [REVIEW.md](../../REVIEW.md). Report each at **MEDIUM** when a PR adds or edits the construct. Do not flag pre-existing code the PR merely touches. Skip this file for backport PRs.

## YB scan layering

The YB scan layer's conventions are in [src/backend/access/yb_scan/README](src/backend/access/yb_scan/README) and the header comments it points to. They apply anywhere under `src/postgres/`, not only under `src/backend/access/`. Flag a PR that does any of the following:

- Goes against an `access/yb_*.h` header's comment. The comment may limit which files include the header, which code calls the functions it declares, or what belongs in it. Flag an include, call, or declaration that the comment does not allow. This includes a change to a header's comment that widens what it allows, such as adding a file to its includers, unless the PR says why. Cite the comment.
- Adds a new way for code above the access-method layer to read or lock relation rows without going through tableam, the index AM interface, or genam's systable functions, such as a direct pggate call or a new YB-only entry point. Ask why no AM interface, or a YB extension of one, fits. When the bypass is unavoidable, check that it is declared and documented as the README asks.
- Duplicates logic from a lower layer: planner code that reimplements what the executor or the AM decides at execution, or executor code that reimplements what the AM decides. Examples are which conditions bind to key columns and whether an index scan may recheck its conditions. Cite the duplicated code, and ask how the two can share it.
