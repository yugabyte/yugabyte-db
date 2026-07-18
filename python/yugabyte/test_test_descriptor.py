# Copyright (c) YugabyteDB, Inc.
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

from typing import List

from yugabyte import test_descriptor


TEST_DESCRIPTORS_STR = '''
com.yugabyte.jedis.TestYBJedis#testPool[1]
org.yb.cql.TestAuthorizationEnforcement#testGrantRoleWithoutPermissions
org.yb.cql.TestVarIntDataType#testVarIntMultipleComparisonInRangeRandom
org.yb.pgsql.TestPgSequencesWithServerCacheFlag#testDefaultCacheOption
org.yb.pgsql.TestPgWrapper#testSimpleDDL
org.yb.util.TestNetUtil#testHostsAndPortsToString
redis.clients.jedis.tests.commands.SortedSetCommandsTest#zrevrank
tests-docdb/docdb-test:::DocDBTests/DocDBTestWrapper.TestUserTimestamp/0
tests-gutil/string_util-test:::StringUtilTest.TestAppendWithSeparator
tests-integration-tests/cdcsdk_ysql-test:::CDCSDKYsqlTest.TestExpiredStreamWithCompaction
tests-integration-tests/create-table-stress-test:::MultiHeartbeat/CreateMultiHBTableStressTest.Rest\
artServersAfterCreation/1
tests-integration-tests/remote_bootstrap-itest:::RemoteBootstrapITest.TestDeleteLeaderDuringRemoteB\
ootstrapStressTestKeyValueType
tests-integration-tests/xcluster-test:::XClusterTestParams/XClusterTest.TestNonZeroLagMetricsWithou\
tGetChange/0
tests-integration-tests/xcluster_ysql-test:::XClusterYsqlTest.TestColocatedTablesReplicationWithLar\
geTableCount
tests-master/catalog_manager-test:::TestCatalogManager.TestLoadNotBalanced
tests-pgwrapper/pg_cancel_transaction-test:::PgCancelTransactionTest.TestCancelWithInvalidStatusTab\
let
tests-pgwrapper/pg_ddl_atomicity-test:::PgDdlAtomicityConcurrentDdlTest.ConcurrentDdl
tests-pgwrapper/pg_libpq-test:::PgLibPqTest.ConcurrentIndexInsert
tests-pgwrapper/pg_row_lock-test:::PgRowLockTest.SerializableInsertForNoKeyUpdate
tests-rocksdb/db_compaction_test:::DBCompactionTestWithParam/DBCompactionTestWithParam.CompactionsG\
enerateMultipleFiles/2
tests-rocksdb/db_test:::DBTest.IterNextWithNewerSeq
tests-rocksdb/db_test:::DBTest.kPointInTimeRecovery
tests-rocksdb/db_test:::DBTestRandomized/DBTestRandomized.Randomized/19
tests-rocksdb/db_universal_compaction_test:::UniversalCompactionNumLevels/DBTestUniversalCompaction\
WithParam.UniversalCompactionFourPaths/2
tests-rocksdb/log_test:::bool/LogTest.ClearEofError/0
tests-tablet/verifyrows-tablet-test:::VerifyRowsTabletTest/0.DoTestAllAtOnce
tests-tools/yb-admin-snapshot-schedule-test:::YbAdminSnapshotScheduleTestWithLB.TestLBHiddenTables
tests-tools/yb-admin-test:::AdminCliTest.GetIsLoadBalancerIdle
tests-tools/yb-backup-cross-feature-test:::YBBackupTest.TestYSQLManualTabletSplit
tests-util/flags-test:::FlagsTest.TestSetFlagDefault
tests-util/trace-test:::TraceTest.TestVLogTrace
'''


def test_test_descriptor() -> None:
    all_simple_test_descriptors: List[test_descriptor.SimpleTestDescriptor] = []

    num_cxx = 0
    num_java = 0
    for line in TEST_DESCRIPTORS_STR.split('\n'):
        line = line.strip()
        if not line:
            continue
        test_desc = test_descriptor.TestDescriptor(line)
        simple_test_descriptor = test_descriptor.SimpleTestDescriptor.parse(line)
        if simple_test_descriptor.language == test_descriptor.LanguageOfTest.CPP:
            assert test_desc.language == 'C++'
            num_cxx += 1
            assert not test_desc.is_jvm_based

            expected_args_for_run_test = simple_test_descriptor.cxx_rel_test_binary
            assert expected_args_for_run_test is not None
            if simple_test_descriptor.class_name is not None:
                expected_args_for_run_test += ' ' + simple_test_descriptor.class_name
                if simple_test_descriptor.test_name:
                    expected_args_for_run_test += '.' + simple_test_descriptor.test_name
        elif simple_test_descriptor.language == test_descriptor.LanguageOfTest.JAVA:
            assert test_desc.language == 'Java'
            num_java += 1
            assert test_desc.is_jvm_based
            expected_args_for_run_test = simple_test_descriptor.class_name
            assert expected_args_for_run_test is not None
            if simple_test_descriptor.test_name is not None:
                expected_args_for_run_test += '#' + simple_test_descriptor.test_name
        else:
            raise ValueError("Unknown language: %s" % simple_test_descriptor.language)
        assert test_desc.attempt_index == 1
        assert test_desc.args_for_run_test == expected_args_for_run_test
        all_simple_test_descriptors.append(simple_test_descriptor)
        assert str(simple_test_descriptor) == line

    all_simple_test_descriptors.sort()
    assert num_cxx == 24
    assert num_java == 7


if __name__ == '__main__':
    test_test_descriptor()
