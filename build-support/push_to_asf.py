#!/usr/bin/env python

# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

# This script fetches branches from the Gerrit repository and
# allows ASF committers to propagate commits from gerrit into the
# official ASF repository.
#
# Current ASF policy is that this mirroring cannot be automatic
# and should be driven by a committer who inspects and signs off
# on the commits being made into the ASF. Additionally, the ASF
# prefers that in most cases, the committer according to source
# control should be the same person to push the commit to a git
# repository.
#
# This script provides the committer the opportunity to review the
# changes to be pushed, warns them if they are pushing code for
# which they weren't the committer, and performs the actual push.

import logging
import optparse
import re
import subprocess
import sys

from kudu_util import check_output, confirm_prompt, Colors, get_my_email

APACHE_REPO = "https://git-wip-us.apache.org/repos/asf/incubator-kudu.git"
GERRIT_URL_RE = re.compile(r"ssh://.+@gerrit.cloudera.org:29418/kudu")

# ANSI color codes.
Colors.RED = "\x1b[31m"
Colors.GREEN = "\x1b[32m"
Colors.YELLOW = "\x1b[33m"
Colors.RESET = "\x1b[m"

# Parsed options, filled in by main().
OPTIONS = None


def check_apache_remote():
  """
  Checks that there is a remote named 'apache' set up correctly.
  Otherwise, exits with an error message.
  """
  try:
    url = check_output(['git', 'config', '--local', '--get', 'remote.apache.url']).strip()
  except subprocess.CalledProcessError:
    print >>sys.stderr, "No remote named 'apache'. Please set one up, for example with: "
    print >>sys.stderr, "  git remote add apache", APACHE_REPO
    sys.exit(1)
  if url != APACHE_REPO:
    print >>sys.stderr, "Unexpected URL for remote 'apache'."
    print >>sys.stderr, "  Got:     ", url
    print >>sys.stderr, "  Expected:", APACHE_REPO
    sys.exit(1)


def check_gerrit_remote():
  """
  Checks that there is a remote named 'gerrit' set up correctly.
  Otherwise, exits with an error message.
  """
  try:
    url = check_output(['git', 'config', '--local', '--get', 'remote.gerrit.url']).strip()
  except subprocess.CalledProcessError:
    print >>sys.stderr, "No remote named 'gerrit'. Please set one up following "
    print >>sys.stderr, "the contributor guide."
    sys.exit(1)
  if not GERRIT_URL_RE.match(url):
    print >>sys.stderr, "Unexpected URL for remote 'gerrit'."
    print >>sys.stderr, "  Got:     ", url
    print >>sys.stderr, "  Expected to find host '%s' in the URL" % GERRIT_HOST
    sys.exit(1)


def fetch(remote):
  """Run git fetch for the given remote, including some logging."""
  logging.info("Fetching from remote '%s'..." % remote)
  subprocess.check_call(['git', 'fetch', remote])
  logging.info("done")


def get_branches(remote):
  """ Fetch a dictionary mapping branch name to SHA1 hash from the given remote. """
  out = check_output(["git", "ls-remote", remote, "refs/heads/*"])
  ret = {}
  for l in out.splitlines():
    sha, ref = l.split("\t")
    branch = ref.replace("refs/heads/", "", 1)
    ret[branch] = sha
  return ret


def rev_parse(rev):
  """Run git rev-parse, returning the sha1, or None if not found"""
  try:
    return check_output(['git', 'rev-parse', rev], stderr=subprocess.STDOUT).strip()
  except subprocess.CalledProcessError:
    return None


def rev_list(arg):
  """Run git rev-list, returning an array of SHA1 commit hashes."""
  return check_output(['git', 'rev-list', arg]).splitlines()


def describe_commit(rev):
  """ Return a one-line description of a commit. """
  return subprocess.check_output(
      ['git', 'log', '--color', '-n1', '--oneline', rev]).strip()


