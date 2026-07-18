#!/usr/bin/env python3
"""
OSS Commit Tracker for YugabyteDB.

Identifies open-source contributions from merged GitHub PRs
and adds the "oss_commit" label to the referenced GitHub
issues.  The existing github-jira-sync integration
propagates this label to Jira automatically.

Classification:
  Author is a 'yugabyte' GitHub org member -> internal
  Otherwise -> OSS contribution

Required environment variables:
  GITHUB_TOKEN       - PAT with read:org + issues write
  GITHUB_REPOSITORY  - owner/repo  (set by Actions)
  GITHUB_EVENT_PATH  - webhook event JSON  (set by Actions)

Optional:
  YB_GITHUB_ORG      - GitHub org name, default "yugabyte"
"""

import json
import logging
import os
import re
import sys
from dataclasses import dataclass, field
from enum import Enum
from urllib.error import HTTPError
from urllib.request import Request, urlopen

logging.basicConfig(
    level=logging.INFO, format="%(levelname)s: %(message)s"
)
log = logging.getLogger("oss-tracker")


class Classification(Enum):
    OSS = "oss"
    INTERNAL = "internal"


@dataclass
class PRInfo:
    number: int
    title: str
    author: str
    head_repo_full_name: str
    merge_commit_sha: str
    html_url: str
    commit_body: str = ""


@dataclass
class ClassificationResult:
    classification: Classification
    reason: str
    github_issues: list = field(default_factory=list)


# -----------------------------------------------------------
# GitHub helpers
# -----------------------------------------------------------

def github_api_get(
    endpoint: str, token: str
) -> dict:
    url = f"https://api.github.com{endpoint}"
    req = Request(url, headers={
        "Authorization": f"token {token}",
        "Accept": "application/vnd.github+json",
    })
    with urlopen(req) as resp:
        return json.loads(resp.read())


def github_api_post(
    endpoint: str, token: str, body: dict
) -> dict:
    url = f"https://api.github.com{endpoint}"
    data = json.dumps(body).encode()
    req = Request(url, data=data, headers={
        "Authorization": f"token {token}",
        "Accept": "application/vnd.github+json",
        "Content-Type": "application/json",
    })
    with urlopen(req) as resp:
        return json.loads(resp.read())


def is_org_member(
    username: str, org: str, token: str
) -> bool:
    """Check org membership.  Requires read:org scope."""
    url = (
        f"https://api.github.com"
        f"/orgs/{org}/members/{username}"
    )
    req = Request(url, headers={
        "Authorization": f"token {token}",
        "Accept": "application/vnd.github+json",
    })
    try:
        with urlopen(req) as resp:
            return resp.status == 204
    except HTTPError as e:
        if e.code == 404:
            return False
        raise


def get_commit_body(
    repo: str, sha: str, token: str
) -> str:
    try:
        data = github_api_get(
            f"/repos/{repo}/commits/{sha}", token
        )
        return data.get("commit", {}).get("message", "")
    except Exception:
        log.warning(
            "Could not fetch commit body for %s", sha
        )
        return ""


def add_label_to_issue(
    repo: str, issue_number: int, label: str, token: str
) -> bool:
    """Add a label to a GitHub issue."""
    try:
        endpoint = (
            f"/repos/{repo}/issues/{issue_number}/labels"
        )
        github_api_post(
            endpoint, token, body={"labels": [label]}
        )
        log.info(
            "Added label '%s' to issue #%d",
            label, issue_number,
        )
        return True
    except Exception as exc:
        log.error(
            "Failed to add label '%s' to issue #%d: %s",
            label, issue_number, exc,
        )
        return False


# -----------------------------------------------------------
# Classification logic
# -----------------------------------------------------------

ISSUE_PATTERNS = [
    re.compile(r"\[#(\d+)\]"),                   # [#12345]
    re.compile(                                   # Fixes #N
        r"(?:fixes|closes|resolves)\s+#(\d+)",
        re.IGNORECASE,
    ),
    re.compile(r"\(#(\d+)\)\s*$"),               # (#12345)
]


def extract_issue_numbers(title: str, body: str) -> list:
    combined = f"{title}\n{body}"
    nums = set()
    for pat in ISSUE_PATTERNS:
        nums.update(int(m) for m in pat.findall(combined))
    return sorted(nums)


