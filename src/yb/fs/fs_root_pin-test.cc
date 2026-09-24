// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
// Tests for the swapped-data-mount detector.
//
// Three layers, deliberately separate:
//   FsRootPinDecisionTest  -- the decision table, as a pure function. No filesystem.
//   FsRootPinFileTest      -- the pin file itself: JSON, atomic write, torn and corrupt files.
//   FsRootPinManagerTest   -- FsManager end to end on real directories, including the swap.
//
// Between them the layers cover every row of EvaluateFsRootPins' decision table and every anomaly,
// the policy matrix (refuse_on_pin_mismatch / refuse_on_superblock_conflict / write_pins), the
// repair matrix
// (DecideRepin against each verdict), the exit code for each severity, and the reporting rules
// that only bite when a node is wrong in more than one way at once -- a failure on one root must
// not swallow the warnings on its siblings.
//
// Not covered here: TSTabletManager::Init's evidence collection and the end-to-end refusal on a
// real tablet server, both demonstrated by integration-tests/fs_root_pin-itest (this file drives
// CertifyDataRoots directly), and the yb-fs_pin binary itself, which nothing automated drives.

#include <gtest/gtest.h>

#include "yb/fs/fs_manager.h"
#include "yb/fs/fs_root_pin.h"

#include "yb/util/env.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/path_util.h"
#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/tostring.h"

using std::string;
using std::vector;

DECLARE_bool(fs_root_pin_enforce);
DECLARE_bool(fs_root_pin_refuse_on_superblock_conflict);
DECLARE_bool(fs_root_pin_write);

namespace yb {

namespace {

const char* const kServerType = "tserver_test";

TabletSuperblockEvidence Evidence(
    const string& tablet_id, const string& found_under, const string& recorded_data,
    const string& recorded_wal = "") {
  TabletSuperblockEvidence e;
  e.tablet_id = tablet_id;
  e.containing_root = found_under;
  e.recorded_data_root = recorded_data;
  e.recorded_wal_root = recorded_wal.empty() ? recorded_data : recorded_wal;
  return e;
}

TabletSuperblockEvidence UnreadableSuperblock(
    const string& tablet_id, const string& found_under, const string& error) {
  TabletSuperblockEvidence e;
  e.tablet_id = tablet_id;
  e.containing_root = found_under;
  e.read_error = error;
  return e;
}

FsRootPinFile PresentPin(const string& mount_point) {
  FsRootPinFile f;
  f.state = FsRootPinFileState::kPresent;
  f.pin.mount_point = mount_point;
  f.pin.pinned_at = "2026-07-29T18:22:04Z";
  f.pin.filesystem_uuid = "b3f2a41c-9d0e-4c77-8a15-2e6f0c9d1b44";
  return f;
}

FsRootPinFile UnreadablePin(const string& error) {
  FsRootPinFile f;
  f.state = FsRootPinFileState::kUnparseable;
  f.error = error;
  return f;
}

FsRootPinFile IoErrorPin(const string& error) {
  FsRootPinFile f;
  f.state = FsRootPinFileState::kIoError;
  f.error = error;
  return f;
}

FsRootState StateOf(const FsRootPinReport& report, const string& root) {
  const auto* v = report.FindRoot(root);
  EXPECT_NE(v, nullptr) << "no verdict for root " << root;
  return v ? v->state : FsRootState::kPending;
}

FsRootRepinDecision RepinOf(const FsRootPinReport& report, const string& root) {
  const auto* v = report.FindRoot(root);
  EXPECT_NE(v, nullptr) << "no verdict for root " << root;
  return v ? DecideRepin(*v) : FsRootRepinDecision::kNotNeeded;
}

// The policy a master build boots with, spelled out so a change to a default shows up here.
FsRootPinStartupPolicy DefaultPolicy() { return FsRootPinStartupPolicy(); }

FsRootPinStartupPolicy StrictPolicy() {
  FsRootPinStartupPolicy p;
  p.refuse_on_superblock_conflict = true;
  return p;
}

FsRootPinStartupPolicy WarnOnlyPolicy() {
  FsRootPinStartupPolicy p;
  p.refuse_on_pin_mismatch = false;
  return p;
}

}  // namespace

// ==========================================================================
//  The decision table, as a pure function
// ==========================================================================

class FsRootPinDecisionTest : public YBTest {
 public:
  // Two data roots, the shape every anomaly in the design doc is written against.
  FsRootPinInputs TwoRoots() {
    FsRootPinInputs inputs;
    inputs.server_type = kServerType;
    inputs.data_roots = {kD0, kD1};
    inputs.wal_roots = {kD0, kD1};
    inputs.evidence_complete = FsRootEvidenceComplete::kTrue;
    return inputs;
  }

