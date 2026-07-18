---
rfc: 0003
title: "RFC Process for DocumentDB"
status: Proposed
owner: "@normtown"
issue: "https://github.com/documentdb/documentdb/issues/372"
version-target: 1.0
implementations:
  - "https://github.com/documentdb/documentdb/pull/354"
---

# RFC-0003: Request for Comments (RFC) Process for DocumentDB

## Problem

DocumentDB lacks a formal process for architectural proposals. This absence creates friction for contributors, manual overhead for maintainers, and opacity for the community.

### Impact on Contributors

Contributors face four barriers:
1. No clear pathway to propose architectural changes
2. Uncertainty about when to open an issue, start a discussion, or submit a Pull Request (PR)
3. Risk of investing time in proposals misaligned with project direction
4. Unclear review and approval process

### Impact on Maintainers

Maintainers experience three challenges:
1. Manual overhead triaging and tracking proposals
2. No standardized format for architectural discussions
3. Inconsistent review standards across proposals

### Impact on the Community

The community lacks:
1. Transparency into architectural decisions
2. Centralized view of accepted designs and implementation status
3. Clear entry points for contributing to architectural work

### Current State

Architectural proposals currently flow through three channels:
- GitHub Issues (no standard format)
- Direct PRs (limited upfront validation)
- Private communications (no transparency)

These channels do not scale as the community grows.

### Success Metrics

This Request for Comments (RFC) process MUST achieve:
1. Clear status visibility (draft, proposed, accepted, implementing, complete)
2. Support for incremental implementation across multiple PRs

### Non-Goals

This RFC process explicitly does NOT:
- Replace bug reports or feature requests (use GitHub Issues)
- Add bureaucracy to code contributions
- Require RFCs for documentation updates or minor fixes
- Mandate implementation approaches

---

## Approach

### Solution Overview

Implement a GitHub-native RFC process using Discussions, Issues, PRs, Projects v2, and GitHub Actions. This provides low-friction, automated workflow for architectural proposals.

### Core Components

**1. RFC Template** (`/rfcs/0000-template.md`)
- Progressive structure: Problem → Approach → Detailed Design → Implementation Tracking
- YAML frontmatter for metadata
- Start lean, add detail iteratively

**2. GitHub Workflow**
- **Discussion:** Validate idea ("Is this worth pursuing?")
- **Issue:** Track visibility (created when RFC is deemed worthy)
- **PR:** Review formal RFC document
- **Implementation PRs:** Link via "Implements RFC-XXXX" syntax

**3. Automation** (GitHub Actions)
- Label PRs touching `/rfcs/` directory
- If no Issue exists, create one
- Add Issue to Projects board
- Link implementation PRs to parent RFC
- Post review guidelines

**4. Projects Board** ("RFC Pipeline")
- Columns: Draft → Under Review → Accepted → Implementing → Complete → Archived
- Fields: Owner, Discussion Link, Issue Link, Target Version

**5. Routing Guidance**
- README.md: Bug → Issue | Feature Request → Issue | Architecture/Design → RFC
- CONTRIBUTING.md: Detailed process documentation
- Clear set of instructions for when to open a feature request, a bug report, or to create an RFC

### Benefits

This approach delivers four advantages:
1. **Low barrier:** Discussions validate ideas before detailed RFC work
2. **Automation:** Labeling, tracking, and linking happen automatically
3. **Incremental implementation:** RFCs span multiple PRs with automatic tracking
4. **Transparency:** Public process visible on Projects board

### Design Rationale

**GitHub-Native:** Uses built-in features contributors already know. No external tools required.

**Progressive Complexity:** RFCs start with Problem statement only. Contributors add detail based on feedback. Ideas fail fast without heavy investment.

**Battle-Tested:** Based on Rust RFCs, React RFCs, and Kubernetes Enhancement Proposals (KEPs).

**Maintainable:** One part-time maintainer can operate the system.

### Tradeoffs

**Process Overhead**
- *Benefit:* Structured review ensures architectural consistency
- *Cost:* Additional steps compared to direct PRs
- *Mitigation:* Clear threshold guidance, lightweight process, progressive disclosure

**Platform Dependency**
- *Benefit:* Leverages existing platform
- *Cost:* Migration difficulty if moving from GitHub
- *Mitigation:* All content in portable Markdown/YAML

**Initial Manual Setup of Project Board**
- *Benefit:* Visual tracking with custom fields
- *Cost:* Projects v2 cannot be version-controlled
- *Mitigation:* Document setup steps, automate card management

### DocumentDB Integration

