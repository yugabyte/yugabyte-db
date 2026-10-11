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

// Detection of swapped data mounts.
//
// A node's data roots (e.g., /mnt/d0 and /mnt/d1) can end up mounted on the wrong physical
// volumes: AWS Nitro NVMe probe order is not stable across re-imaging, and on-prem operators can
// remount by hand. Left undetected, that goes badly in a specific way: the tablet server finds no
// data where a tablet superblock says it should be, creates a fresh replica on the other disk, and
// the *next* restart FATALs on duplicate tablet metadata -- by which point a replica has been
// created and sometimes remote-bootstrapped. Nothing else in the system catches it in between.
//
// This file holds the detector. Each data root carries a small JSON "pin" file recording the mount
// point it was certified at; at startup we compare the pin against the path the root is actually
// mounted at, and refuse to launch when they disagree. A root with no pin is certified from the
// tablet superblocks that live under it -- a superblock records its data directory as an absolute
// path, written when the layout was correct, so a superblock naming the root it was found under is
// proof the volume has not moved. The converse holds too, which is what makes the evidence safe to
// condemn with: nothing relocates a tablet between data roots in place (kv_store.rocksdb_dir is
// set at CreateNew and at tablet split, and a split child stays under its parent's root), so a
// superblock naming a *different* root is never the stale record of a legitimate move -- it means
// the volume is not where its data was written.
//
// To follow how a root is judged and what happens as a result, start at FsRootState below: the
// per-root states are the heart of the design, and everything else in this header either
// produces them (EvaluateFsRootPins), acts on them (FsRootPinStartupPolicy), or repairs them
// (FsRootRepinDecision). tools/fs_pin-tool.cc documents yb-fs_pin -- the same check, run on the
// node with the server stopped -- and the exit codes it contracts with YugabyteDB Anywhere.
//
// ==========================================================================
//  The pin file
// ==========================================================================
//
// At <root>/yb-data/<server_type>/fs-root-pin.json. For example:
//
//   { "mount_point": "/mnt/d0",
//     "pinned_at": "2026-08-04T21:14:03Z",
//     "filesystem_uuid": "b3f2a41c-6d0e-4f3a-9c1b-2a7d8e5f0c44" }
//
// JSON rather than protobuf so an operator on a broken node can read it with cat, and so support
// can hand-repair one. Only mount_point gates startup. pinned_at is audit only: it lets support
// tell whether a pin predates the incident being investigated. filesystem_uuid is a breadcrumb for
// matching a failure message to a device, and is empty wherever /dev/disk/by-uuid is not mounted,
// which includes most containers -- so never build tooling that depends on it.
//
// Writes are atomic (temp file, fsync, rename, fsync the directory) because unusable pin contents
// refuse startup, so a torn write would otherwise brick the node.
//
// ==========================================================================
//  Three separations, each with a reason
// ==========================================================================
//
//  - Decision (EvaluateFsRootPins) is separated from all I/O, so every combination of pin file
//    and superblock evidence can be unit tested without a filesystem.
//
//  - Severity (FsRootSeverity: is something wrong?) is separated from action
//    (FsRootPinReport::ShouldRefuseStartup: does this node refuse to boot?). The survey
//    (`yb-fs_pin survey`, the pre-upgrade check in tools/fs_pin-tool.cc) must flag things that a
//    running server deliberately tolerates. Blocking an upgrade beforehand is cheap; refusing to
//    boot afterwards is not.
//
//  - Repair (DecideRepin) is separated from detection, because the only safe repair is the one the
//    evidence licenses: a pin may be rewritten when the superblocks under the root certify the
//    current mount point, and never when they corroborate the old pin.
//
// The severity/action split matters most for FsRootState::kEvidenceConflict, the one state that
// needs no pin at all and so can fire on a node's very first boot after a binary upgrade; what a
// server does about it is deliberately conservative (see
// FsRootPinStartupPolicy::refuse_on_superblock_conflict below).
//
// ==========================================================================
//  Where the checks run
// ==========================================================================
//
// The server checks at two stages of startup, because the evidence arrives in two stages.
//
//  - FsManager::CheckAndOpenFileSystemRoots decides the states that need only the pin files (via
//    FsManager::VerifyExistingDataRootPins), right after the loop that reads each root's instance
//    file. It has to run before CreateFileSystemRoots(), which adopts a root that reads as empty
//    by writing a fresh instance file into it: right for a genuine disk replacement, exactly
//    wrong for a volume that failed to mount. Tablets are not enumerated yet, so unpinned roots
//    come back kPending for the second stage rather than being certified from evidence never
//    gathered.
//
//  - TSTabletManager::Init decides the remaining states once superblocks are loaded, from
//    metadata already in memory, and before RegisterDataAndWalDir() -- which registers the root
//    derived from the superblock rather than the root the superblock was found under, so on a
//    swapped node it would populate the drive assignment maps with the wrong disk.
//
// Only a *refusing* state stops the adoption described in the first bullet: kUnformatted does not
// refuse, so such a root is still adopted on the same start, and its warning says so outright.
//
// ==========================================================================
//  Alternatives considered and rejected
// ==========================================================================
//
//  - Automatic recovery. Decided against: this feature only detects, reports, and refuses. It
//    does not rewrite paths, delete duplicates, or pick a winner, because every one of those can
//    destroy the only remaining copy of something if the diagnosis is wrong, and the diagnosis
//    rests on absolute paths that are wrong by definition in the situation being diagnosed.
//
//  - Treating an unparseable pin as absent and pinning a root with no tablets. Both would record
//    a mapping nothing proves; the comments on FsRootState::kPinUnparseable and
//    FsRootState::kUncertified below explain what each would cost.
//
//  - Reusing the KvStoreInfoPB::tier_paths list added for tiered storage. A swap leaves the *set*
//    of roots unchanged, so a set comparison catches a replaced disk and never catches a swap.
//    Only slot 0 / kv_store.rocksdb_dir is evidence, which is what this file compares.
//
//  - Deriving the identity from the device rather than the mount point (the filesystem UUID, or
//    the disk's World Wide Name). It would be the stronger record, but it is unavailable in
//    containers and after a volume-level restore, and the mount point is what the tablet
//    superblocks are written in terms of.