  const string kD0 = "/mnt/d0";
  const string kD1 = "/mnt/d1";
};

// Pin present and it names this root: proceed, whatever the tablets say.
TEST_F(FsRootPinDecisionTest, PinMatchesProceeds) {
  auto inputs = TwoRoots();
  inputs.pin_files[kD0] = PresentPin(kD0);
  inputs.pin_files[kD1] = PresentPin(kD1);
  inputs.evidence = {Evidence("t1", kD0, kD0), Evidence("t2", kD1, kD1)};

  auto report = EvaluateFsRootPins(inputs);
  ASSERT_EQ(FsRootState::kVerified, StateOf(report, kD0));
  ASSERT_EQ(FsRootState::kVerified, StateOf(report, kD1));
  ASSERT_FALSE(report.HasFailure());
  ASSERT_FALSE(report.HasWarnings());
  ASSERT_TRUE(report.FailureMessage().empty());
  ASSERT_TRUE(report.CertifiableRoots().empty());
}

// The incident: two roots, pins planted, mounts swapped. Refuse, naming both.
TEST_F(FsRootPinDecisionTest, SwappedMountsRefuseNamingBoth) {
  auto inputs = TwoRoots();
  inputs.pin_files[kD0] = PresentPin(kD1);  // The volume at /mnt/d0 was pinned at /mnt/d1.
  inputs.pin_files[kD1] = PresentPin(kD0);
  inputs.evidence = {Evidence("t1", kD0, kD1), Evidence("t2", kD1, kD0)};

  auto report = EvaluateFsRootPins(inputs);
  ASSERT_EQ(FsRootState::kMismatched, StateOf(report, kD0));
  ASSERT_EQ(FsRootState::kMismatched, StateOf(report, kD1));
  ASSERT_TRUE(report.HasFailure());
  // Nothing is written on a refusal, however consistent the rest of the node looks.
  ASSERT_TRUE(report.CertifiableRoots().empty());

  const auto message = report.FailureMessage();
  ASSERT_STR_CONTAINS(message, "/mnt/d0 holds the volume pinned at /mnt/d1");
  ASSERT_STR_CONTAINS(message, "/mnt/d1 holds the volume pinned at /mnt/d0");
  ASSERT_STR_CONTAINS(message, "/mnt/d0 and /mnt/d1 appear to be swapped.");
  ASSERT_STR_CONTAINS(message, "No data has been lost");
  ASSERT_STR_CONTAINS(message, "Do not delete anything");
}

// A failing root must not suppress the warnings about the others. The scenario that motivated this:
// /mnt/d0 swapped and /mnt/d2's volume not mounted. If the refusal names only d0, the operator
// remounts d0, restarts, and d2 is silently adopted as a fresh empty root.
TEST_F(FsRootPinDecisionTest, WarningsSurviveAlongsideAFailure) {
  FsRootPinInputs inputs;
  inputs.server_type = kServerType;
  inputs.data_roots = {"/mnt/d0", "/mnt/d1", "/mnt/d2"};
  inputs.wal_roots = inputs.data_roots;
  inputs.evidence_complete = FsRootEvidenceComplete::kTrue;
  inputs.pin_files["/mnt/d0"] = PresentPin("/mnt/d1");   // failure: mismatched
  inputs.pin_files["/mnt/d1"] = PresentPin("/mnt/d1");   // fine
  inputs.unformatted_roots = {"/mnt/d2"};                // warning: no instance file

  auto report = EvaluateFsRootPins(inputs);
  ASSERT_TRUE(report.HasFailure());
  // Top severity is failure, so HasWarnings() (which is Severity() == kWarning exactly) is false
  // here. A caller gating warning output on it would drop the warning text entirely.
  ASSERT_FALSE(report.HasWarnings());

  ASSERT_STR_CONTAINS(report.FailureMessage(), "/mnt/d0 holds the volume pinned at /mnt/d1");

  const auto warnings = report.WarningMessage();
  ASSERT_FALSE(warnings.empty()) << "warning content must survive a failing sibling";
  ASSERT_STR_CONTAINS(warnings, "/mnt/d2 has no instance file");
  // And it must say what is about to happen to it, not only what was seen.
  ASSERT_STR_CONTAINS(warnings, "adopted as a new, empty data directory");
}

// The st_dev hint is diagnostic only, but when the caller supplies it the warning must use it: an
// unmounted mount point is the case an operator most needs pointed out.
TEST_F(FsRootPinDecisionTest, UnformattedRootOnRootFilesystemSaysSo) {
  auto inputs = TwoRoots();
  inputs.pin_files[kD0] = PresentPin(kD0);
  inputs.unformatted_roots = {kD1};
  inputs.likely_unmounted_roots = {kD1};

  auto report = EvaluateFsRootPins(inputs);
  ASSERT_EQ(FsRootState::kUnformatted, StateOf(report, kD1));
  ASSERT_STR_CONTAINS(report.WarningMessage(), "same device as / while its sibling data roots");

  // Without the hint the warning stays generic rather than inventing the diagnosis.
  inputs.likely_unmounted_roots.clear();
  auto quiet = EvaluateFsRootPins(inputs);
  ASSERT_STR_NOT_CONTAINS(quiet.WarningMessage(), "same device as /");
  ASSERT_STR_CONTAINS(quiet.WarningMessage(), "/mnt/d1 has no instance file");
}

// One volume arrives from somewhere else, i.e., not a permutation of this node's own roots.
// Still a refusal, but we must not claim a swap we cannot see.
TEST_F(FsRootPinDecisionTest, MismatchThatIsNotASwapDoesNotClaimOne) {
  auto inputs = TwoRoots();
  inputs.pin_files[kD0] = PresentPin("/mnt/d7");
  inputs.pin_files[kD1] = PresentPin(kD1);

  auto report = EvaluateFsRootPins(inputs);
  ASSERT_TRUE(report.HasFailure());
  const auto message = report.FailureMessage();
  ASSERT_STR_CONTAINS(message, "/mnt/d0 holds the volume pinned at /mnt/d7");
  ASSERT_STR_NOT_CONTAINS(message, "appear to be swapped");
}

// Three roots rotated onto each other's mount points.
TEST_F(FsRootPinDecisionTest, ThreeWayRotationIsReportedAsSuch) {
  FsRootPinInputs inputs;
  inputs.server_type = kServerType;
  inputs.data_roots = {"/mnt/d0", "/mnt/d1", "/mnt/d2"};
  inputs.wal_roots = inputs.data_roots;
  inputs.evidence_complete = FsRootEvidenceComplete::kTrue;
  inputs.pin_files["/mnt/d0"] = PresentPin("/mnt/d1");
  inputs.pin_files["/mnt/d1"] = PresentPin("/mnt/d2");
  inputs.pin_files["/mnt/d2"] = PresentPin("/mnt/d0");

  auto report = EvaluateFsRootPins(inputs);
  ASSERT_TRUE(report.HasFailure());
  ASSERT_STR_CONTAINS(
      report.FailureMessage(), "remounted onto each other's mount points");
}

// An unparseable pin refuses. It is emphatically not treated as absent: doing so would let a torn
// write re-certify whatever layout happens to be mounted.
TEST_F(FsRootPinDecisionTest, UnreadablePinRefusesAndIsNotTreatedAsAbsent) {
  auto inputs = TwoRoots();
  inputs.pin_files[kD0] = UnreadablePin("not valid JSON: Missing a closing quotation mark");
  inputs.pin_files[kD1] = PresentPin(kD1);
  // Evidence that would otherwise certify /mnt/d0 happily.
  inputs.evidence = {Evidence("t1", kD0, kD0), Evidence("t2", kD1, kD1)};

  auto report = EvaluateFsRootPins(inputs);
  ASSERT_EQ(FsRootState::kPinUnparseable, StateOf(report, kD0));
  ASSERT_TRUE(report.HasFailure());
  ASSERT_TRUE(report.CertifiableRoots().empty());
  ASSERT_STR_CONTAINS(report.FailureMessage(), "pin file whose contents are unusable");
  ASSERT_STR_CONTAINS(report.FailureMessage(), "Missing a closing quotation mark");

  // The evidence still says the data belongs here, so the corrupt pin is repairable in place.
  ASSERT_EQ(FsRootRepinDecision::kSafe, RepinOf(report, kD0));
}

// Certification: no pin, and every superblock under the root names the root. Write the pin.
TEST_F(FsRootPinDecisionTest, ConsistentSuperblocksCertify) {
  auto inputs = TwoRoots();
  inputs.evidence = {
      Evidence("t1", kD0, kD0), Evidence("t2", kD0, kD0), Evidence("t3", kD1, kD1)};

  auto report = EvaluateFsRootPins(inputs);
  ASSERT_EQ(FsRootState::kCertifiable, StateOf(report, kD0));
  ASSERT_EQ(FsRootState::kCertifiable, StateOf(report, kD1));
  ASSERT_FALSE(report.HasFailure());
  ASSERT_EQ(vector<string>({kD0, kD1}), report.CertifiableRoots());
}

// Certification: one inconsistent superblock refuses, and nothing is pinned -- not even the root
// whose own evidence was clean. Any single root demanding refusal refuses the whole process.
TEST_F(FsRootPinDecisionTest, OneInconsistentSuperblockRefusesAndPinsNothing) {
  auto inputs = TwoRoots();
  inputs.evidence = {
      Evidence("tablet_aaa", kD0, kD0),
      Evidence("tablet_bbb", kD0, kD1),  // Found under d0, superblock says d1.
      Evidence("tablet_ccc", kD1, kD1)};

  auto report = EvaluateFsRootPins(inputs);
  ASSERT_EQ(FsRootState::kEvidenceConflict, StateOf(report, kD0));
  ASSERT_EQ(FsRootState::kCertifiable, StateOf(report, kD1));
  ASSERT_TRUE(report.HasFailure());

  // Which tablet's metadata is not agreeing, and what it says.
  const auto* v = report.FindRoot(kD0);
  ASSERT_ONLY_NOTNULL(v);
  ASSERT_EQ(1, v->disagreeing_superblocks.size());
  ASSERT_EQ("tablet_bbb", v->disagreeing_superblocks[0].tablet_id);
  ASSERT_EQ(kD1, v->disagreeing_superblocks[0].recorded_data_root);
  ASSERT_EQ(kD0, v->disagreeing_superblocks[0].containing_root);
  ASSERT_EQ(2, v->superblocks_seen);
  ASSERT_EQ(2, v->superblocks_with_evidence);

  const auto message = report.FailureMessage();
  ASSERT_STR_CONTAINS(message, "tablet tablet_bbb: superblock says /mnt/d1, found under /mnt/d0");
  ASSERT_STR_CONTAINS(message, "1 of 2 tablet superblocks under it record a different data root");
  ASSERT_STR_CONTAINS(message, "Not writing a pin");
}

// The zero-tablet row. With no tablets there is no evidence the root is where it belongs, so we
// record nothing and do not fail. Certified on a later start once tablets land.
TEST_F(FsRootPinDecisionTest, ZeroTabletRootStaysUncertified) {
  auto inputs = TwoRoots();
  inputs.evidence = {Evidence("t1", kD0, kD0)};  // Nothing under /mnt/d1 at all.

  auto report = EvaluateFsRootPins(inputs);
  ASSERT_EQ(FsRootState::kCertifiable, StateOf(report, kD0));
  ASSERT_EQ(FsRootState::kUncertified, StateOf(report, kD1));
  ASSERT_FALSE(report.HasFailure());
  ASSERT_EQ(vector<string>({kD0}), report.CertifiableRoots());
}

// A root holding only tombstoned tablets has superblocks but no rocksdb dir, so it has tablets and
// still no evidence. It must land on kUncertified, not kCertifiable: a tombstone proves nothing.
TEST_F(FsRootPinDecisionTest, TombstonedTabletsAreNotEvidence) {
  auto inputs = TwoRoots();
  inputs.evidence = {
      Evidence("t1", kD0, "" /* no rocksdb dir */),
      Evidence("t2", kD1, kD1)};

  auto report = EvaluateFsRootPins(inputs);
  ASSERT_EQ(FsRootState::kUncertified, StateOf(report, kD0));
  ASSERT_FALSE(report.HasFailure());

  const auto* v = report.FindRoot(kD0);
  ASSERT_ONLY_NOTNULL(v);
  ASSERT_EQ(1, v->superblocks_seen);
  ASSERT_EQ(0, v->superblocks_with_evidence);
}

// The FsManager pass runs before tablets are enumerated. Unpinned roots must come back kPending,
// not kUncertified: kUncertified asserts "examined, and no tablets live here", which the report
// states as fact -- and this pass has not looked. A planted pin mismatch is still caught, since
// pin states need no evidence.
TEST_F(FsRootPinDecisionTest, EvidenceNotGatheredLeavesUnpinnedRootsPending) {
  auto inputs = TwoRoots();
  inputs.evidence_complete = FsRootEvidenceComplete::kFalse;
  inputs.pin_files[kD1] = PresentPin(kD1);

  auto report = EvaluateFsRootPins(inputs);
  ASSERT_EQ(FsRootState::kPending, StateOf(report, kD0));
  ASSERT_EQ(FsRootState::kVerified, StateOf(report, kD1));
  ASSERT_FALSE(report.HasFailure());
  ASSERT_TRUE(report.CertifiableRoots().empty());
}

// The same pass still catches a planted mismatch, which is the whole point of running it early:
// before CreateFileSystemRoots can adopt an empty-looking root by writing an instance file into it.
TEST_F(FsRootPinDecisionTest, EvidenceNotGatheredStillCatchesMismatch) {
  auto inputs = TwoRoots();
  inputs.evidence_complete = FsRootEvidenceComplete::kFalse;
  inputs.pin_files[kD0] = PresentPin(kD1);
  inputs.pin_files[kD1] = PresentPin(kD0);

  auto report = EvaluateFsRootPins(inputs);
  ASSERT_TRUE(report.HasFailure());
  ASSERT_STR_CONTAINS(report.FailureMessage(), "appear to be swapped");
}

// A superblock naming another root, under a root whose pin is intact. The pin proves the volume
// did not move, so this is a hand-moved tablet directory, not a swap: warn, do not refuse.
TEST_F(FsRootPinDecisionTest, DisagreeingSuperblockUnderVerifiedRootWarnsOnly) {
  auto inputs = TwoRoots();
  inputs.pin_files[kD0] = PresentPin(kD0);
  inputs.pin_files[kD1] = PresentPin(kD1);
  inputs.evidence = {Evidence("tablet_moved", kD0, kD1), Evidence("t2", kD1, kD1)};

  auto report = EvaluateFsRootPins(inputs);
  ASSERT_EQ(FsRootState::kVerified, StateOf(report, kD0));
  ASSERT_FALSE(report.HasFailure());
  ASSERT_TRUE(report.HasWarnings());
  ASSERT_STR_CONTAINS(report.WarningMessage(), "tablet_moved");
  ASSERT_STR_CONTAINS(report.WarningMessage(), "moved tablet");
}

// A WAL root outside --fs_wal_dirs is reported but never gates: --fs_wal_dirs can be re-pointed.
TEST_F(FsRootPinDecisionTest, UnknownWalRootWarnsButDoesNotRefuse) {
  auto inputs = TwoRoots();
  inputs.evidence = {Evidence("t1", kD0, kD0, "/mnt/wal_gone"), Evidence("t2", kD1, kD1)};

  auto report = EvaluateFsRootPins(inputs);
  ASSERT_EQ(FsRootState::kCertifiable, StateOf(report, kD0));
  ASSERT_FALSE(report.HasFailure());
  ASSERT_TRUE(report.HasWarnings());
  ASSERT_STR_CONTAINS(report.WarningMessage(), "not in --fs_wal_dirs");
}

// A separate WAL disk (IDEA-2667) is a supported layout: the WAL root is legitimately not the data
// root the superblock was found under, and must not be flagged.
TEST_F(FsRootPinDecisionTest, SeparateWalDiskIsNotAnAnomaly) {
  FsRootPinInputs inputs;
  inputs.server_type = kServerType;
  inputs.data_roots = {kD0, kD1};
  inputs.wal_roots = {"/mnt/wal0"};
  inputs.evidence_complete = FsRootEvidenceComplete::kTrue;
  inputs.evidence = {
      Evidence("t1", kD0, kD0, "/mnt/wal0"), Evidence("t2", kD1, kD1, "/mnt/wal0")};

  auto report = EvaluateFsRootPins(inputs);
  ASSERT_FALSE(report.HasFailure());
  ASSERT_FALSE(report.HasWarnings());
}

// A root dropped by the faulty-drive branch is invisible to the survey, so the report must say so
// rather than read as a clean node. (fs_manager.cc drops a root whose instance file fails to read,
// which includes a file that is present but fails its checksum.)
TEST_F(FsRootPinDecisionTest, DroppedRootMakesTheReportIncomplete) {
  auto inputs = TwoRoots();
  inputs.data_roots = {kD0};
  inputs.dropped_roots = {kD1};
  inputs.evidence = {Evidence("t1", kD0, kD0)};

  auto report = EvaluateFsRootPins(inputs);
  ASSERT_FALSE(report.HasFailure());
  ASSERT_TRUE(report.HasWarnings());
  ASSERT_STR_CONTAINS(report.WarningMessage(), "/mnt/d1");
  ASSERT_STR_CONTAINS(report.WarningMessage(), "not a clean node");
}

// A dropped root is also called out inside a failure message, so an operator is not told to remount
// two disks while a third went unexamined.
TEST_F(FsRootPinDecisionTest, DroppedRootIsNamedInTheFailureMessage) {
  auto inputs = TwoRoots();
  inputs.pin_files[kD0] = PresentPin(kD1);
  inputs.dropped_roots = {"/mnt/d9"};

  auto report = EvaluateFsRootPins(inputs);
  ASSERT_TRUE(report.HasFailure());
  ASSERT_STR_CONTAINS(report.FailureMessage(), "This report is incomplete");
  ASSERT_STR_CONTAINS(report.FailureMessage(), "/mnt/d9");
}

// The failure message names a bounded number of tablets and says how many it left out, so a node
// with hundreds of affected tablets still produces a readable FATAL.
TEST_F(FsRootPinDecisionTest, ManyDisagreeingTabletsAreTruncatedWithACount) {
  auto inputs = TwoRoots();
  for (int i = 0; i < 412; ++i) {
    inputs.evidence.push_back(Evidence(Format("tablet_$0", i), kD0, kD1));
  }

  auto report = EvaluateFsRootPins(inputs);
  ASSERT_TRUE(report.HasFailure());

  const auto* v = report.FindRoot(kD0);
  ASSERT_ONLY_NOTNULL(v);
  ASSERT_EQ(412, v->disagreeing_superblocks.size());  // All of them are in the report...

  const auto message = report.FailureMessage();
  ASSERT_STR_CONTAINS(message, "412 of 412 tablet superblocks");
  ASSERT_STR_CONTAINS(message, "(and 402 more");        // ... only 10 are in the message.
  ASSERT_STR_CONTAINS(message, "yb-fs_pin survey");     // ... and it says where to get the rest.
}

TEST_F(FsRootPinDecisionTest, JsonReportCarriesTheOffendingTablets) {
  auto inputs = TwoRoots();
  inputs.evidence = {Evidence("tablet_bbb", kD0, kD1), Evidence("t2", kD1, kD1)};

  const auto json = EvaluateFsRootPins(inputs).ToJson();
  ASSERT_STR_CONTAINS(json, "\"has_failure\": true");
  ASSERT_STR_CONTAINS(json, "kEvidenceConflict");
  ASSERT_STR_CONTAINS(json, "tablet_bbb");
  ASSERT_STR_CONTAINS(json, "\"recorded_data_root\": \"/mnt/d1\"");
}

// ==========================================================================
//  Severity vs action: what is wrong, and what a server does about it
// ==========================================================================

// A pin mismatch requires a pin this feature wrote on an earlier boot, so it cannot fire on a first
// upgrade. It refuses by default.
TEST_F(FsRootPinDecisionTest, PinMismatchRefusesUnderTheDefaultPolicy) {
  auto inputs = TwoRoots();
  inputs.pin_files[kD0] = PresentPin(kD1);
  inputs.pin_files[kD1] = PresentPin(kD0);

  auto report = EvaluateFsRootPins(inputs);
  ASSERT_TRUE(report.HasFailure());
  ASSERT_TRUE(report.ShouldRefuseStartup(DefaultPolicy()));
  ASSERT_TRUE(report.ShouldRefuseStartup(StrictPolicy()));
  ASSERT_FALSE(report.ShouldRefuseStartup(WarnOnlyPolicy()));
}

// A superblock conflict is different in kind: it fires purely from pre-existing on-disk state, so
// it is the only check that can condemn a node on its first boot after a binary upgrade -- with no
// pin this feature ever wrote involved. It is a failure, and it blocks the offline pre-upgrade
// gate, but by default it does not stop a server that is already running on that state.
TEST_F(FsRootPinDecisionTest, SuperblockConflictIsAFailureButDoesNotRefuseByDefault) {
  auto inputs = TwoRoots();
  inputs.evidence = {Evidence("tablet_moved", kD0, kD1), Evidence("t2", kD1, kD1)};

  auto report = EvaluateFsRootPins(inputs);
  ASSERT_EQ(FsRootState::kEvidenceConflict, StateOf(report, kD0));
  ASSERT_TRUE(report.HasFailure());
  ASSERT_FALSE(report.ShouldRefuseStartup(DefaultPolicy()));
  // ... but it is still exit 1 for the pre-upgrade check, where blocking is cheap.
  ASSERT_EQ(kFsPinExitFailure, FsPinExitCodeForReport(report));
  // ... and the operator who wants the strong behavior can have it.
  ASSERT_TRUE(report.ShouldRefuseStartup(StrictPolicy()));
}

// Whatever the policy, the conflicted root is never certified: a pin must never record a layout the
// evidence contradicts.
TEST_F(FsRootPinDecisionTest, SuperblockConflictIsNeverCertifiedUnderAnyPolicy) {
  auto inputs = TwoRoots();
  inputs.evidence = {Evidence("tablet_moved", kD0, kD1), Evidence("t2", kD1, kD1)};

  auto report = EvaluateFsRootPins(inputs);
  const auto to_pin = report.CertifiableRoots();
  ASSERT_EQ(vector<string>({kD1}), to_pin) << "the conflicted root must not be pinnable";
}

// The exit code is derived from severity, not from what a server would do, so a state the server is
// configured to tolerate still blocks an upgrade.
TEST_F(FsRootPinDecisionTest, ExitCodeContract) {
  {
    auto inputs = TwoRoots();
    inputs.evidence = {Evidence("t1", kD0, kD0), Evidence("t2", kD1, kD1)};
    ASSERT_EQ(kFsPinExitOk, FsPinExitCodeForReport(EvaluateFsRootPins(inputs)));
  }
  {
    auto inputs = TwoRoots();
    inputs.pin_files[kD0] = PresentPin(kD0);
    inputs.pin_files[kD1] = PresentPin(kD1);
    inputs.evidence = {Evidence("moved", kD0, kD1), Evidence("t2", kD1, kD1)};
    ASSERT_EQ(kFsPinExitWarning, FsPinExitCodeForReport(EvaluateFsRootPins(inputs)));
  }
  {
    auto inputs = TwoRoots();
    inputs.pin_files[kD0] = PresentPin(kD1);
    ASSERT_EQ(kFsPinExitFailure, FsPinExitCodeForReport(EvaluateFsRootPins(inputs)));
  }
}

// An unreadable pin file is a device fault, not a corrupt record. The established policy for the
// far more important instance file is to drop the root and keep running, so taking a node down
// over one unreadable 100-byte file is backwards. It is a warning: not trusted, not replaced, and
// above all not certified over.
TEST_F(FsRootPinDecisionTest, PinIoErrorWarnsAndBlocksCertification) {
  auto inputs = TwoRoots();
  inputs.pin_files[kD0] = IoErrorPin("Input/output error (system error 5)");
  inputs.evidence = {Evidence("t1", kD0, kD0), Evidence("t2", kD1, kD1)};

  auto report = EvaluateFsRootPins(inputs);
  ASSERT_EQ(FsRootState::kPinIoError, StateOf(report, kD0));
  ASSERT_FALSE(report.HasFailure());
  ASSERT_TRUE(report.HasWarnings());
  ASSERT_FALSE(report.ShouldRefuseStartup(StrictPolicy()));
  // The crucial part: clean evidence must not overwrite a pin we merely failed to read.
  ASSERT_EQ(vector<string>({kD1}), report.CertifiableRoots());
  ASSERT_EQ(FsRootRepinDecision::kUnprovable, RepinOf(report, kD0));
  ASSERT_STR_CONTAINS(report.WarningMessage(), "Input/output error");
}

// An unreadable pin with disagreeing superblocks under it is consistent with a volume that really
// moved, so the warning must not claim the root is pinned here or that the volume has not moved --
// neither is known. The disagreeing tablets are still reported, with honest wording.
TEST_F(FsRootPinDecisionTest, PinIoErrorWithDisagreeingSuperblocksDoesNotClaimPinnedHere) {
  auto inputs = TwoRoots();
  inputs.pin_files[kD0] = IoErrorPin("Input/output error (system error 5)");
  inputs.evidence = {Evidence("t1", kD0, kD1), Evidence("t2", kD1, kD1)};

  auto report = EvaluateFsRootPins(inputs);
  ASSERT_EQ(FsRootState::kPinIoError, StateOf(report, kD0));
  const auto message = report.WarningMessage();
  ASSERT_STR_NOT_CONTAINS(message, "is pinned here");
  ASSERT_STR_NOT_CONTAINS(message, "has not moved");
  ASSERT_STR_CONTAINS(message, "t1");
  ASSERT_STR_CONTAINS(message, "cannot be verified");
}

// A data volume that failed to mount presents as an empty directory on the root filesystem, which
// would otherwise pass the pre-upgrade check as a root that simply has no tablets.
TEST_F(FsRootPinDecisionTest, UnformattedRootWarnsWhenSiblingsAreFormatted) {
  auto inputs = TwoRoots();
  inputs.unformatted_roots = {kD0};
  inputs.evidence = {Evidence("t1", kD1, kD1)};

  auto report = EvaluateFsRootPins(inputs);
  ASSERT_EQ(FsRootState::kUnformatted, StateOf(report, kD0));
  ASSERT_FALSE(report.HasFailure());
  ASSERT_EQ(kFsPinExitWarning, FsPinExitCodeForReport(report));
  ASSERT_STR_CONTAINS(report.WarningMessage(), "did not mount");
  // The unformatted root is never certified -- there is no instance file, so there is nothing to
  // certify -- while its healthy sibling still is.
  ASSERT_EQ(vector<string>({kD1}), report.CertifiableRoots());
}

// A mismatched pin must not be downgraded by a missing instance file. The volume mounted at d0
// was certified at d1 (its pin travels with it) and has lost its instance file -- a partial wipe.
// The kUnformatted short-circuit deliberately requires the pin to be ABSENT, so the mismatch is
// still reported as the failure it is; d1 is a separate, healthy root.
TEST_F(FsRootPinDecisionTest, UnformattedDoesNotMaskAMismatchedPin) {
  auto inputs = TwoRoots();
  inputs.unformatted_roots = {kD0};
  inputs.pin_files[kD0] = PresentPin(kD1);
  inputs.evidence = {Evidence("t1", kD1, kD1)};

  auto report = EvaluateFsRootPins(inputs);
  ASSERT_EQ(FsRootState::kMismatched, StateOf(report, kD0));
  ASSERT_TRUE(report.HasFailure());
  ASSERT_TRUE(report.ShouldRefuseStartup(DefaultPolicy()));
}

// On a genuinely fresh node every root is unformatted, which is normal and must stay silent.
TEST_F(FsRootPinDecisionTest, AllRootsUnformattedIsAFreshNodeNotAnAnomaly) {
  auto inputs = TwoRoots();
  inputs.unformatted_roots = {kD0, kD1};

  auto report = EvaluateFsRootPins(inputs);
  ASSERT_EQ(FsRootState::kUncertified, StateOf(report, kD0));
  ASSERT_FALSE(report.HasFailure());
  ASSERT_FALSE(report.HasWarnings());
  ASSERT_EQ(kFsPinExitOk, FsPinExitCodeForReport(report));
}

// A root whose superblocks all fail to parse has tablets and no evidence. It must not read as a
// root with nothing on it, which is the second way a broken node could pass the gate.
TEST_F(FsRootPinDecisionTest, UnreadableSuperblocksAreReportedNotSilentlyDropped) {
  auto inputs = TwoRoots();
  inputs.evidence = {
      UnreadableSuperblock("t1", kD0, "Corruption: bad checksum"),
      UnreadableSuperblock("t2", kD0, "Corruption: bad checksum"),
      Evidence("t3", kD1, kD1)};

  auto report = EvaluateFsRootPins(inputs);
  ASSERT_EQ(FsRootState::kUncertified, StateOf(report, kD0));
  ASSERT_TRUE(report.HasWarnings());
  ASSERT_EQ(kFsPinExitWarning, FsPinExitCodeForReport(report));

  const auto* v = report.FindRoot(kD0);
  ASSERT_ONLY_NOTNULL(v);
  ASSERT_EQ(2, v->superblocks_seen);
  ASSERT_EQ(0, v->superblocks_with_evidence);
  ASSERT_EQ(2, v->unreadable_superblocks.size());
  ASSERT_STR_CONTAINS(report.WarningMessage(), "bad checksum");
  ASSERT_STR_CONTAINS(report.ToJson(), "unreadable_superblocks");
}

// Side anomalies must survive a failing verdict: the WAL problem should not vanish from the output
// exactly when the node is most broken.
TEST_F(FsRootPinDecisionTest, WalAnomalyStillReportedOnAFailingRoot) {
  auto inputs = TwoRoots();
  inputs.pin_files[kD0] = PresentPin(kD1);  // Mismatched: a failure.
  inputs.evidence = {Evidence("t1", kD0, kD0, "/mnt/wal_gone")};

  auto report = EvaluateFsRootPins(inputs);
  ASSERT_TRUE(report.HasFailure());
  ASSERT_STR_CONTAINS(report.FailureMessage(), "not in --fs_wal_dirs");
}

// ==========================================================================
//  Repair
// ==========================================================================

// The only safe repair: the pin is stale and every superblock under the root says the data belongs
// here. Rewriting the record is provably correct.
TEST_F(FsRootPinDecisionTest, RepinIsSafeWhenTheEvidenceCertifiesTheCurrentMount) {
  auto inputs = TwoRoots();
  inputs.pin_files[kD0] = PresentPin("/data/old_layout");
  inputs.evidence = {Evidence("t1", kD0, kD0), Evidence("t2", kD0, kD0)};

  auto report = EvaluateFsRootPins(inputs);
  ASSERT_EQ(FsRootState::kMismatched, StateOf(report, kD0));
  ASSERT_EQ(FsRootRepinDecision::kSafe, RepinOf(report, kD0));
  // And the message says so, so an operator is not left to work it out.
  ASSERT_STR_CONTAINS(report.FailureMessage(), "it is the pin that is stale");
  ASSERT_STR_CONTAINS(report.FailureMessage(), "yb-fs_pin repin");
}

// The unsafe one, and the reason repin cannot simply be `--force`: the superblocks corroborate the
// old pin, so the volume really moved and its tablets will not find their data here whatever the
// pin says. This is the actual swap, and the only fix is a remount.
TEST_F(FsRootPinDecisionTest, RepinRefusesWhenTheVolumeActuallyMoved) {
  auto inputs = TwoRoots();
  inputs.pin_files[kD0] = PresentPin(kD1);
  inputs.pin_files[kD1] = PresentPin(kD0);
  inputs.evidence = {Evidence("t1", kD0, kD1), Evidence("t2", kD1, kD0)};

  auto report = EvaluateFsRootPins(inputs);
  ASSERT_EQ(FsRootRepinDecision::kUnsafeVolumeMoved, RepinOf(report, kD0));
  ASSERT_EQ(FsRootRepinDecision::kUnsafeVolumeMoved, RepinOf(report, kD1));
  // No repair is offered in the message for a real swap.
  ASSERT_STR_NOT_CONTAINS(report.FailureMessage(), "yb-fs_pin repin");
}

// A mismatched pin on a root with no tablets cannot be adjudicated either way.
TEST_F(FsRootPinDecisionTest, RepinIsUnprovableWithoutEvidence) {
  auto inputs = TwoRoots();
  inputs.pin_files[kD0] = PresentPin(kD1);

  auto report = EvaluateFsRootPins(inputs);
  ASSERT_EQ(FsRootRepinDecision::kUnprovable, RepinOf(report, kD0));
}

// A superblock conflict has no pin to repair; the problem is where the data is, not what we wrote
// down about it.
TEST_F(FsRootPinDecisionTest, RepinCannotHelpASuperblockConflict) {
  auto inputs = TwoRoots();
  inputs.evidence = {Evidence("moved", kD0, kD1), Evidence("t2", kD1, kD1)};

  auto report = EvaluateFsRootPins(inputs);
  ASSERT_EQ(FsRootRepinDecision::kUnsafeVolumeMoved, RepinOf(report, kD0));
  ASSERT_EQ(FsRootRepinDecision::kNotNeeded, RepinOf(report, kD1));
}

// A healthy node offers nothing to repair.
TEST_F(FsRootPinDecisionTest, RepinIsNotNeededOnAHealthyNode) {
  auto inputs = TwoRoots();
  inputs.pin_files[kD0] = PresentPin(kD0);
  inputs.pin_files[kD1] = PresentPin(kD1);
  inputs.evidence = {Evidence("t1", kD0, kD0), Evidence("t2", kD1, kD1)};

  auto report = EvaluateFsRootPins(inputs);
  ASSERT_EQ(FsRootRepinDecision::kNotNeeded, RepinOf(report, kD0));
  ASSERT_EQ(FsRootRepinDecision::kNotNeeded, RepinOf(report, kD1));
}

// The offline command must not tell the reader to run the command they are already running.
TEST_F(FsRootPinDecisionTest, OfflineAudienceOmitsTheSurveyHint) {
  auto inputs = TwoRoots();
  inputs.pin_files[kD0] = PresentPin(kD1);

  auto report = EvaluateFsRootPins(inputs);
  ASSERT_STR_CONTAINS(
      report.FailureMessage(FsRootPinReport::Audience::kServerLog), "yb-fs_pin survey");
  ASSERT_STR_NOT_CONTAINS(
      report.FailureMessage(FsRootPinReport::Audience::kOfflineCommand), "yb-fs_pin survey");
}

// The message reaches the log behind up to two RETURN_NOT_OK_PREPEND prefixes, so the headline has
// to start its own line.
TEST_F(FsRootPinDecisionTest, FailureMessageStartsOnItsOwnLine) {
  auto inputs = TwoRoots();
  inputs.pin_files[kD0] = PresentPin(kD1);

  const auto message = EvaluateFsRootPins(inputs).FailureMessage();
  ASSERT_FALSE(message.empty());
  ASSERT_EQ('\n', message[0]) << message.substr(0, 40);
}

// ==========================================================================
//  The pin file on disk
// ==========================================================================

class FsRootPinFileTest : public YBTest {
 public:
  string PinPath(const string& name) { return GetTestPath(name); }
};

TEST_F(FsRootPinFileTest, RoundTrip) {
  FsRootPin pin;
  pin.mount_point = "/mnt/d0";
  pin.pinned_at = "2026-07-29T18:22:04Z";
  pin.filesystem_uuid = "b3f2a41c-9d0e-4c77-8a15-2e6f0c9d1b44";

  const auto json = pin.ToJson();
  // Human readable is a requirement, not an accident: an operator reads this with cat.
  ASSERT_STR_CONTAINS(json, "\"mount_point\": \"/mnt/d0\"");

  auto parsed = ASSERT_RESULT(FsRootPin::ParseJson(json));
  ASSERT_EQ(pin.mount_point, parsed.mount_point);
  ASSERT_EQ(pin.pinned_at, parsed.pinned_at);
  ASSERT_EQ(pin.filesystem_uuid, parsed.filesystem_uuid);
}

TEST_F(FsRootPinFileTest, FsUuidIsOptional) {
  FsRootPin pin;
  pin.mount_point = "/mnt/d0";
  pin.pinned_at = "2026-07-29T18:22:04Z";

  ASSERT_STR_NOT_CONTAINS(pin.ToJson(), "filesystem_uuid");
  auto parsed = ASSERT_RESULT(FsRootPin::ParseJson(pin.ToJson()));
  ASSERT_TRUE(parsed.filesystem_uuid.empty());
}

// Unknown fields must not break an older binary reading a newer pin.
TEST_F(FsRootPinFileTest, UnknownFieldsAreIgnored) {
  auto parsed = ASSERT_RESULT(FsRootPin::ParseJson(
      R"({"mount_point": "/mnt/d0", "pinned_at": "2026-07-29T18:22:04Z", "future": 7})"));
  ASSERT_EQ("/mnt/d0", parsed.mount_point);
}

TEST_F(FsRootPinFileTest, MalformedPinsAreRejected) {
  const std::pair<const char*, const char*> kCases[] = {
      {"", "not valid JSON"},
      {"   ", "not valid JSON"},
      // Truncated mid-write, the case atomic writes exist to prevent.
      {R"({"mount_point": "/mnt/d)", "not valid JSON"},
      {R"({"mount_point": "/mnt/d0",)", "not valid JSON"},
      {"\xff\xfe garbage", "not valid JSON"},
      {"[1, 2, 3]", "not a JSON object"},
      {R"("just a string")", "not a JSON object"},
      {"{}", "\"mount_point\" is missing"},
      {R"({"mount_point": ""})", "\"mount_point\" is missing"},
      {R"({"mount_point": 42})", "\"mount_point\" is missing"},
      {R"({"mount_point": "relative/path"})", "not an absolute path"},
  };
  for (const auto& [json, expected] : kCases) {
    auto result = FsRootPin::ParseJson(json);
    ASSERT_NOK(result) << "should have been rejected: " << json;
    ASSERT_STR_CONTAINS(result.status().message().ToBuffer(), expected);
  }
}

TEST_F(FsRootPinFileTest, ReadOfAMissingFileIsAbsentNotUnreadable) {
  auto f = ReadFsRootPinFile(env_.get(), PinPath("no_such_pin.json"));
  ASSERT_EQ(FsRootPinFileState::kAbsent, f.state);
  ASSERT_TRUE(f.error.empty());
}

// The distinction the whole design turns on: a file that exists but cannot be understood is
// kUnparseable (which the server refuses on), never kAbsent (which it would re-certify over).
TEST_F(FsRootPinFileTest, ReadOfATruncatedFileIsUnreadable) {
  const auto path = PinPath("torn_pin.json");
  ASSERT_OK(WriteStringToFile(env_.get(), Slice(R"({"mount_point": "/mnt/d)"), path));

  auto f = ReadFsRootPinFile(env_.get(), path);
  ASSERT_EQ(FsRootPinFileState::kUnparseable, f.state);
  ASSERT_STR_CONTAINS(f.error, "not valid JSON");
}

TEST_F(FsRootPinFileTest, ReadOfAnEmptyFileIsUnreadable) {
  const auto path = PinPath("empty_pin.json");
  ASSERT_OK(WriteStringToFile(env_.get(), Slice(""), path));

  auto f = ReadFsRootPinFile(env_.get(), path);
  ASSERT_EQ(FsRootPinFileState::kUnparseable, f.state);
}

TEST_F(FsRootPinFileTest, WriteThenRead) {
  const auto path = PinPath("pin.json");
  const auto pin = FsRootPin::ForMountPoint("/mnt/d0");
  ASSERT_OK(WriteFsRootPinFile(env_.get(), path, pin));

  auto f = ReadFsRootPinFile(env_.get(), path);
  ASSERT_EQ(FsRootPinFileState::kPresent, f.state);
  ASSERT_EQ("/mnt/d0", f.pin.mount_point);
  ASSERT_FALSE(f.pin.pinned_at.empty());
}

// Re-pinning after a deliberate relayout overwrites in place, and leaves no temp file behind.
TEST_F(FsRootPinFileTest, WriteIsIdempotentAndLeavesNoTempFile) {
  const auto dir = GetTestPath("pin_dir");
  ASSERT_OK(env_->CreateDir(dir));
  const auto path = JoinPathSegments(dir, "fs-root-pin.json");

  FsRootPin pin;
  pin.mount_point = "/mnt/d0";
  pin.pinned_at = "2026-07-29T18:22:04Z";
  ASSERT_OK(WriteFsRootPinFile(env_.get(), path, pin));

  pin.mount_point = "/mnt/d1";
  ASSERT_OK(WriteFsRootPinFile(env_.get(), path, pin));

  auto f = ReadFsRootPinFile(env_.get(), path);
  ASSERT_EQ(FsRootPinFileState::kPresent, f.state);
  ASSERT_EQ("/mnt/d1", f.pin.mount_point);

  auto children = ASSERT_RESULT(env_->GetChildren(dir, ExcludeDots::kTrue));
  ASSERT_EQ(1, children.size()) << "temp file left behind: " << AsString(children);
  ASSERT_EQ("fs-root-pin.json", children[0]);
}

TEST_F(FsRootPinFileTest, PinnedAtIsIso8601Utc) {
  const auto now = FsRootPin::ForMountPoint("/mnt/d0").pinned_at;
  ASSERT_EQ(20, now.size()) << now;    // YYYY-MM-DDTHH:MM:SSZ
  ASSERT_EQ('T', now[10]) << now;
  ASSERT_EQ('Z', now[19]) << now;
}

// The path arithmetic every comparison depends on: data_root_dir() ends in ".../data",
// wal_root_dir() in ".../wals", and GetTabletPath() in ".../<server_type>". All three must
// normalize to the same fs root, or every verdict is nonsense.
TEST_F(FsRootPinFileTest, FsRootOfYbDataPath) {
  ASSERT_EQ("/mnt/d0", FsRootOfYbDataPath("/mnt/d0/yb-data/tserver/data"));
  ASSERT_EQ("/mnt/d0", FsRootOfYbDataPath("/mnt/d0/yb-data/tserver/wals"));
  ASSERT_EQ("/mnt/d0", FsRootOfYbDataPath("/mnt/d0/yb-data/tserver"));
  ASSERT_EQ("/mnt/d0", FsRootOfYbDataPath("/mnt/d0/yb-data/master/data"));
  ASSERT_EQ(
      "/mnt/d0",
      FsRootOfYbDataPath("/mnt/d0/yb-data/tserver/data/rocksdb/table-abc/tablet-def"));
  ASSERT_EQ("/", FsRootOfYbDataPath("/yb-data/tserver/data"));

  // No yb-data component: no answer. Must not guess.
  ASSERT_EQ("", FsRootOfYbDataPath(""));
  ASSERT_EQ("", FsRootOfYbDataPath("/mnt/d0"));
  ASSERT_EQ("", FsRootOfYbDataPath("/"));
}

TEST_F(FsRootPinFileTest, PinPathLayout) {
  ASSERT_EQ("/mnt/d0/yb-data/tserver/fs-root-pin.json", FsRootPinPath("/mnt/d0", "tserver"));
  ASSERT_EQ("/mnt/d0/yb-data/master/fs-root-pin.json", FsRootPinPath("/mnt/d0", "master"));
}

// ==========================================================================
//  FsManager, on real directories
// ==========================================================================

class FsRootPinManagerTest : public YBTest {
 public:
  void SetUp() override {
    YBTest::SetUp();
    root_a_ = GetTestPath("d0");
    root_b_ = GetTestPath("d1");
    ASSERT_OK(env_->CreateDir(root_a_));
    ASSERT_OK(env_->CreateDir(root_b_));
  }

  std::unique_ptr<FsManager> MakeFsManager() {
    FsManagerOpts opts;
    opts.data_paths = {root_a_, root_b_};
    opts.wal_paths = {root_a_, root_b_};
    opts.server_type = kServerType;
    return std::make_unique<FsManager>(env_.get(), opts);
  }

  // A formatted, opened FsManager over the two roots.
  std::unique_ptr<FsManager> OpenFsManager() {
    auto fs = MakeFsManager();
    EXPECT_OK(fs->CreateInitialFileSystemLayout());
    EXPECT_OK(fs->CheckAndOpenFileSystemRoots());
    return fs;
  }

  string PinPathFor(const string& root) { return FsRootPinPath(root, kServerType); }

  // A placeholder superblock file, enough for the directory listing that the early pass counts.
  void PlantTabletMeta(const string& root, const string& tablet_id) {
    const auto dir = FsManager::GetRaftGroupMetadataDir(
        GetServerTypeDataPath(root, kServerType));
    std::unique_ptr<WritableFile> writer;
    ASSERT_OK(env_->NewWritableFile(JoinPathSegments(dir, tablet_id), &writer));
  }

  void PlantPin(const string& root, const string& mount_point) {
    ASSERT_OK(WriteFsRootPinFile(
        env_.get(), PinPathFor(root), FsRootPin::ForMountPoint(mount_point)));
  }

  TabletSuperblockEvidence TabletOn(
      const string& found_under, const string& recorded, const string& tablet_id) {
    return Evidence(tablet_id, found_under, recorded, found_under);
  }

  string root_a_;
  string root_b_;
};

// A fresh node has no pins and no tablets. Nothing is written, nothing fails, and the roots stay
// uncertified until tablets land.
TEST_F(FsRootPinManagerTest, FreshNodeWritesNoPins) {
  auto fs = OpenFsManager();

  ASSERT_FALSE(env_->FileExists(PinPathFor(root_a_)));
  ASSERT_FALSE(env_->FileExists(PinPathFor(root_b_)));

  ASSERT_OK(fs->CertifyDataRoots({}));
  ASSERT_FALSE(env_->FileExists(PinPathFor(root_a_)));

  auto report = ASSERT_RESULT(fs->SurveyDataRoots({}, FsRootEvidenceComplete::kTrue));
  ASSERT_EQ(FsRootState::kUncertified, StateOf(report, root_a_));
}

// Upgrade of an existing multi-root cluster: pins appear on the first restart that has tablets,
// and nothing else changes.
TEST_F(FsRootPinManagerTest, PinsAppearOnFirstRestartWithTablets) {
  auto fs = OpenFsManager();

  ASSERT_OK(fs->CertifyDataRoots(
      {TabletOn(root_a_, root_a_, "t1"), TabletOn(root_b_, root_b_, "t2")}));

  ASSERT_TRUE(env_->FileExists(PinPathFor(root_a_)));
  ASSERT_TRUE(env_->FileExists(PinPathFor(root_b_)));

  auto pin_a = ReadFsRootPinFile(env_.get(), PinPathFor(root_a_));
  ASSERT_EQ(FsRootPinFileState::kPresent, pin_a.state);
  ASSERT_EQ(root_a_, pin_a.pin.mount_point);
  ASSERT_FALSE(pin_a.pin.pinned_at.empty());

  // A second start over the same layout is a no-op that verifies.
  auto fs2 = MakeFsManager();
  ASSERT_OK(fs2->CheckAndOpenFileSystemRoots());
  auto report = ASSERT_RESULT(fs2->SurveyDataRoots({}, FsRootEvidenceComplete::kFalse));
  ASSERT_EQ(FsRootState::kVerified, StateOf(report, root_a_));
  ASSERT_EQ(FsRootState::kVerified, StateOf(report, root_b_));
  ASSERT_FALSE(report.HasFailure());
}

// End to end: pins planted, then the two volumes swapped. CheckAndOpenFileSystemRoots must refuse,
// before anything else runs and before any directory is created.
TEST_F(FsRootPinManagerTest, SwappedMountsRefuseAtCheckAndOpen) {
  auto fs = OpenFsManager();
  PlantPin(root_a_, root_b_);  // The volume mounted at d0 was certified at d1.
  PlantPin(root_b_, root_a_);
  // Two tablets on d0, one on d1, so the message can say how much data is on each root.
  PlantTabletMeta(root_a_, "tablet_one");
  PlantTabletMeta(root_a_, "tablet_two");
  PlantTabletMeta(root_b_, "tablet_three");

  auto fs2 = MakeFsManager();
  const auto s = fs2->CheckAndOpenFileSystemRoots();
  ASSERT_NOK(s);
  ASSERT_TRUE(s.IsIllegalState()) << s;

  // The FATAL body an operator will read. It must name both roots, both pins, and how many
  // tablets are on each -- this pass runs before superblocks are parsed, so a count is all it has.
  const auto message = s.message().ToBuffer();
  ASSERT_STR_CONTAINS(message, "Data volumes are not mounted where they were");
  ASSERT_STR_CONTAINS(message, root_a_ + " holds the volume pinned at " + root_b_);
  ASSERT_STR_CONTAINS(message, root_b_ + " holds the volume pinned at " + root_a_);
  ASSERT_STR_CONTAINS(message, FsRootPinPath(root_a_, kServerType));
  ASSERT_STR_CONTAINS(message, "2 tablet superblocks under this root");
  ASSERT_STR_CONTAINS(message, "1 tablet superblock under this root");
  ASSERT_STR_CONTAINS(message, "appear to be swapped");
  ASSERT_STR_CONTAINS(message, "No data has been lost");
  ASSERT_STR_CONTAINS(message, "Do not delete anything");
  ASSERT_STR_CONTAINS(message, "Refusing to start");
}

// The refusal must not be a NotFound, which RpcAndWebServerBase::Init treats as "no FS layout yet"
// and answers by creating a fresh one -- the single worst response to a swap.
TEST_F(FsRootPinManagerTest, RefusalIsNotMistakenForAMissingLayout) {
  auto fs = OpenFsManager();
  PlantPin(root_a_, root_b_);
  PlantPin(root_b_, root_a_);

  auto fs2 = MakeFsManager();
  const auto s = fs2->CheckAndOpenFileSystemRoots();
  ASSERT_NOK(s);
  ASSERT_FALSE(s.IsNotFound()) << s;
  ASSERT_FALSE(fs2->HasAnyLockFiles());
}

TEST_F(FsRootPinManagerTest, UnparseablePinRefusesAtCheckAndOpen) {
  auto fs = OpenFsManager();
  ASSERT_OK(WriteStringToFile(
      env_.get(), Slice(R"({"mount_point": )"), PinPathFor(root_a_)));

  auto fs2 = MakeFsManager();
  const auto s = fs2->CheckAndOpenFileSystemRoots();
  ASSERT_NOK(s);
  ASSERT_STR_CONTAINS(s.message().ToBuffer(), "pin file whose contents are unusable");
  ASSERT_STR_CONTAINS(s.message().ToBuffer(), PinPathFor(root_a_));
}

// One inconsistent superblock writes nothing -- neither for the conflicted root nor for the clean
// one -- but by default it does not refuse: this state fires purely from pre-existing superblocks,
// so it is the one check that could condemn a node on its first boot after an upgrade.
TEST_F(FsRootPinManagerTest, InconsistentEvidencePinsNothingAndDoesNotRefuseByDefault) {
  auto fs = OpenFsManager();

  ASSERT_OK(fs->CertifyDataRoots({
      TabletOn(root_a_, root_a_, "tablet_ok"),
      TabletOn(root_a_, root_b_, "tablet_wrong"),
      TabletOn(root_b_, root_b_, "tablet_other")}));

  ASSERT_FALSE(env_->FileExists(PinPathFor(root_a_)));
  ASSERT_FALSE(env_->FileExists(PinPathFor(root_b_)))
      << "a report with a failure in it must not add new state anywhere";
}

// ... and with the strict flag set, the same state refuses, naming the tablet.
TEST_F(FsRootPinManagerTest, InconsistentEvidenceRefusesUnderTheStrictFlag) {
  auto fs = OpenFsManager();

  google::FlagSaver flag_saver;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_fs_root_pin_refuse_on_superblock_conflict) = true;

  const auto s = fs->CertifyDataRoots({
      TabletOn(root_a_, root_a_, "tablet_ok"),
      TabletOn(root_a_, root_b_, "tablet_wrong"),
      TabletOn(root_b_, root_b_, "tablet_other")});
  ASSERT_NOK(s);
  ASSERT_TRUE(s.IsIllegalState()) << s;
  ASSERT_STR_CONTAINS(s.message().ToBuffer(), "tablet tablet_wrong: superblock says " + root_b_);

  ASSERT_FALSE(env_->FileExists(PinPathFor(root_a_)));
  ASSERT_FALSE(env_->FileExists(PinPathFor(root_b_)));
}

// Warn mode: the same diagnosis, no refusal, and no new pin over a node we have just diagnosed as
// wrong.
TEST_F(FsRootPinManagerTest, WarnModeDoesNotRefuseAndDoesNotPin) {
  auto fs = OpenFsManager();
  PlantPin(root_a_, root_b_);
  PlantPin(root_b_, root_a_);

  google::FlagSaver flag_saver;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_fs_root_pin_enforce) = false;

  auto fs2 = MakeFsManager();
  ASSERT_OK(fs2->CheckAndOpenFileSystemRoots());
  ASSERT_OK(fs2->CertifyDataRoots({TabletOn(root_a_, root_b_, "t1")}));

  // The pins are still the planted, mismatched ones: warn mode wrote nothing.
  auto pin_a = ReadFsRootPinFile(env_.get(), PinPathFor(root_a_));
  ASSERT_EQ(root_b_, pin_a.pin.mount_point);
}

// But on a healthy node, warn mode still certifies. Refusing and writing are separate decisions:
// a release branch that never accumulated pins would be stuck forever on the weaker superblock
// check, which is the signal this feature exists to improve on.
TEST_F(FsRootPinManagerTest, WarnModeStillPinsAHealthyNode) {
  auto fs = OpenFsManager();

  google::FlagSaver flag_saver;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_fs_root_pin_enforce) = false;

