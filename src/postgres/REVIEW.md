# Review conventions for src/postgres

These apply to changes under `src/postgres/` in addition to the repo-root [REVIEW.md](../../REVIEW.md). Report each at **MEDIUM** when a PR adds or edits the construct. Do not flag pre-existing code the PR merely touches. Skip this file for backport PRs.

## YB scan layering

The YB scan layer's conventions are in [src/backend/access/yb_scan/README](src/backend/access/yb_scan/README) and the header comments it points to. They apply anywhere under `src/postgres/`, not only under `src/backend/access/`. Flag a PR that does any of the following:

- Includes an `access/yb_*.h` header from a file that the header's comment does not allow, or adds a declaration that the comment says belongs elsewhere. Cite the comment.
- Goes around tableam, the index AM interface, or genam's systable functions from above the access-method layer, in a way that neither the README nor the header in use already accounts for. Ask why no AM interface fits. When the bypass is unavoidable, its header should say why or cite the issue tracking its removal.
- Duplicates logic from a lower layer: planner code that reimplements what the executor or the AM decides at execution, or executor code that reimplements what the AM decides. Examples are which conditions bind to key columns and whether an index scan may recheck its conditions. Cite the duplicated code, and ask how the two can share it.