#pragma once

#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "yb/util/enums.h"
#include "yb/util/result.h"
#include "yb/util/status_fwd.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb {

class Env;

// The contents of one pin file. JSON, not protobuf, so that an operator staring at a broken node
// can read it with cat and a support engineer can hand-repair it.
struct FsRootPin {
  // The data root this volume was certified at, canonicalized the way FsManager canonicalizes
  // every root: realpath(3) on the parent directory (resolving symlinks and dot segments in the
  // ancestry) with the root's own final path component re-appended as spelled. The only field
  // that gates startup.
  std::string mount_point;

  // ISO-8601 UTC, e.g., "2026-08-04T21:14:03Z". Audit only: lets support tell whether a pin
  // predates the incident being investigated and is therefore trustworthy.
  std::string pinned_at;

  // Best-effort filesystem UUID: a diagnostic breadcrumb so an operator can match the failure
  // message to a device. Never gates anything. Empty when it cannot be determined, which includes
  // most container deployments, where /dev/disk/by-uuid is not mounted -- so treat its presence as
  // a bonus and never build tooling that depends on it.
  std::string filesystem_uuid;

  std::string ToJson() const;

  // Rejects anything that is not a JSON object with a non-empty absolute "mount_point".
  static Result<FsRootPin> ParseJson(const std::string& json);

  // A pin certifying `mount_point` as of now: pinned_at is the current UTC time, and
  // filesystem_uuid is a best-effort probe of the volume currently mounted there.
  static FsRootPin ForMountPoint(const std::string& mount_point);
};

// How a pin file presented itself on disk.
YB_DEFINE_ENUM(FsRootPinFileState,
    // No pin file at this root: never certified, or the pin was deleted.
    (kAbsent)
    // Pin file read and parsed successfully.
    (kPresent)
    // Pin file read, but its contents are not a usable pin: e.g., truncated, corrupt, not JSON, or
    // missing mount_point. Deliberately NOT treated as absent; the FsRootState::kPinUnparseable
    // state below has the reasoning.
    (kUnparseable)
    // The pin file could not be read at all: e.g., EIO, EACCES, a full descriptor table, or an NFS
    // hiccup. Distinct from kUnparseable because the fault is the device, not the contents; the
    // FsRootState::kPinIoError state below has the reasoning.
    (kIoError));

struct FsRootPinFile {
  FsRootPinFileState state = FsRootPinFileState::kAbsent;
  FsRootPin pin;         // Valid only when state == kPresent.
  std::string error;     // Populated when state is kUnparseable or kIoError.
};

// What one tablet superblock says about where its data lives. recorded_data_root and
// recorded_wal_root are empty when the caller located the superblock but did not parse it, when
// the superblock could not be read (see read_error), or when the tablet is tombstoned and carries
// no RocksDB directory.
struct TabletSuperblockEvidence {
  // The tablet this superblock belongs to. Known even when the superblock's contents cannot be
  // read, because superblock files are named by tablet id.
  std::string tablet_id;