  ASSERT_OK(fs->CertifyDataRoots(
      {TabletOn(root_a_, root_a_, "t1"), TabletOn(root_b_, root_b_, "t2")}));
  ASSERT_TRUE(env_->FileExists(PinPathFor(root_a_)));
  ASSERT_TRUE(env_->FileExists(PinPathFor(root_b_)));
}

// The write can be turned off on its own, for a deployment that wants detection without new state.
TEST_F(FsRootPinManagerTest, WriteFlagOffCertifiesNothing) {
  auto fs = OpenFsManager();

  google::FlagSaver flag_saver;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_fs_root_pin_write) = false;

  ASSERT_OK(fs->CertifyDataRoots(
      {TabletOn(root_a_, root_a_, "t1"), TabletOn(root_b_, root_b_, "t2")}));
  ASSERT_FALSE(env_->FileExists(PinPathFor(root_a_)));
}

// A read-only FsManager (the survey) must never write, even when the evidence certifies.
TEST_F(FsRootPinManagerTest, ReadOnlyManagerNeverWritesPins) {
  auto fs = OpenFsManager();

  FsManagerOpts opts;
  opts.data_paths = {root_a_, root_b_};
  opts.wal_paths = {root_a_, root_b_};
  opts.server_type = kServerType;
  opts.read_only = true;
  FsManager read_only(env_.get(), opts);

  ASSERT_OK(read_only.CertifyDataRoots(
      {TabletOn(root_a_, root_a_, "t1"), TabletOn(root_b_, root_b_, "t2")}));
  ASSERT_FALSE(env_->FileExists(PinPathFor(root_a_)));
}