def is_fast_forward(ancestor, child):
  """
  Return True if 'child' is a descendent of 'ancestor' and thus
  could be fast-forward merged.
  """
  try:
    merge_base = check_output(['git', 'merge-base', ancestor, child]).strip()
  except:
    # If either of the commits is unknown, count this as a non-fast-forward.
    return False
  return merge_base == rev_parse(ancestor)


def get_committer_email(rev):
  """ Return the email address of the committer of the given revision. """
  return check_output(['git', 'log', '-n1', '--pretty=format:%ce', rev]).strip()


def do_update(branch, gerrit_sha, apache_sha):
  """
  Displays and performs a proposed update of the Apache repository
  for branch 'branch' from 'apache_sha' to 'gerrit_sha'.
  """
  # First, verify that the update is fast-forward. If it's not, then something
  # must have gotten committed to Apache outside of gerrit, and we'd need some
  # manual intervention.
  if not is_fast_forward(apache_sha, gerrit_sha):
    print >>sys.stderr, "Cannot update branch '%s' from gerrit:" % branch
    print >>sys.stderr, "Apache revision %s is not an ancestor of gerrit revision %s" % (
      apache_sha[:8], gerrit_sha[:8])
    print >>sys.stderr, "Something must have been committed to Apache and bypassed gerrit."
    print >>sys.stderr, "Manual intervention is required."
    sys.exit(1)

  # List the commits that are going to be pushed to the ASF, so that the committer
  # can verify and "sign off".
  commits = rev_list("%s..%s" % (apache_sha, gerrit_sha))
  commits.reverse()  # Display from oldest to newest.
  print "-" * 60
  print Colors.GREEN + ("%d commit(s) need to be pushed from Gerrit to ASF:" % len(commits)) + Colors.RESET
  push_sha = None
  for sha in commits:
    oneline = describe_commit(sha)
    print "  ", oneline
    committer = get_committer_email(sha)
    if committer != get_my_email():
      print Colors.RED + "   !!! Committed by someone else (%s) !!!" % committer, Colors.RESET
      if not confirm_prompt(
          Colors.RED + "   !!! Are you sure you want to push on behalf of another committer?" + Colors.RESET):
        # Even if they don't want to push this commit, we could still push any
        # earlier commits that the user _did_ author.
        if push_sha is not None:
          print "... will still update to prior commit %s..." % push_sha
        break
    push_sha = sha
  if push_sha is None:
    print "Nothing to push"
    return

  # Everything has been confirmed. Do the actual push
  cmd = ['git', 'push', 'apache']
  if OPTIONS.dry_run:
    cmd.append('--dry-run')
  cmd.append('%s:refs/heads/%s' % (push_sha, branch))
  print Colors.GREEN + "Running: " + Colors.RESET + " ".join(cmd)
  subprocess.check_call(cmd)
  print Colors.GREEN + "Successfully updated %s to %s" % (branch, gerrit_sha) + Colors.RESET
  print


def main():
  global OPTIONS
  p = optparse.OptionParser(
    epilog=("See the top of the source code for more information on the purpose of " +
            "this script."))
  p.add_option("-n", "--dry-run", action="store_true",
               help="Perform git pushes with --dry-run")
  OPTIONS, args = p.parse_args()
  if args:
    p.error("no arguments expected")
    sys.exit(1)

  # Pre-flight checks.
  check_apache_remote()
  check_gerrit_remote()

  # Ensure we have the latest state of gerrit.
  fetch('gerrit')

  # Check the current state of branches on Apache.
  # For each branch, we try to update it if the revisions don't match.
  apache_branches = get_branches('apache')
  for branch, apache_sha in sorted(apache_branches.iteritems()):
    gerrit_sha = rev_parse("remotes/gerrit/" + branch)
    print "Branch '%s':\t" % branch,
    if gerrit_sha is None:
      print Colors.YELLOW, "found on Apache but not in gerrit", Colors.RESET
      continue
    if gerrit_sha == apache_sha:
      print Colors.GREEN, "up to date", Colors.RESET
      continue
    print Colors.YELLOW, "needs update", Colors.RESET
    do_update(branch, gerrit_sha, apache_sha)


if __name__ == "__main__":
  logging.basicConfig(level=logging.INFO)
  main()