  // The data root whose tablet-meta directory held this superblock.
  std::string containing_root;

  // The data root implied by the superblock's kv_store.rocksdb_dir.
  std::string recorded_data_root;

  // The data root implied by the superblock's wal_dir.
  std::string recorded_wal_root;

  // Empty when the superblock was read and parsed fine; otherwise, why it could not be read.
  // Recorded because a superblock we cannot read is not evidence of anything, and a root made
  // entirely of them must not read as "no tablets".
  std::string read_error;
};

// The state of one data root: is its volume mounted where the records say it belongs, and can
// that even be determined? Decided by EvaluateFsRootPins below; the full decision table, every
// combination of pin file and superblock evidence, is with its implementation in fs_root_pin.cc.
YB_DEFINE_ENUM(FsRootState,
    // Pin file present and it names this root. Proceed.
    (kVerified)
    // Pin file present and it names a different root: this volume is mounted somewhere it does
    // not belong.
    (kMismatched)
    // Pin file present but its contents are unusable (FsRootPinFileState::kUnparseable). Treating
    // it as absent would let a torn write re-certify whatever layout happens to be mounted, so
    // this is a failure, not a fresh start.
    (kPinUnparseable)
    // The pin file could not be read because of a device or permission fault. We can neither trust
    // nor replace it, so the root is left exactly as it is: not certified, not condemned.
    (kPinIoError)
    // No pin file, and every superblock under this root names this root. The evidence certifies
    // the root, so a certifying caller writes the pin; the read-only survey just reports it.
    (kCertifiable)
    // No pin file, and at least one superblock under this root names a different data root. We
    // will not record a mapping the evidence contradicts.
    (kEvidenceConflict)
    // No pin file and no usable superblocks. With no tablets there is no evidence the root is
    // where it belongs, and we will not record a mapping we cannot prove. Certified on a later
    // start once tablets land; nothing is at risk meanwhile, because a swap can only cause harm
    // if data moved.
    (kUncertified)
    // No pin file, and the caller has not gathered superblock evidence yet. The FsManager pass
    // runs before tablets are enumerated, so it leaves these for the TSTabletManager pass.
    (kPending)
    // The root has no instance file while its siblings do: it was never formatted, was wiped, is a
    // blank replacement volume, is newly added to --fs_data_dirs, or -- the case worth catching --
    // its volume did not mount and we are looking at an empty directory on the root filesystem.
    // Nothing on disk separates those last cases from each other, which is why this state warns
    // and never refuses: the root is still adopted on this same start. Benign on a genuinely fresh
    // node, where no root has an instance file.
    //
    // Note the signal is relative to the siblings, so it catches the *partial* case only. When
    // every volume on a node fails to mount, no root has an instance file, nothing here fires, and
    // the node reads as pristine. Detecting that needs an absolute reference the node does not
    // have; the pin files are on the volumes that did not mount.
    (kUnformatted));

// Is something wrong, independent of what any particular caller does about it. Declared in
// increasing order of seriousness: FsRootPinReport::Severity() takes the max over its verdicts, so
// the order is load-bearing.
YB_DEFINE_ENUM(FsRootSeverity,
    (kOk)
    // Worth an operator's attention, and worth blocking an upgrade over, but not by itself grounds
    // for the server refusing to start.
    (kWarning)
    // The recorded layout and the actual layout disagree. Whether that stops the process is a
    // policy decision; that it is wrong is not.
    (kFailure));

// The verdict on one data root: its state, plus every observation that went into deciding it.
struct FsRootVerdict {
  // The data root this verdict is about: an absolute path, canonicalized the way FsManager
  // canonicalizes every root (see FsRootPin::mount_point).
  std::string root;

  FsRootState state = FsRootState::kPending;

  // Path of the pin file this verdict is about. Always set, whether or not the file exists.
  std::string pin_path;

  // The pin file's contents; pin.mount_point is where the pin says this volume belongs. Fields
  // are set only when the pin file was read and parsed, so for kVerified and kMismatched.
  FsRootPin pin;

  // Why the pin file could not be read or parsed. Set for kPinUnparseable and kPinIoError.
  std::string read_error;

  // Superblock files found under this root, whether or not their contents were usable.
  size_t superblocks_seen = 0;

  // Of superblocks_seen, how many recorded a usable data root: read and parsed fine, and not a
  // tombstoned tablet (which carries no RocksDB directory). Only these certify or condemn.
  size_t superblocks_with_evidence = 0;

