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
// yb-fs_pin -- standalone check for swapped data mounts.
//
// This is the interface YugabyteDB Anywhere invokes. It runs on the node, against the
// --fs_data_dirs directories in place, with the server stopped: the same check the server runs at
// startup, reporting what the server would decide.
//
//   yb-fs_pin --fs_data_dirs=/mnt/d0,/mnt/d1 survey
//   yb-fs_pin --fs_data_dirs=/mnt/d0,/mnt/d1 --output_format=json survey
//   yb-fs_pin --fs_data_dirs=/mnt/d0,/mnt/d1 pin
//   yb-fs_pin --fs_data_dirs=/mnt/d0,/mnt/d1 repin
//
// `survey` writes nothing at all: it does not create directories, does not probe the roots with a
// temp file (which CheckAndOpenFileSystemRoots does), and does not load tablet metadata through
// RaftGroupMetadata (which would rewrite pre-tiered-storage superblocks on first read). It reads
// each superblock as a raw protobuf.
//
// `pin` runs the same check and then certifies the roots the evidence proves. It is the step to
// run after provisioning.
//
// `repin` is the repair path. It rewrites a stale pin, and only where the superblocks under the
// root prove the data belongs there -- so it fixes "the record is wrong" and refuses "the volume
// moved", which no pin can fix and which needs a remount instead.
//
// Exit codes, the contract with YugabyteDB Anywhere, are defined in fs_root_pin.h and derived from
// the report's severity rather than from what a server would do about it: blocking an upgrade
// beforehand is cheap, and a running server is deliberately configured to tolerate some of what
// should still stop an upgrade.

#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "yb/fs/fs_manager.h"
#include "yb/fs/fs_root_pin.h"

#include "yb/tablet/metadata.pb.h"

#include "yb/util/env.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/path_util.h"
#include "yb/util/pb_util.h"
#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"

DEFINE_NON_RUNTIME_string(server_type, "tserver",
    "Which server's data layout to check: \"tserver\" or \"master\".");

DEFINE_NON_RUNTIME_string(output_format, "text",
    "\"text\" for the operator-facing report, \"json\" for a machine-readable one.");

DEFINE_NON_RUNTIME_bool(force, false,
    "For `repin` only: also rewrite pins on roots where no superblock can prove where the data "
    "belongs. Never overrides a root whose superblocks show the volume actually moved; that needs "
    "a remount, not a pin.");

DECLARE_string(fs_data_dirs);
DECLARE_string(fs_wal_dirs);

