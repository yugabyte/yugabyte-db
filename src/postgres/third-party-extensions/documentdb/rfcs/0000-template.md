---
rfc: 0000
title: "RFC Title"
status: Draft
owner: "@github-username"
issue: "https://github.com/documentdb/documentdb/issues/XXX"
discussion: "https://github.com/documentdb/documentdb/discussions/XXX"
version-target: 1.0
implementations:
  - "https://github.com/documentdb/documentdb/pull/XXX"
---

# RFC-XXXX: [Title]

*Delete these italicized instructions before submitting your RFC. For details of the RFC process, see https://github.com/documentdb/documentdb/blob/main/rfcs/0003-rfc-process.md*

*RFC statuses follow this flow:*

```
[Draft] → [Proposed] → [Accepted/Rejected] → [Implementing] → [Complete]
                                           ↘ [Archived]
```

## Problem

*This section is REQUIRED before moving from Draft to Proposed status.*

**Section purpose:** Clearly articulate the problem or opportunity this RFC addresses. 

**Complete this section when:** You're ready to start a discussion and get initial feedback on whether this problem is worth solving.

**Guidance:**
- What problem are you trying to solve?
- Who is impacted by this problem?
- What are the consequences of not solving it?
- What are the current workarounds, if any?
- What are the success criteria?
- What are the non-goals?
- What behavior are you intending to emulate? What are the quircks there?
- Is there an API spec being targeted?

**Example prompts to guide your thinking:**
- "Users currently struggle with..."
- "The system lacks..."
- "This creates friction when..."
- "Without this, contributors must..."

---

## Approach

*This section is REQUIRED before moving from Proposed to Accepted status.*

**Purpose:** Describe your proposed solution at a high level.

**Complete this section when:** You've received feedback that the problem is worth solving and you're ready to propose a specific approach.

**Progressive disclosure:** Start lean! You don't need all the details yet. Focus on the core idea and high-level approach. Detailed design comes later.

**Guidance:**
- What is your proposed solution?
- Why is this approach better than alternatives?
- What are the key benefits and tradeoffs?
- How does this fit with existing DocumentDB architecture?

**Example prompts for solution thinking:**
- "The proposed solution is to..."
- "This approach is preferable because..."
- "Key tradeoffs include..."
- "This aligns with existing patterns by..."

---

## Detailed Design

*This section MAY BE REQUIRED before moving from Proposed to Accepted status. This section MUST be completed and approved to move to Implementing status.*

**Purpose:** Provide comprehensive technical details needed for implementation.

**Complete this section when:** Your solution approach has been validated and you're ready to commit to specific implementation details.

**Guidance:** This is where you get specific. Include enough detail that someone could implement this RFC without having to make major design decisions.

### Technical Details

*Describe the technical implementation specifics*
- Data structures
- Algorithms
- Architecture patterns
- Performance considerations

### API Changes

*Document any public API additions or modifications*
- New functions, including UDFs
- Modified signatures
- Breaking changes
- Deprecation plans

### Database Schema Changes

*If applicable, describe schema modifications*
- New tables/collections
- Schema migrations
- Index changes
- Data migration strategies

### Configuration Changes

*Document new or modified configuration options*
- New settings
- Modified defaults
- Environment variables
- Configuration validation

### Testing Strategy

*Describe how this will be tested*
- Unit test approach
- Integration test requirements
- Compatibility test requirements
- Performance test plans
- Migration test strategy

### Migration Path

*How do existing users/deployments upgrade?*
- Backwards/forwards compatibility
- Migration steps
- Rollback strategy
- Deprecation timeline

### Documentation Updates

*What documentation needs to change?*
- User-facing docs
- Developer guides
- API references
- Examples/tutorials

---

## Implementation Tracking

*This section SHALL be populated during the Implementation phase.*

**Purpose:** Track the implementation progress of this RFC.

**Complete this section when:** Your RFC has been accepted and implementation work begins.

**Guidance:**
- Link to the PRs that implement this RFC. Update as implementation progresses.
- Provide success metrics.

### Implementation PRs

- [ ] PR #XXX: [Brief description of what this PR implements]
- [ ] PR #XXX: [Brief description of what this PR implements]
- [ ] PR #XXX: [Brief description of what this PR implements]

### Status Updates

*Add dated status updates as implementation progresses*

**YYYY-MM-DD:** Initial implementation started in PR #XXX

**YYYY-MM-DD:** [Update on progress, blockers, or changes]

### Open Questions

*Track unresolved questions that arise during implementation*

- [ ] Question: [Description]
  - Discussion: [Link to discussion or resolution]

### Implementation Notes

*Capture important decisions or learnings during implementation*

- **Decision [YYYY-MM-DD]:** [What was decided]
  - **Context:** [Why this decision was made]
  - **Alternatives:** [What else was considered]