  // The superblocks under this root that name a different data root -- i.e., exactly which
  // tablets' metadata disagrees. Populated whenever the evidence shows it, whatever the state:
  // for kEvidenceConflict it is the failure itself, while for kVerified it is a warning (the pin
  // proves the volume did not move, so a disagreeing superblock means something moved a tablet
  // directory by hand).
  std::vector<TabletSuperblockEvidence> disagreeing_superblocks;

  // Superblocks under this root whose recorded WAL root is not one of the configured WAL roots.
  // Populated whatever the state. Reported, never a failure: --fs_wal_dirs can legitimately be
  // re-pointed.
  std::vector<TabletSuperblockEvidence> unknown_wal_root_superblocks;

  // Superblocks under this root that could not be read. Populated whatever the state, so that a
  // root whose metadata is entirely unreadable cannot pass as a root with nothing on it.
  std::vector<TabletSuperblockEvidence> unreadable_superblocks;

  // This root is on the same device as "/" while a sibling data root is not, which is what an
  // unmounted mount point looks like from inside. Only ever used to word the kUnformatted warning
  // more sharply; see FsRootPinInputs::likely_unmounted_roots for why it gates nothing.
  bool likely_unmounted = false;

  FsRootSeverity Severity() const;
};

// What a server does about a report. Separated from severity so that the offline survey can flag
// what a running server tolerates: blocking an upgrade beforehand is cheap, refusing to boot
// afterwards is not.
struct FsRootPinStartupPolicy {
  // A pin we wrote ourselves disagrees with reality (kMismatched / kPinUnparseable): refuse to
  // start. When false, log the identical diagnosis and continue.
  bool refuse_on_pin_mismatch = true;

  // kEvidenceConflict refuses to start. Off by default: unlike the pin states, this one fires
  // purely from pre-existing superblocks, so it is the only state that can condemn a node on its
  // very first boot after a binary upgrade, with no pin this feature ever wrote involved.
  bool refuse_on_superblock_conflict = false;

  // Write pins for the roots the evidence certifies. Independent of `refuse_on_pin_mismatch`, so
  // a warn-only deployment still accumulates the pins that make the strong check work
  // later. On the server this comes from --fs_root_pin_write, an AutoFlag: automatic pin writing
  // waits for the upgrade to be finalized, so a rollback never strands pin files on a version
  // that cannot clean them (the flag's help text has the details).
  bool write_pins = true;
};

// Reads the policy from --fs_root_pin_* gflags. Used by the server and by yb-fs_pin, so
// that "what the survey says the server would do" and "what the server does" cannot drift.
FsRootPinStartupPolicy FsRootPinPolicyFromFlags();

struct FsRootPinReport {
  std::vector<FsRootVerdict> verdicts;  // Sorted by root.

  // Roots that CheckAndOpenFileSystemRoots dropped because their instance file could not be read.
  // Tablets on them are invisible to this report, so a clean verdict here is not a clean node.
  std::set<std::string> dropped_roots;

  FsRootSeverity Severity() const;
  bool HasFailure() const { return Severity() == FsRootSeverity::kFailure; }
  bool HasWarnings() const { return Severity() == FsRootSeverity::kWarning; }

  // Whether any root's state stops the process under `policy`.
  bool ShouldRefuseStartup(const FsRootPinStartupPolicy& policy) const;

  // Roots whose state is kCertifiable, i.e., the ones a certifying caller writes a pin for.
  std::vector<std::string> CertifiableRoots() const;

  const FsRootVerdict* FindRoot(const std::string& root) const;

  // Where this text is going. The startup message points the reader at yb-fs_pin for the
  // per-tablet detail; yb-fs_pin's own output must not tell the reader to run the command they
  // just ran.
  enum class Audience { kServerLog, kOfflineCommand };

  // Multi-line operator-facing text naming every affected root and the tablets whose metadata
  // disagrees. Empty when there is no failure. This is the FATAL body.
  std::string FailureMessage(Audience audience = Audience::kServerLog) const;

  // Multi-line text for anomalies that are not failures. Empty when there are none.
  std::string WarningMessage() const;

  // One line, for the ordinary success path.
  std::string SummaryLine() const;

  // Machine-readable form, for YugabyteDB Anywhere and for support tooling.
  std::string ToJson() const;
};

// ==========================================================================
//  Repair
// ==========================================================================