namespace yb {
namespace tools {

namespace {

enum class Command { kSurvey, kPin, kRepin };

void PrintUsageToStream(const std::string& prog_name, std::ostream* out) {
  *out << "Usage: " << prog_name << " --fs_data_dirs=<dirs> [--fs_wal_dirs=<dirs>]\n"
       << "                 [--server_type=tserver|master] [--output_format=text|json]\n"
       << "                 <survey|pin|repin>\n\n"
       << "Commands:\n"
       << "  survey   Report whether each data root is mounted where it was certified.\n"
       << "           Read-only: writes nothing, safe to run on a live node.\n"
       << "  pin      Same check, and record a pin for every root the tablet superblocks\n"
       << "           certify. Run after provisioning.\n"
       << "  repin    Repair a stale pin, where the superblocks under the root prove the\n"
       << "           data belongs there. Refuses roots whose superblocks show the volume\n"
       << "           really moved: those need a remount, which no pin substitutes for.\n"
       << "           --force additionally repins roots with no superblock evidence.\n\n"
       << "Exit codes:\n"
       << "  " << kFsPinExitOk << "  all data roots verified or benignly uncertified\n"
       << "  " << kFsPinExitFailure << "  a data root's layout is wrong; block the upgrade\n"
       << "  " << kFsPinExitCannotRun << "  the check could not run; unknown, not clean\n"
       << "  " << kFsPinExitWarning << "  warnings only; do not block\n";
}

// Reads one tablet superblock as a raw protobuf.
//
// Deliberately not RaftGroupMetadata::Load(): that path rewrites the superblock in place when it
// predates the tiered-storage tier_paths migration, which would make a "read-only" survey persist
// the very layout it is supposed to be judging.
TabletSuperblockEvidence ReadSuperblockEvidence(
    Env* env, const std::string& root, const std::string& server_type,
    const std::string& tablet_id) {
  TabletSuperblockEvidence evidence;
  evidence.tablet_id = tablet_id;
  evidence.containing_root = root;

  const auto path = JoinPathSegments(
      FsManager::GetRaftGroupMetadataDir(GetServerTypeDataPath(root, server_type)), tablet_id);

  tablet::RaftGroupReplicaSuperBlockPB superblock;
  const auto s = pb_util::ReadPBContainerFromPath(env, path, &superblock);
  if (!s.ok()) {
    // A superblock we cannot read is not evidence of anything, and a root made entirely of them
    // must not read as "a root with no tablets". Recording the error keeps it in the report and in
    // the JSON, rather than only on stderr where the caller parsing our output will not see it.
    evidence.read_error = s.ToString(/* include_file_and_line = */ false);
    return evidence;
  }

  evidence.recorded_data_root = FsRootOfYbDataPath(superblock.kv_store().rocksdb_dir());
  evidence.recorded_wal_root = FsRootOfYbDataPath(superblock.wal_dir());
  return evidence;
}

Result<std::vector<TabletSuperblockEvidence>> CollectEvidence(
    FsManager* fs_manager, const std::string& server_type) {
  auto by_root = VERIFY_RESULT(fs_manager->ListTabletIdsByRoot());

  std::vector<TabletSuperblockEvidence> evidence;
  for (const auto& [root, tablet_ids] : by_root) {
    for (const auto& tablet_id : tablet_ids) {
      evidence.push_back(ReadSuperblockEvidence(fs_manager->env(), root, server_type, tablet_id));
    }
  }
  return evidence;
}

void PrintTextReport(const FsRootPinReport& report, std::ostream* out) {
  *out << report.SummaryLine() << "\n\n";
  for (const auto& v : report.verdicts) {
    *out << "  " << v.root << ": " << ToString(v.state);
    if (!v.pin.mount_point.empty() && v.pin.mount_point != v.root) {
      *out << " (pinned at " << v.pin.mount_point << ")";
    }
    *out << ", " << v.superblocks_seen
         << (v.superblocks_seen == 1 ? " tablet superblock" : " tablet superblocks");
    if (v.superblocks_with_evidence != v.superblocks_seen) {
      *out << " (" << v.superblocks_with_evidence << " usable as evidence)";
    }
    const auto repin = DecideRepin(v);
    if (repin != FsRootRepinDecision::kNotNeeded) {
      *out << ", repin: " << ToString(repin);
    }
    *out << "\n";
    if (!v.read_error.empty()) {
      *out << "      pin file: " << v.read_error << "\n";
    }
    // The whole point of the offline survey is that it prints every disagreeing tablet, where the
    // startup message truncates. Support has had to reconstruct this table by hand in a real
    // incident of this bug.
    for (const auto& t : v.disagreeing_superblocks) {
      *out << "      tablet " << t.tablet_id << ": superblock says " << t.recorded_data_root
           << ", found under " << t.containing_root << "\n";
    }
    for (const auto& t : v.unreadable_superblocks) {
      *out << "      tablet " << t.tablet_id << ": unreadable superblock: " << t.read_error << "\n";
    }
    for (const auto& t : v.unknown_wal_root_superblocks) {
      *out << "      tablet " << t.tablet_id << ": WAL root " << t.recorded_wal_root
           << " is not in --fs_wal_dirs\n";
    }
  }

  if (report.HasFailure()) {
    *out << report.FailureMessage(FsRootPinReport::Audience::kOfflineCommand);
  } else if (report.HasWarnings()) {
    *out << "\nWarnings:\n" << report.WarningMessage();
  }
}

// Writes a pin for `root`. Shared by `pin` (certifying an unpinned root) and `repin` (replacing a
// stale one); the difference is entirely in which roots the caller passes.
Status WritePinFor(const std::string& root) {
  const auto pin = FsRootPin::ForMountPoint(root);
  const auto pin_path = FsRootPinPath(root, FLAGS_server_type);
  RETURN_NOT_OK(WriteFsRootPinFile(Env::Default(), pin_path, pin));
  std::cout << "pinned " << root << " -> " << pin_path << std::endl;
  return Status::OK();
}

int RunPin(const FsRootPinReport& report) {
  if (report.HasFailure()) {
    std::cerr << "\nrefusing to pin: the layout above is wrong, and `pin` only ever records what\n"
              << "the superblocks already prove. If a pin is merely stale, `repin` is the repair\n"
              << "path." << std::endl;
    return kFsPinExitFailure;
  }
  for (const auto& root : report.CertifiableRoots()) {
    const auto s = WritePinFor(root);
    if (!s.ok()) {
      std::cerr << "error: unable to pin " << root << ": " << s.ToString() << std::endl;
      return kFsPinExitCannotRun;
    }
  }
  return FsPinExitCodeForReport(report);
}

// Worst severity across the report's roots, ignoring `repaired`. Lets a command that just fixed
// some roots exit for what is still wrong rather than for what was wrong when it started.
FsRootSeverity SeverityExcluding(
    const FsRootPinReport& report, const std::set<std::string>& repaired) {
  auto worst = FsRootSeverity::kOk;
  for (const auto& v : report.verdicts) {
    if (repaired.contains(v.root)) {
      continue;
    }
    worst = std::max(worst, v.Severity());
  }
  // A root that could not be opened is still unexamined however many pins were rewritten.
  if (!report.dropped_roots.empty()) {
    worst = std::max(worst, FsRootSeverity::kWarning);
  }
  return worst;
}

int RunRepin(const FsRootPinReport& report) {
  std::vector<std::string> to_repin;
  std::vector<const FsRootVerdict*> moved;
  std::vector<const FsRootVerdict*> unprovable;

  for (const auto& v : report.verdicts) {
    switch (DecideRepin(v)) {
      case FsRootRepinDecision::kSafe: to_repin.push_back(v.root); break;
      case FsRootRepinDecision::kUnsafeVolumeMoved: moved.push_back(&v); break;
      case FsRootRepinDecision::kUnprovable: unprovable.push_back(&v); break;
      case FsRootRepinDecision::kNotNeeded: break;
    }
  }

  for (const auto* v : moved) {
    std::cerr << "\nrefusing to repin " << v->root << ": " << v->disagreeing_superblocks.size()
              << " of its tablet superblocks record a different data root, so this\n"
              << "volume really did move. Its tablets will not find their data at this path "
              << "whatever\nthe pin says. Remount the volume where it belongs instead."
              << std::endl;
  }

  // Kept apart from `unprovable`: a forced root has been repaired, so it must not go on
  // contributing to the exit code. Leaving it in was why a successful `repin --force` exited 1,
  // which the documented contract defines as "a data root's layout is wrong; block the upgrade" -
  // exactly backwards for a command that just fixed it, and read by anything scripted around
  // these codes.
  for (const auto* v : unprovable) {
    if (FLAGS_force) {
      std::cerr << "\n--force: repinning " << v->root << " with no superblock evidence that the\n"
                << "data belongs here. Nothing verified this; you are asserting it." << std::endl;
      to_repin.push_back(v->root);
    } else {
      std::cerr << "\nnot repinning " << v->root << ": no superblock under it can prove where its\n"
                << "data belongs, so rewriting the pin would record a mapping nothing verified.\n"
                << "Re-run with --force to assert it anyway." << std::endl;
    }
  }

  if (to_repin.empty()) {
    std::cerr << "\nnothing repinned." << std::endl;
    return FsPinExitCodeForReport(report);
  }

  std::set<std::string> repaired;
  for (const auto& root : to_repin) {
    const auto s = WritePinFor(root);
    if (!s.ok()) {
      std::cerr << "error: unable to repin " << root << ": " << s.ToString() << std::endl;
      return kFsPinExitCannotRun;
    }
    repaired.insert(root);
  }

  // What is left wrong now, not what was wrong when we started. Deriving it per root also stops a
  // fully repaired node from returning 0 while an unrelated warning - an unformatted sibling, say -
  // is still outstanding, which the old whole-report check hid in the other direction. A fully
  // repaired node comes back clean on the next survey, which is what the operator should run to
  // confirm.
  return FsPinExitCodeForSeverity(SeverityExcluding(report, repaired));
}

int Run(Command command) {
  FsManagerOpts opts;
  opts.server_type = FLAGS_server_type;
  // The survey must not create or probe anything. The writing commands need exactly one file per
  // root, which WriteFsRootPinFile does directly, so they do not need a writable FsManager either.
  opts.read_only = (command == Command::kSurvey);

  FsManager fs_manager(Env::Default(), opts);

  auto evidence = CollectEvidence(&fs_manager, FLAGS_server_type);
  if (!evidence.ok()) {
    std::cerr << "error: " << evidence.status().ToString() << std::endl;
    return kFsPinExitCannotRun;
  }

  auto report = fs_manager.SurveyDataRoots(*evidence, FsRootEvidenceComplete::kTrue);
  if (!report.ok()) {
    std::cerr << "error: " << report.status().ToString() << std::endl;
    return kFsPinExitCannotRun;
  }

  if (FLAGS_output_format == "json") {
    std::cout << report->ToJson() << std::endl;
  } else {
    PrintTextReport(*report, &std::cout);
  }

  switch (command) {
    case Command::kSurvey: return FsPinExitCodeForReport(*report);
    case Command::kPin: return RunPin(*report);
    case Command::kRepin: return RunRepin(*report);
  }
  return kFsPinExitCannotRun;
}

}  // anonymous namespace

static int FsPinToolMain(int argc, char** argv) {
  FLAGS_logtostderr = 1;
  std::stringstream usage_str;
  PrintUsageToStream(argv[0], &usage_str);
  google::SetUsageMessage(usage_str.str());
  ParseCommandLineFlags(&argc, &argv, true);
  InitGoogleLoggingSafe(argv[0]);

  if (argc < 2) {
    PrintUsageToStream(argv[0], &std::cerr);
    return kFsPinExitCannotRun;
  }
  const std::string command_name = argv[1];
  Command command;
  if (command_name == "survey") {
    command = Command::kSurvey;
  } else if (command_name == "pin") {
    command = Command::kPin;
  } else if (command_name == "repin") {
    command = Command::kRepin;
  } else {
    std::cerr << "error: unknown command \"" << command_name << "\"" << std::endl << std::endl;
    PrintUsageToStream(argv[0], &std::cerr);
    return kFsPinExitCannotRun;
  }

  if (FLAGS_output_format != "text" && FLAGS_output_format != "json") {
    std::cerr << "error: --output_format must be \"text\" or \"json\"" << std::endl;
    return kFsPinExitCannotRun;
  }
  if (FLAGS_fs_data_dirs.empty()) {
    std::cerr << "error: --fs_data_dirs is required" << std::endl;
    return kFsPinExitCannotRun;
  }
  if (FLAGS_server_type != "tserver" && FLAGS_server_type != "master") {
    std::cerr << "error: --server_type must be \"tserver\" or \"master\"" << std::endl;
    return kFsPinExitCannotRun;
  }
  if (FLAGS_server_type == "master" && command != Command::kSurvey) {
    // A master enforces any pin it finds, because VerifyExistingDataRootPins runs from
    // CheckAndOpenFileSystemRoots, which is server-type agnostic. But no master code path calls
    // CertifyDataRoots, so a master pin would never be refreshed by the server after a legitimate
    // change: writing one leaves master coverage in a state that is neither on nor off. Surveying
    // is read-only and useful, so only the writing commands are blocked.
    std::cerr << "error: --server_type=master supports `survey` only. Master-side certification is"
              << "\nnot implemented yet, so a master pin would be enforced at startup but never"
              << "\nmaintained by the server." << std::endl;
    return kFsPinExitCannotRun;
  }

  return Run(command);
}

}  // namespace tools
}  // namespace yb

int main(int argc, char** argv) {
  return yb::tools::FsPinToolMain(argc, argv);
}
