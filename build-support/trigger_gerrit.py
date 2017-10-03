#!/usr/bin/env python
#
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


# This tool triggers a Jenkins build based on a particular gerrit URL.
# The Jenkins build will post a +1 or -1 on the gerrit.
#
# NOTE: currently this is hard-coded to an internal Cloudera server.
# We plan to move to upstream infrastructure at some later date.

import logging
import json
import re
import subprocess
import sys
import urllib
import urllib2
import urlparse

from kudu_util import check_output

GERRIT_HOST = "gerrit.cloudera.org"
JENKINS_URL = "http://sandbox.jenkins.cloudera.com/"
JENKINS_JOB = "kudu-public-gerrit"


def get_gerrit_ssh_command():
  url = check_output("git config --get remote.gerrit.url".split(" "))
  m = re.match(r'ssh://(.+)@(.+):(\d+)/.+', url)
  if not m:
    raise Exception("expected gerrit remote to be an ssh://user@host:port/ URL: %s" % url)
  user, host, port = m.groups()
  if host != GERRIT_HOST:
    raise Exception("unexpected gerrit host %s in remote 'gerrit'. Expected %s" % (
        host, GERRIT_HOST))
  return ["ssh", "-p", port, "-l", user, host]


def current_ref_for_gerrit_number(change_num):
  j = check_output(get_gerrit_ssh_command() + [
      "gerrit", "query", "--current-patch-set", "--format", "JSON",
      "change:%d" % change_num])
  j = json.loads(j.split("\n")[0])
  return j['currentPatchSet']['ref']


def url_to_ref(url):
  u = urlparse.urlparse(url)
  if not u.netloc.startswith(GERRIT_HOST):
    print >>sys.stderr, "unexpected gerrit host %s, expected %s\n" % (
        u.netloc, GERRIT_HOST)
    usage()
    sys.exit(1)
  if u.path == '/':
    m = re.match(r'/c/(\d+)/', u.fragment)
    if m:
      return current_ref_for_gerrit_number(int(m.group(1)))
  print >>sys.stderr, "invalid gerrit URL: ", url
  usage()
  sys.exit(1)

def usage():
  print >>sys.stderr, "usage: %s <url>\n" % sys.argv[0]
  print >>sys.stderr, "The provided URL should look something like:"
  print >>sys.stderr, "http://gerrit.cloudera.org:8080/#/c/963/\n"


def determine_ref():
  if len(sys.argv) != 2:
    usage()
    sys.exit(1)

  arg = sys.argv[1]
  if arg.startswith("http"):
    return url_to_ref(arg)
  else:
    print >>sys.stderr, "Unable to parse argument: %s\n" % arg
    sys.exit(1)


def trigger_jenkins(ref):
  logging.info("Will trigger Jenkins for ref %s" % ref)
  url = "%s/job/%s/buildWithParameters" % (JENKINS_URL, JENKINS_JOB)
  params = dict(GERRIT_BRANCH=ref)
  req = urllib2.Request(url,
                        data=urllib.urlencode(params),
                        headers={"Accept": "application/json"})
  urllib2.urlopen(req).read()
  logging.info("Successfuly triggered jenkins job!")


def main():
  gerrit_ref = determine_ref()
  trigger_jenkins(gerrit_ref)

if __name__ == "__main__":
  logging.basicConfig(level=logging.INFO)
  main()