// Whether the pin on one root may be rewritten from its current mount point.
YB_DEFINE_ENUM(FsRootRepinDecision,
    // The pin already names this root, or there is nothing to repair.
    (kNotNeeded)
    // The pin is stale and the superblocks under this root certify the current mount point. The
    // data belongs here and only the record is wrong, so rewriting it is provably safe.
    (kSafe)
    // The superblocks corroborate the old pin: this volume really did move, and its tablets will
    // not find their data at the current path whatever the pin says. Remount, do not repin.
    (kUnsafeVolumeMoved)
    // No usable superblock under this root, so neither conclusion can be proven. Rewriting would
    // record a mapping we cannot justify; an operator may still override.
    (kUnprovable));

FsRootRepinDecision DecideRepin(const FsRootVerdict& verdict);

// ==========================================================================
//  yb-fs_pin exit codes -- the contract with YugabyteDB Anywhere
// ==========================================================================

constexpr int kFsPinExitOk = 0;          // Every data root verified or benignly uncertified.
constexpr int kFsPinExitFailure = 1;     // A data root's layout is wrong. Block the upgrade.
constexpr int kFsPinExitCannotRun = 2;   // The check could not run. Unknown, not clean.
constexpr int kFsPinExitWarning = 3;     // Something is off, but do not block.

// Deliberately derived from Severity() and not from ShouldRefuseStartup(): the pre-upgrade gate
// blocks on anything wrong, including the states a running server is configured to tolerate.
int FsPinExitCodeForReport(const FsRootPinReport& report);

int FsPinExitCodeForSeverity(FsRootSeverity severity);

// ==========================================================================
//  Evaluation
// ==========================================================================

// False when the caller has not read tablet superblocks. Unpinned roots are then left kPending
// rather than certified or faulted, because "no evidence gathered" must not look like "no tablets".
YB_STRONGLY_TYPED_BOOL(FsRootEvidenceComplete);

struct FsRootPinInputs {
  std::string server_type;  // "tserver" or "master".

  // Canonicalized the way FsManager canonicalizes every root (see FsRootPin::mount_point).
  std::set<std::string> data_roots;
  std::set<std::string> wal_roots;

  std::map<std::string, FsRootPinFile> pin_files;  // Keyed by data root.
  std::vector<TabletSuperblockEvidence> evidence;
  FsRootEvidenceComplete evidence_complete = FsRootEvidenceComplete::kFalse;
  std::set<std::string> dropped_roots;

  // Roots with no instance file. When every root is in here the node is simply unformatted, which
  // is normal; when only some are, the rest of the node proves those roots should have had one.
  std::set<std::string> unformatted_roots;

  // Roots that sit on the same device as the root filesystem while at least one sibling data root
  // does not. That is what an unmounted mount point looks like from inside: the directory is real,
  // it is just on "/" instead of on its volume. Used only to sharpen the kUnformatted warning -
  // never to gate anything, because bind mounts, single-drive nodes and containers all make it
  // meaningless. Empty when the caller did not or could not determine it.
  std::set<std::string> likely_unmounted_roots;
};

// The whole decision, as a pure function of the inputs: no I/O, no clock, no flags. Returns one
// verdict for every entry of inputs.data_roots, sorted by root. The full decision table lives
// with the implementation in fs_root_pin.cc.
FsRootPinReport EvaluateFsRootPins(const FsRootPinInputs& inputs);

// ==========================================================================
//  I/O helpers
// ==========================================================================

// <root>/yb-data/<server_type>/fs-root-pin.json
std::string FsRootPinPath(const std::string& root, const std::string& server_type);

// Never fails: a missing file is kAbsent, unusable contents are kUnparseable, and anything else
// that goes wrong is kIoError with the reason in `error`. What to do about each is the caller's
// decision, not this function's.
FsRootPinFile ReadFsRootPinFile(Env* env, const std::string& pin_path);

// Atomic: temp file, fsync, rename, fsync the directory. An unusable pin refuses startup, so a torn
// write would otherwise brick the node. Sweeps any temp file a previous crash left behind.
Status WriteFsRootPinFile(Env* env, const std::string& pin_path, const FsRootPin& pin);

// Removes the pin and any temp file left by a crash mid-write. Missing files are not an error.
Status DeleteFsRootPinFile(Env* env, const std::string& pin_path);

// Maps any path under a root's yb-data tree back to the root:
//   /mnt/d0/yb-data/tserver/data            -> /mnt/d0
//   /mnt/d0/yb-data/tserver/wals            -> /mnt/d0
//   /mnt/d0/yb-data/tserver                 -> /mnt/d0
// Returns "" when the path has no yb-data component (including for an empty path).
std::string FsRootOfYbDataPath(const std::string& path);

}  // namespace yb
