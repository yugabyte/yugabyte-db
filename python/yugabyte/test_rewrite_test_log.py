# Copyright (c) Yugabyte, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.

import pathlib
import os
import subprocess

from yugabyte.rewrite_test_log import LogRewriterConf, LogRewriter


def test_java_test_log_rewrite(tmp_path: pathlib.Path) -> None:
    input_path = os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        'test_data',
        'org_yb_pgsql_TestDropTableWithConcurrentTxn_testDmlTxnDrop_1pct_sample.log')
    expected_output_path = os.path.splitext(input_path)[0] + '.out'
    # This is not a temporary directory that we are using here to store the output, this is the
    # temporary directory that the test whose log we are rewriting used.
    test_tmpdir = ('/tmp/yb_test.org.yb.pgsql.TestDropTableWithConcurrentTxn#testDmlTxnDrop.' +
                   '20220707230115167368.7164446636')
    output_path = tmp_path / (os.path.basename(input_path) + '.rewritten')
    yb_src_root = '/var/lib/jenkins/workspace/yugabyte-db'
    conf = LogRewriterConf(
        input_log_path=input_path,
        verbose=True,
        yb_src_root=yb_src_root,
        build_root=os.path.join(yb_src_root, 'build', 'release-clang12-dynamic-ninja'),
        test_tmpdir=test_tmpdir,
        replace_original=False,
        output_log_path=str(output_path),
    )
    rewriter = LogRewriter(conf)
    rewriter.run()
    subprocess.check_call([
        'diff',
        expected_output_path,
        output_path])