// Wiping the layout must take the pin with it. Two reasons: the pin certifies a layout that no
// longer exists, and CreateInitialFileSystemLayout() refuses to run on a root whose server
// directory is not empty -- so a leftover pin would break the delete-then-recreate cycle that
// master shell mode and the lock-file recovery path both depend on.
TEST_F(FsRootPinManagerTest, DeleteFileSystemLayoutRemovesThePin) {
  auto fs = OpenFsManager();
  ASSERT_OK(fs->CertifyDataRoots(
      {TabletOn(root_a_, root_a_, "t1"), TabletOn(root_b_, root_b_, "t2")}));
  ASSERT_TRUE(env_->FileExists(PinPathFor(root_a_)));

  ASSERT_OK(fs->DeleteFileSystemLayout());
  ASSERT_FALSE(env_->FileExists(PinPathFor(root_a_)));
  ASSERT_FALSE(env_->FileExists(PinPathFor(root_b_)));

  // And the root is now empty enough to be reformatted.
  auto fs2 = MakeFsManager();
  ASSERT_OK(fs2->CreateInitialFileSystemLayout());
  ASSERT_OK(fs2->CheckAndOpenFileSystemRoots());
}

// ListTabletIdsByRoot is what the offline survey walks. It must attribute each superblock to the
// root it was actually found under, tolerate a root with no tablet-meta directory, and not delete
// or rewrite anything on the way.
TEST_F(FsRootPinManagerTest, ListTabletIdsByRootAttributesAndTolerates) {
  auto fs = OpenFsManager();

  const auto meta_dir_a = FsManager::GetRaftGroupMetadataDir(
      GetServerTypeDataPath(root_a_, kServerType));
  std::unique_ptr<WritableFile> writer;
  ASSERT_OK(env_->NewWritableFile(JoinPathSegments(meta_dir_a, "tablet_one"), &writer));
  ASSERT_OK(env_->NewWritableFile(JoinPathSegments(meta_dir_a, "tablet_two"), &writer));
  ASSERT_OK(env_->NewWritableFile(JoinPathSegments(meta_dir_a, "scratch.tmp"), &writer));
  ASSERT_OK(env_->NewWritableFile(JoinPathSegments(meta_dir_a, ".hidden"), &writer));

  // Root b has no tablet-meta directory at all.
  ASSERT_OK(env_->DeleteRecursively(
      FsManager::GetRaftGroupMetadataDir(GetServerTypeDataPath(root_b_, kServerType))));

  auto by_root = ASSERT_RESULT(fs->ListTabletIdsByRoot());
  ASSERT_EQ(2, by_root.size());
  ASSERT_EQ(vector<string>({"tablet_one", "tablet_two"}), by_root[root_a_]);
  ASSERT_TRUE(by_root[root_b_].empty());

  // Read-only: the temp file is still there, unlike ListTabletIds which deletes it.
  ASSERT_TRUE(env_->FileExists(JoinPathSegments(meta_dir_a, "scratch.tmp")));
}