DocumentDB architecture spans:
- pg_documentdb_core (PostgreSQL extension for BSON)
- pg_documentdb (document operation APIs)
- pg_documentdb_gw (DocumentDB API gateway)
- pg_documentdb_gw_host (PostgreSQL extension to start gateway within PostgreSQL)

This RFC process enables:
- Cross-component change discussions
- MongoDB compatibility impact evaluation
- Breaking change migration planning
- C and Rust codebase coordination

---

## Detailed Design

### RFC Lifecycle

**State Machine:**
```
[Draft] → [Proposed] → [Accepted/Rejected] → [Implementing] → [Complete]
                                           ↘ [Archived]
```

**State Definitions:**

| State | Requirements | Entry Trigger |
|-------|-------------|---------------|
| Draft | Problem section MUST be complete | Contributor creates file |
| Proposed | Problem and Approach sections MUST be completed | PR opened touching `/rfcs/` |
| Accepted | Problem and Approach sections MUST be completed; Detailed Design section MAY BE REQUIRED per judgment of reviewers | RFC PR merged |
| Rejected | N/A | Maintainer closes PR |
| Implementing | Implementation PRs MUST be linked to RFC | Implementation PR references RFC |
| Complete | All implementation PRs are merged | Maintainer updates status |
| Archived | N/A | Superseded or abandoned |

**Automatic Transitions:**
- Draft → Proposed: PR opened
- Proposed → Accepted: PR merged
- Accepted → Implementing: Implementation PR contains "Implements RFC-XXXX"

**Manual Transitions:**
- Proposed → Rejected: Maintainer closes PR
- Implementing → Complete: Maintainer updates after all PRs merge
- Any state → Archived: Maintainer decision

### Numbering Scheme

**Format:** `NNNN-descriptive-name.md` (4-digit zero-padded). The file name SHOULD BE title of the RFC in kebab-case (lowercase characters separated by dashes (`-`)).

**Assignment:** Maintainers assign numbers during review. Contributors submit as `XXXX-feature-name.md`. Maintainer renames during merge to prevent collisions.

**Template:** File `0000-template.md` is never assigned to actual RFCs.

**Directory Structure:**
```
/rfcs/
  ├── 0000-template.md       # Template only
  ├── 0001-feature-one.md    # Assigned RFCs
  ├── 0002-feature-two.md
  └── 0003-rfc-process.md    # This RFC
```

### Metadata Schema

RFC documents MUST include YAML frontmatter with these fields:

```yaml
---
rfc: NNNN                    # 4-digit number (required)
title: "RFC Title"           # Quoted string (required)
status: Draft                # State name, one of Draft|Proposed|Accepted|Rejected|Implementing|Complete|Archived, case-sensitive (required)
owner: "@username"           # GitHub handle with @ (required)
issue: "URL"                 # GitHub Issue URL (required)
discussion: "URL"            # GitHub Discussions URL (optional)
version-target: X.Y          # Target release version (required)
implementations:             # Array of PR URLs (optional, populated during implementation)
  - "URL1"
  - "URL2"
---
```

Status values MUST exactly match state names. YAML MUST parse without errors.

### Configuration Requirements

**GitHub Actions:**

File `.github/workflows/rfc-automation.yml` MUST trigger on:
```yaml
on:
  pull_request:
    types: [opened, synchronize, reopened]
    paths: ['rfcs/**']
  pull_request_target:
    types: [closed]
    paths: ['rfcs/**']
```

File `.github/workflows/issue-automation.yml` MUST trigger on:
```yaml
on:
  issues:
    types: [opened]
```

**GitHub Projects:**
- Board Name: "RFC Pipeline"
- Columns: Draft, Under Review, Accepted, Implementing, Complete, Archived
- Required Fields: Champion (Person), Issue Link (URL), Target Version (Text)
- Optional Fields: Discussion Link (URL)

**GitHub Discussions:**

Categories MUST include:
- RFC Ideas (validation)
- RFC Questions (clarification)
- Implementation Help (coordination)
- Process Feedback (improvement)

### Testing Requirements

**Template Tests:**
1. Verify template renders in GitHub UI
2. Test copying and completing template
3. Validate YAML parsing

**Automation Tests:**
1. PR labeling on `/rfcs/` changes
2. Projects board card creation and movement
3. "Implements RFC-XXXX" pattern matching and linking
4. Issue template automation

**Integration Tests:**
1. Complete workflow: Discussion → Issue → PR → Merge → Implementation
2. Verify automation at each stage
3. Confirm no duplicate labels, comments, or board cards
4. Ensure non-RFC PRs remain unaffected

### Deployment Plan