def classify_pr(
    pr: PRInfo, org: str, token: str
) -> ClassificationResult:
    author = pr.author
    github_issues = extract_issue_numbers(
        pr.title, pr.commit_body
    )

    def _result(cls, reason):
        return ClassificationResult(
            cls, reason, github_issues
        )

    if is_org_member(author, org, token):
        return _result(
            Classification.INTERNAL,
            f"'{author}' is a member of '{org}' org",
        )

    return _result(
        Classification.OSS,
        f"'{author}' is not a member of '{org}' org",
    )


# -----------------------------------------------------------
# GitHub Actions output helpers
# -----------------------------------------------------------

def write_summary(text: str):
    summary_file = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary_file:
        with open(summary_file, "a") as f:
            f.write(text + "\n")


# -----------------------------------------------------------
# Main
# -----------------------------------------------------------

def main():
    token = os.environ.get("GITHUB_TOKEN", "")
    if not token:
        log.error("GITHUB_TOKEN is required")
        sys.exit(1)

    event_path = os.environ.get("GITHUB_EVENT_PATH", "")
    if not event_path or not os.path.exists(event_path):
        log.error(
            "GITHUB_EVENT_PATH not set or file missing"
        )
        sys.exit(1)

    repo = os.environ.get(
        "GITHUB_REPOSITORY", "yugabyte/yugabyte-db"
    )
    org = os.environ.get("YB_GITHUB_ORG", "yugabyte")

    with open(event_path) as f:
        event = json.load(f)

    pull_request = event.get("pull_request", {})
    if not pull_request:
        log.info(
            "No pull_request in event payload; skipping."
        )
        return

    if not pull_request.get("merged"):
        log.info("PR closed without merging; skipping.")
        return

    head_repo = (
        pull_request.get("head", {}).get("repo", {}) or {}
    )
    user = pull_request.get("user", {})

    pr = PRInfo(
        number=pull_request["number"],
        title=pull_request.get("title", ""),
        author=user.get("login", ""),
        head_repo_full_name=head_repo.get(
            "full_name", ""
        ),
        merge_commit_sha=pull_request.get(
            "merge_commit_sha", ""
        ),
        html_url=pull_request.get("html_url", ""),
    )

    log.info(
        "Processing PR #%d by %s: %s",
        pr.number, pr.author, pr.title,
    )

    if pr.merge_commit_sha:
        pr.commit_body = get_commit_body(
            repo, pr.merge_commit_sha, token
        )

    result = classify_pr(pr, org, token)

    cls_value = result.classification.value
    log.info(
        "Classification: %s - %s", cls_value, result.reason
    )
    log.info(
        "GitHub issues: %s", result.github_issues
    )

    if result.classification != Classification.OSS:
        summary = (
            f"### PR #{pr.number} - "
            f"{cls_value.upper()}\n"
            f"**Author:** {pr.author}\n"
            f"**Reason:** {result.reason}\n"
        )
        write_summary(summary)
        log.info("Not an OSS contribution. Done.")
        return

    # --- OSS contribution detected ---
    label = "oss_commit"
    labeled_issues = []

    if not result.github_issues:
        log.warning(
            "No GitHub issues referenced in PR #%d "
            "title - cannot label. Title: %s",
            pr.number, pr.title,
        )

    for issue_num in result.github_issues:
        if add_label_to_issue(
            repo, issue_num, label, token
        ):
            labeled_issues.append(issue_num)

    gh_col = (
        ", ".join(f"#{i}" for i in result.github_issues)
        if result.github_issues
        else "N/A"
    )
    labeled_col = (
        ", ".join(f"#{i}" for i in labeled_issues)
        if labeled_issues
        else "N/A"
    )
    summary = (
        f"### OSS Contribution Detected\n"
        f"| Field | Value |\n"
        f"|-------|-------|\n"
        f"| PR | [#{pr.number}]({pr.html_url}) |\n"
        f"| Author | @{pr.author} |\n"
        f"| Fork | `{pr.head_repo_full_name}` |\n"
        f"| Reason | {result.reason} |\n"
        f"| Issues ref'd | {gh_col} |\n"
        f"| Issues labeled | {labeled_col} |\n"
    )
    write_summary(summary)
    log.info(
        "OSS tracker complete. Labeled issues: %s",
        labeled_issues,
    )


if __name__ == "__main__":
    main()