// A root whose instance file cannot be read is dropped by the server and its tablets become
// invisible. The survey must say its answer is incomplete rather than report a clean node -- this
// happened on a real node in a real field incident of this bug, where the instance file failed its
// checksum, which no existence check would have caught.
TEST_F(FsRootPinManagerTest, CorruptInstanceFileMakesTheSurveyIncomplete) {
  auto fs = OpenFsManager();
  ASSERT_OK(fs->CertifyDataRoots(
      {TabletOn(root_a_, root_a_, "t1"), TabletOn(root_b_, root_b_, "t2")}));

  // Present, wrong contents: a read corruption, not a missing file.
  ASSERT_OK(WriteStringToFile(
      env_.get(), Slice("not a protobuf container"), fs->GetInstanceMetadataPath(root_b_)));

  auto fs2 = MakeFsManager();
  auto report = ASSERT_RESULT(fs2->SurveyDataRoots({}, FsRootEvidenceComplete::kFalse));
  ASSERT_TRUE(report.HasWarnings());
  ASSERT_EQ(1, report.dropped_roots.count(root_b_)) << "the corrupt root must be reported dropped";
  ASSERT_STR_CONTAINS(report.WarningMessage(), "not a clean node");
  ASSERT_EQ(kFsPinExitWarning, FsPinExitCodeForReport(report));
}