**Steps:**
1. Create `/rfcs/` directory and template
2. Create issue templates
3. Update README.md and CONTRIBUTING.md
4. Set up Projects board
5. Deploy automation workflows
6. Test with pilot RFC
7. Announce to community

**Backwards Compatibility:**

This RFC introduces no breaking changes:
- Existing Issues and PRs continue unchanged
- Contributors MAY still submit direct PRs for appropriate changes
- All new functionality is additive

**Rollback:**

To rollback:
1. Disable GitHub Actions workflows
2. Remove RFC routing from README.md
3. Archive `/rfcs/` directory
4. Return to previous ad-hoc process

### Documentation Changes

**README.md Updates:**
- Add "Contributing to DocumentDB" section
- Include routing table
- Link to CONTRIBUTING.md

**CONTRIBUTING.md Updates:**
- Add "RFC Process" section covering:
  - RFC vs. Issue threshold
  - Discussion → Issue → PR → Implementation workflow
  - Template structure and progression
  - Review criteria
  - Implementation tracking
- Add "Bug Reports and Feature Requests" section:
  - Link to issue templates
  - Triage process
- Update "Making Changes" section:
  - Add RFC path for architectural changes

**No Changes:**
- docs/v1/ remains unchanged (product documentation separate from process)

**New Files:**
- `/rfcs/0000-template.md`: RFC template
- `/rfcs/0003-rfc-process.md`: This RFC

---

## Implementation Tracking

### User Stories

Implementation spans seven user stories:

1. **Story 1.1:** Foundation RFC Template - IMPLEMENTING
   - Created `/rfcs/0000-template.md`

2. **Story 1.2:** Issue Templates - PENDING
   - Create `.github/ISSUE_TEMPLATE/rfc.yml`

3. **Story 1.3:** Documentation Updates - PENDING
   - Update README.md with routing
   - Update CONTRIBUTING.md with process

4. **Story 1.4:** Projects Board - PENDING
   - Create "RFC Pipeline" board
   - Configure columns and fields
   - Set up Discussion categories

5. **Story 1.5:** RFC Automation - PENDING
   - Create `.github/workflows/rfc-automation.yml`
   - Implement labeling, Projects integration, PR linking

6. **Story 1.6:** Issue Automation - PENDING
   - Create `.github/workflows/issue-automation.yml`
   - Implement labeling and RFC routing

7. **Story 1.7:** Testing and Launch - PENDING
   - End-to-end testing
   - Documentation review
   - Community announcement

### Timeline

**2025-10-15:** RFC 0003 created. Story 1.1 completed.

**Next Actions:**
1. Complete Story 1.2 (Issue Templates)
2. Complete Story 1.3 (Documentation)
3. Set up Projects board (Story 1.4)
4. Implement automation (Stories 1.5, 1.6)
5. Execute end-to-end testing (Story 1.7)

### Open Issues

**Issue 1: Issue Creation Method**
- *Question:* Should RFC Issues be created automatically or manually?
- *Context:* The Issue is just for tracking in the Project board. Automating its creation on submission of Draft PR removes a step.
- *Status:* Will discuss with TSC and determine in Story 1.5.

**Issue 2: RFC Threshold Definition**
- *Question:* Clear examples of "RFC required" vs. "Issue sufficient"?
- *Proposed:*
  - RFC: Architecture changes, breaking changes, major features, cross-component work
  - Issue: Bug fixes, minor enhancements, documentation, single-component refactoring
- *Status:* Will refine based on first 3-5 RFC submissions

### Design Decisions

**Decision 1 [2025-10-15]: Meta-RFC Approach**
- *Rationale:* Validate template and workflow by using RFC process to describe itself
- *Benefit:* Tests usability, demonstrates progressive disclosure, provides reference
- *Tradeoff:* Circular definition, but valuable validation

**Decision 2 [2025-10-15]: Four Progressive Sections**
- *Rationale:* Support "fail fast" at multiple validation stages
- *Alternatives Considered:*
  - Single comprehensive template (rejected: too heavyweight)
  - Two sections only (rejected: insufficient granularity)
- *Result:* Problem → Approach → Detailed Design → Implementation enables iteration

**Decision 3 [2025-10-15]: GitHub-Native Implementation**
- *Rationale:* Minimize friction and infrastructure dependencies
- *Alternatives Considered:*
  - Dedicated RFC platform (rejected: adds complexity)
  - Wiki-based (rejected: lacks automation)
  - Email-based (rejected: no transparency)
- *Result:* GitHub affordances (Discussions, PRs, Projects, Actions) are sufficient
