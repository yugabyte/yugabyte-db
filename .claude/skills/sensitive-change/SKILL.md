---
name: sensitive-change
description: >-
  Work on a security fix or a customer-sensitive change so that the public PR
  stays mechanical and the reasoning stays in JIRA. Use when the user says
  "vulnerability mode" or "sensitive mode", names a Vulnerability ticket to
  work on, or the change fixes a security issue or exists because of one
  customer.
---

# Sensitive change

A security fix or a customer-driven change has two audiences. The public repo gets the change
and a description of what it does. JIRA gets why it is needed, who is affected and what is
still open. This skill keeps the two apart for the whole session. The rules themselves are in
[`src/AGENTS.md` § Security and customer-sensitive
changes](../../../src/AGENTS.md#security-and-customer-sensitive-changes); read that section first.

## Step 1: Ticket and tier

1. Get the JIRA key from the user. If there is none, file one with `/create-issue`. A security
   fix is always a JIRA ticket of type **Vulnerability**, never a GitHub issue. Confirm the
   summary and description with the user before creating it.
2. Pick the tier with the user and say which one you are using:
   - `public-cve`: a published CVE in a dependency we bundle or link.
   - `embargoed`: a defect in our own code, or a finding that is not public yet.
   - `customer`: the change exists because of one customer.

Keep the tier for the rest of the session. If a change fits two tiers, use the stricter one.

## Step 2: Do the work

- Name the branch after the change (`validate-before-use`), with the JIRA key if you like. For
  `embargoed`, the branch, test names and comments must not say it is a security fix.
- Write commit messages that say what the code does. For `embargoed` and `customer`, every
  commit on the branch follows the same rule, not only the first.
- Put everything else in the JIRA ticket as you go: the finding, how it can be reached,
  affected releases and images, scan links, what stays unfixed and why, and the customer.

## Step 3: Publish

Use `/create-pr` (or `/create-diff`) and pass the tier to the script:

```
.agents/scripts/create-pr.sh -s <tier> -i <JIRA key> ...
```

`-s` runs `.agents/scripts/check-public-text.sh` over the title, the body, the commit messages
and the branch name, and stops before the push if it finds wording the tier does not allow. Set
`SENSITIVE_WORDS` to a comma-separated list, such as the customer's name, to have those checked
too. The check is a backstop; read the text yourself first.

When a fix goes to release branches, open those PRs together with the master PR so they can
merge on the same day. A fix that sits on master alone tells readers what the released
branches lack.

## Step 4: Review

- For `embargoed` and `customer`, answer review questions that need the sensitive context in
  the JIRA ticket, and reply on the PR with a pointer to it.
- For every tier, never write on the PR which releases, branches, images or components do not
  have the fix, or how the defect could be used.
- The merged commit message keeps the same wording as the PR. Fuller public wording belongs in
  the release notes and the disclosure page once fixed builds have shipped.