// A root with no instance file at all, while its sibling has one. This is what an unmounted data
// volume looks like: an empty directory on the root filesystem.
TEST_F(FsRootPinManagerTest, MissingInstanceFileIsReportedAsUnformatted) {
  auto fs = OpenFsManager();
  ASSERT_OK(env_->DeleteFile(fs->GetInstanceMetadataPath(root_b_)));

  auto fs2 = MakeFsManager();
  auto report = ASSERT_RESULT(fs2->SurveyDataRoots({}, FsRootEvidenceComplete::kTrue));
  ASSERT_EQ(FsRootState::kUnformatted, StateOf(report, root_b_));
  ASSERT_TRUE(report.HasWarnings());
  ASSERT_EQ(kFsPinExitWarning, FsPinExitCodeForReport(report));
}

// A crash between the temp write and the rename leaves a file that DeleteFileSystemLayout's
// exact-path removal does not match and that IsDirectoryEmpty does not excuse, which would break
// the delete-then-recreate cycle just as surely as the pin itself did.
TEST_F(FsRootPinManagerTest, LeftoverPinTempFileIsSweptAndDoesNotBlockReformatting) {
  auto fs = OpenFsManager();
  const auto stale = PinPathFor(root_a_) + ".tmp.abc123";
  ASSERT_OK(WriteStringToFile(env_.get(), Slice("torn"), stale));

  ASSERT_OK(fs->DeleteFileSystemLayout());
  ASSERT_FALSE(env_->FileExists(stale));

  auto fs2 = MakeFsManager();
  ASSERT_OK(fs2->CreateInitialFileSystemLayout());
  ASSERT_OK(fs2->CheckAndOpenFileSystemRoots());
}

// Writing a pin also sweeps a leftover, so a node does not accumulate them across crashes.
TEST_F(FsRootPinManagerTest, WritingAPinSweepsALeftoverTempFile) {
  auto fs = OpenFsManager();
  const auto stale = PinPathFor(root_a_) + ".tmp.abc123";
  ASSERT_OK(WriteStringToFile(env_.get(), Slice("torn"), stale));

  ASSERT_OK(fs->CertifyDataRoots(
      {TabletOn(root_a_, root_a_, "t1"), TabletOn(root_b_, root_b_, "t2")}));
  ASSERT_FALSE(env_->FileExists(stale));
  ASSERT_TRUE(env_->FileExists(PinPathFor(root_a_)));
}

// A cosmetic edit to --fs_data_dirs must not brick a node. POSIX dirname/basename strip trailing
// slashes, so canonicalization makes "/mnt/d0/" and "/mnt/d0" the same string -- but that is the
// kind of property worth pinning down, since a divergence would present as a mount swap.
TEST_F(FsRootPinManagerTest, TrailingSlashInFsDataDirsStillVerifies) {
  auto fs = OpenFsManager();
  ASSERT_OK(fs->CertifyDataRoots(
      {TabletOn(root_a_, root_a_, "t1"), TabletOn(root_b_, root_b_, "t2")}));

  FsManagerOpts opts;
  opts.data_paths = {root_a_ + "/", root_b_ + "/"};
  opts.wal_paths = opts.data_paths;
  opts.server_type = kServerType;
  FsManager with_slashes(env_.get(), opts);

  ASSERT_OK(with_slashes.CheckAndOpenFileSystemRoots());
  auto report = ASSERT_RESULT(with_slashes.SurveyDataRoots({}, FsRootEvidenceComplete::kFalse));
  ASSERT_FALSE(report.HasFailure()) << report.FailureMessage();
  ASSERT_EQ(FsRootState::kVerified, StateOf(report, root_a_));
}

// The path arithmetic the whole comparison rests on, exercised against paths built exactly the way
// RaftGroupMetadata::CreateNew builds them (rocksdb dir = <data root>/rocksdb/table-X/tablet-Y, WAL
// dir = <wal root>/table-X/tablet-Y). A mistake here becomes a fleet-wide false refusal, so it is
// checked against the real construction rather than against hand-written literals.
TEST_F(FsRootPinManagerTest, SuperblockPathShapesNormalizeToTheFsRoot) {
  auto fs = OpenFsManager();

  const auto data_root = fs->GetDataRootDirs()[0];      // <root>/yb-data/<server>/data
  const auto wal_root = fs->GetWalRootDirs()[0];        // <root>/yb-data/<server>/wals
  const auto rocksdb_dir = JoinPathSegments(
      data_root, FsManager::kRocksDBDirName, "table-abc", "tablet-def");
  const auto wal_dir = JoinPathSegments(wal_root, "table-abc", "tablet-def");

  // data_root_dir() and wal_root_dir() strip back to these two, and GetTabletPath() yields the
  // server data path. All three must land on the same fs root.
  const auto expected = FsRootOfYbDataPath(data_root);
  ASSERT_FALSE(expected.empty());
  ASSERT_EQ(expected, FsRootOfYbDataPath(data_root));
  ASSERT_EQ(expected, FsRootOfYbDataPath(wal_root));
  ASSERT_EQ(expected, FsRootOfYbDataPath(rocksdb_dir));
  ASSERT_EQ(expected, FsRootOfYbDataPath(wal_dir));
  ASSERT_EQ(expected, FsRootOfYbDataPath(GetServerTypeDataPath(expected, kServerType)));

  // And it is one of the roots the manager is actually configured with.
  ASSERT_TRUE(expected == root_a_ || expected == root_b_) << expected;
}

}  // namespace yb
