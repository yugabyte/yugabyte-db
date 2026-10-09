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

#include "yb/fs/fs_root_pin.h"

#include <sys/stat.h>
#include <time.h>

#include <algorithm>
#include <sstream>
#include <string_view>

#ifdef __linux__
#include <dirent.h>
#endif

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

#include "yb/gutil/strings/join.h"

#include "yb/util/env.h"
#include "yb/util/env_util.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/jsonwriter.h"
#include "yb/util/logging.h"
#include "yb/util/path_util.h"
#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"

DEFINE_NON_RUNTIME_bool(fs_root_pin_enforce, true,
    "Refuse to start when a data root's pin file names a mount point other than the one the root "
    "is mounted at now, or when the pin cannot be parsed. This is the master refusal switch: with "
    "it false, nothing in this check refuses to start, including the case governed by "
    "--fs_root_pin_refuse_on_superblock_conflict. The diagnosis is logged identically either way, "
    "and whether pins are written is controlled separately by --fs_root_pin_write. Set it false "
    "to bring a node up while its mounts are still wrong, accepting that a node running on the "
    "wrong volumes will create replacement replicas on the wrong disks. To correct a pin that is "
    "merely stale after a deliberate relocation, run `yb-fs_pin repin` instead of disabling the "
    "check.");
TAG_FLAG(fs_root_pin_enforce, advanced);

DEFINE_NON_RUNTIME_bool(fs_root_pin_refuse_on_superblock_conflict, false,
    "Refuse to start when an unpinned data root holds tablet superblocks that name a different "
    "data root. Off by default because, unlike a pin mismatch, this fires purely from pre-existing "
    "on-disk state and so is the only check that can condemn a node on its first boot after a "
    "binary upgrade -- including nodes whose tablet directories were moved by hand during an "
    "earlier manual recovery. The condition is always reported, "
    "and always blocks the pre-upgrade check. Has no effect when --fs_root_pin_enforce is false.");
TAG_FLAG(fs_root_pin_refuse_on_superblock_conflict, advanced);

DEFINE_RUNTIME_AUTO_bool(fs_root_pin_write, kLocalPersisted, false, true,
    "Certify data roots whose tablet superblocks prove where they belong, by writing a pin file. "
    "Certification is evidence-gated, so this never records a layout the superblocks contradict; "
    "it is a separate flag from --fs_root_pin_enforce so that a warn-only deployment still "
    "accumulates the pins the strong check depends on. An AutoFlag (kLocalPersisted) so that "
    "servers start writing pin files only once the upgrade is finalized: a rollback to a version "
    "that does not know the pin file would otherwise trip that version's delete-then-recreate "
    "path, whose empty-directory precondition does not expect the leftover pin. Detection based "
    "on existing pins and on superblock evidence is active regardless of this flag; the explicit "
    "yb-fs_pin pin/repin commands write unconditionally, as deliberate operator actions.");
TAG_FLAG(fs_root_pin_write, advanced);

namespace yb {

namespace {

// Base name of the per-root pin file. Lives next to the `instance` file, at
// <root>/yb-data/<server_type>/fs-root-pin.json.
constexpr std::string_view kFsRootPinFileName = "fs-root-pin.json";

// Marker embedded in the name of the temp file that WriteFsRootPinFile writes and then renames
// into place (fs-root-pin.json.tmp.<random>), and that the sweep in DeleteFsRootPinFile and
// WriteFsRootPinFile matches when a crash between the write and the rename leaves one behind.
constexpr std::string_view kFsRootPinTempInfix = ".tmp.";

// A pin file is tiny and fixed-shape; anything larger is not one. The cap bounds what a stray,
// corrupt, or maliciously large file at this path can make the server read into memory during
// startup, and costs nothing when the file is legitimate.
constexpr size_t kMaxFsRootPinFileSize = 64 * 1024;

// How many tablets to name in the failure message before switching to a count. The message goes to
// a FATAL log line an operator has to read; the survey command prints the full list.
constexpr size_t kMaxTabletsInMessage = 10;

// How far up from a leaf we are willing to walk looking for the "yb-data" component. Every path
// this is used on has yb-data within three levels; the bound only stops a pathological input from
// spinning, it is not a statement about supported path depth.
constexpr int kMaxYbDataAncestorWalk = 16;

constexpr const char* kYbDataDirName = "yb-data";

std::string NowIso8601Utc() {
  const time_t now = time(nullptr);
  struct tm tm_utc;
  if (gmtime_r(&now, &tm_utc) == nullptr) {
    return std::string();
  }
  char buf[32];
  if (strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc) == 0) {
    return std::string();
  }
  return std::string(buf);
}

// Best-effort filesystem UUID for the volume `path` lives on. Empty when unavailable, which
// includes most containers; never fails.
std::string ResolveFilesystemUuid(const std::string& path) {
#ifdef __linux__
  // Best effort, and quietly gives up on any error: this value is a breadcrumb for an operator
  // matching the failure message to a device, and never gates anything. /dev/disk/by-uuid is not
  // mounted in most container deployments, so an empty result is the common case there.
  struct stat path_stat;
  if (stat(path.c_str(), &path_stat) != 0) {
    return std::string();
  }

  static constexpr const char* kByUuidDir = "/dev/disk/by-uuid";
  DIR* dir = opendir(kByUuidDir);
  if (dir == nullptr) {
    return std::string();
  }

  std::string result;
  struct dirent* entry = nullptr;
  while ((entry = readdir(dir)) != nullptr) {
    if (entry->d_name[0] == '.') {
      continue;
    }
    const std::string link = JoinPathSegments(kByUuidDir, entry->d_name);
    struct stat dev_stat;
    // stat() follows the symlink to the device node, whose st_rdev is the device id that the
    // mounted filesystem reports as st_dev.
    if (stat(link.c_str(), &dev_stat) != 0) {
      continue;
    }
    if (S_ISBLK(dev_stat.st_mode) && dev_stat.st_rdev == path_stat.st_dev) {
      result = entry->d_name;
      break;
    }
  }
  closedir(dir);
  return result;
#else
  return std::string();
#endif
}

std::string ReadStringField(const rapidjson::Value& obj, const char* name) {
  auto it = obj.FindMember(name);
  if (it == obj.MemberEnd() || !it->value.IsString()) {
    return std::string();
  }
  return std::string(it->value.GetString(), it->value.GetStringLength());
}

// "1 tablet superblock" / "2 tablet superblocks". This text is read by an operator in the middle
// of an incident; "1 tablet superblocks" is the kind of thing that makes them doubt the rest.
std::string Plural(size_t n, const char* noun) {
  return Format("$0 $1$2", n, noun, n == 1 ? "" : "s");
}

// True when the mismatched roots are a permutation of one another: every mismatched root's pin
// names another mismatched root, and no two name the same one. That is the signature of mounts
// exchanged among themselves, as opposed to one volume arriving from a different node.
bool MountsPermutedAmongThemselves(const std::vector<const FsRootVerdict*>& mismatched) {
  if (mismatched.size() < 2) {
    return false;
  }
  std::set<std::string> roots;
  for (const auto* v : mismatched) {
    roots.insert(v->root);
  }
  std::set<std::string> targets;
  for (const auto* v : mismatched) {
    if (roots.find(v->pin.mount_point) == roots.end()) {
      return false;
    }
    if (!targets.insert(v->pin.mount_point).second) {
      return false;
    }
  }
  return targets.size() == roots.size();
}

void AppendTabletLines(
    const std::vector<TabletSuperblockEvidence>& tablets, std::ostringstream* out) {
  const size_t shown = std::min(tablets.size(), kMaxTabletsInMessage);
  for (size_t i = 0; i < shown; ++i) {
    const auto& t = tablets[i];
    *out << "      tablet " << t.tablet_id << ": superblock says " << t.recorded_data_root
         << ", found under " << t.containing_root << "\n";
  }
  if (tablets.size() > shown) {
    *out << "      (and " << (tablets.size() - shown) << " more, not shown)\n";
  }
}

// Anomalies worth reporting on any root, whatever its state. Kept separate from the state so that
// a WAL anomaly or an unreadable superblock does not vanish from the output exactly when the root
// is also failing for another reason, which is when it is most likely to matter.
bool HasSideAnomalies(const FsRootVerdict& v) {
  return !v.unknown_wal_root_superblocks.empty() || !v.unreadable_superblocks.empty();
}

void AppendSideAnomalies(const FsRootVerdict& v, const char* indent, std::ostringstream* out) {
  if (!v.unreadable_superblocks.empty()) {
    *out << indent << Plural(v.unreadable_superblocks.size(), "tablet superblock") << " under "
         << v.root << " could not be read, so they are not evidence of anything:\n";
    const size_t shown = std::min(v.unreadable_superblocks.size(), kMaxTabletsInMessage);
    for (size_t i = 0; i < shown; ++i) {
      *out << indent << "    tablet " << v.unreadable_superblocks[i].tablet_id << ": "
           << v.unreadable_superblocks[i].read_error << "\n";
    }
    if (v.unreadable_superblocks.size() > shown) {
      *out << indent << "    (and " << (v.unreadable_superblocks.size() - shown) << " more)\n";
    }
  }
  if (!v.unknown_wal_root_superblocks.empty()) {
    *out << indent << v.root << ": "
         << Plural(v.unknown_wal_root_superblocks.size(), "tablet superblock")
         << " record a WAL root that is not in --fs_wal_dirs\n";
  }
}

}  // namespace

// ==========================================================================
//  FsRootPin
// ==========================================================================

std::string FsRootPin::ToJson() const {
  std::stringstream ss;
  JsonWriter w(&ss, JsonWriter::PRETTY);
  w.StartObject();
  w.String("mount_point");
  w.String(mount_point);
  w.String("pinned_at");
  w.String(pinned_at);
  if (!filesystem_uuid.empty()) {
    w.String("filesystem_uuid");
    w.String(filesystem_uuid);
  }
  w.EndObject();
  // A trailing newline so `cat` output is not glued to the shell prompt. JSON parsers ignore it.
  return ss.str() + "\n";
}

Result<FsRootPin> FsRootPin::ParseJson(const std::string& json) {
  rapidjson::Document doc;
  // Length-delimited, so an embedded NUL is seen as the corruption it is rather than silently
  // truncating the document at it.
  doc.Parse(json.data(), json.size());
  if (doc.HasParseError()) {
    return STATUS_FORMAT(
        Corruption, "not valid JSON: $0 (at offset $1)",
        rapidjson::GetParseError_En(doc.GetParseError()), doc.GetErrorOffset());
  }
  if (!doc.IsObject()) {
    return STATUS(Corruption, "not a JSON object");
  }

  FsRootPin pin;
  pin.mount_point = ReadStringField(doc, "mount_point");
  pin.pinned_at = ReadStringField(doc, "pinned_at");
  pin.filesystem_uuid = ReadStringField(doc, "filesystem_uuid");

  // mount_point is the only field that gates startup, so it is the only one we insist on. A pin
  // without it tells us nothing, and treating it as absent would let the next start re-certify
  // whatever layout happens to be current.
  if (pin.mount_point.empty()) {
    return STATUS(Corruption, "\"mount_point\" is missing, empty, or not a string");
  }
  if (pin.mount_point[0] != '/') {
    return STATUS_FORMAT(
        Corruption, "\"mount_point\" is not an absolute path: $0", pin.mount_point);
  }
  return pin;
}

FsRootPin FsRootPin::ForMountPoint(const std::string& mount_point) {
  FsRootPin pin;
  pin.mount_point = mount_point;
  pin.pinned_at = NowIso8601Utc();
  pin.filesystem_uuid = ResolveFilesystemUuid(mount_point);
  return pin;
}

// ==========================================================================
//  Severity and policy
// ==========================================================================

FsRootSeverity FsRootVerdict::Severity() const {
  switch (state) {
    case FsRootState::kMismatched:
    case FsRootState::kPinUnparseable:
    case FsRootState::kEvidenceConflict:
      return FsRootSeverity::kFailure;

    case FsRootState::kPinIoError:
    case FsRootState::kUnformatted:
      return FsRootSeverity::kWarning;

    case FsRootState::kVerified:
    case FsRootState::kCertifiable:
    case FsRootState::kUncertified:
    case FsRootState::kPending:
      // A superblock naming another root under a root whose pin matches is not a swap -- the pin
      // proves the volume did not move -- but something moved a tablet directory by hand and those
      // tablets will not find their data.
      if (!disagreeing_superblocks.empty() || HasSideAnomalies(*this)) {
        return FsRootSeverity::kWarning;
      }
      return FsRootSeverity::kOk;
  }
  return FsRootSeverity::kOk;
}

FsRootPinStartupPolicy FsRootPinPolicyFromFlags() {
  FsRootPinStartupPolicy policy;
  policy.refuse_on_pin_mismatch = FLAGS_fs_root_pin_enforce;
  // --fs_root_pin_enforce is the master refusal switch: with it off, warn-only mode tolerates
  // everything, a superblock conflict included.
  policy.refuse_on_superblock_conflict =
      FLAGS_fs_root_pin_enforce && FLAGS_fs_root_pin_refuse_on_superblock_conflict;
  policy.write_pins = FLAGS_fs_root_pin_write;
  return policy;
}

namespace {

// Whether one root's state stops the process under `policy`.
bool RefusesUnder(const FsRootVerdict& verdict, const FsRootPinStartupPolicy& policy) {
  switch (verdict.state) {
    case FsRootState::kMismatched:
    case FsRootState::kPinUnparseable:
      // A pin this feature wrote on an earlier boot disagrees with reality. This cannot fire on a
      // first upgrade, so enforcing it costs nothing that was not already broken.
      return policy.refuse_on_pin_mismatch;

    case FsRootState::kEvidenceConflict:
      return policy.refuse_on_superblock_conflict;

    case FsRootState::kPinIoError:
    case FsRootState::kUnformatted:
    case FsRootState::kVerified:
    case FsRootState::kCertifiable:
    case FsRootState::kUncertified:
    case FsRootState::kPending:
      return false;
  }
  return false;
}

}  // namespace

FsRootSeverity FsRootPinReport::Severity() const {
  auto worst = dropped_roots.empty() ? FsRootSeverity::kOk : FsRootSeverity::kWarning;
  for (const auto& v : verdicts) {
    worst = std::max(worst, v.Severity());
  }
  return worst;
}

bool FsRootPinReport::ShouldRefuseStartup(const FsRootPinStartupPolicy& policy) const {
  for (const auto& v : verdicts) {
    if (RefusesUnder(v, policy)) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> FsRootPinReport::CertifiableRoots() const {
  std::vector<std::string> roots;
  for (const auto& v : verdicts) {
    if (v.state == FsRootState::kCertifiable) {
      roots.push_back(v.root);
    }
  }
  return roots;
}

const FsRootVerdict* FsRootPinReport::FindRoot(const std::string& root) const {
  for (const auto& v : verdicts) {
    if (v.root == root) {
      return &v;
    }
  }
  return nullptr;
}

int FsPinExitCodeForSeverity(FsRootSeverity severity) {
  switch (severity) {
    case FsRootSeverity::kFailure: return kFsPinExitFailure;
    case FsRootSeverity::kWarning: return kFsPinExitWarning;
    case FsRootSeverity::kOk: return kFsPinExitOk;
  }
  return kFsPinExitCannotRun;
}

int FsPinExitCodeForReport(const FsRootPinReport& report) {
  return FsPinExitCodeForSeverity(report.Severity());
}

FsRootRepinDecision DecideRepin(const FsRootVerdict& verdict) {
  switch (verdict.state) {
    case FsRootState::kMismatched:
    case FsRootState::kPinUnparseable:
      // The pin disagrees with the mount point, or is corrupt. The superblocks decide which of the
      // two records is the stale one: if they name the root we are looking at, the data belongs
      // here and only the pin is wrong.
      if (verdict.superblocks_with_evidence == 0) {
        return FsRootRepinDecision::kUnprovable;
      }
      return verdict.disagreeing_superblocks.empty() ? FsRootRepinDecision::kSafe
                                                 : FsRootRepinDecision::kUnsafeVolumeMoved;

    case FsRootState::kEvidenceConflict:
      // No pin is involved: the superblocks themselves name another root. Writing a pin cannot make
      // those tablets find their data.
      return FsRootRepinDecision::kUnsafeVolumeMoved;

    case FsRootState::kPinIoError:
      // We could not read what is there. Overwriting it blind would destroy the only record.
      return FsRootRepinDecision::kUnprovable;

    case FsRootState::kVerified:
    case FsRootState::kCertifiable:
    case FsRootState::kUncertified:
    case FsRootState::kPending:
    case FsRootState::kUnformatted:
      return FsRootRepinDecision::kNotNeeded;
  }
  return FsRootRepinDecision::kNotNeeded;
}

// ==========================================================================
//  Messages
// ==========================================================================

std::string FsRootPinReport::FailureMessage(Audience audience) const {
  if (!HasFailure()) {
    return std::string();
  }

  std::ostringstream out;
  // Leading newline: this text reaches the log behind up to two RETURN_NOT_OK_PREPEND prefixes, and
  // the headline has to start its own line to be readable.
  //
  // Whether this ends in a refusal is the caller's decision (see --fs_root_pin_enforce), so the
  // text states the diagnosis and the remedy but does not announce an action.
  out << "\nData volumes are not mounted where they were.\n\n";

  std::vector<const FsRootVerdict*> mismatched;
  bool any_repin_safe = false;
  for (const auto& v : verdicts) {
    if (DecideRepin(v) == FsRootRepinDecision::kSafe) {
      any_repin_safe = true;
    }
    switch (v.state) {
      case FsRootState::kMismatched:
        mismatched.push_back(&v);
        out << "  " << v.root << " holds the volume pinned at " << v.pin.mount_point << "\n";
        out << "      pin file " << v.pin_path;
        if (!v.pin.pinned_at.empty()) {
          out << ", pinned at " << v.pin.pinned_at;
        }
        if (!v.pin.filesystem_uuid.empty()) {
          out << ", filesystem UUID " << v.pin.filesystem_uuid;
        }
        out << "\n";
        if (v.superblocks_seen > 0) {
          out << "      " << Plural(v.superblocks_seen, "tablet superblock")
              << " under this root\n";
        }
        if (!v.disagreeing_superblocks.empty()) {
          out << "      " << v.disagreeing_superblocks.size()
              << " of them also record a different data root:\n";
          AppendTabletLines(v.disagreeing_superblocks, &out);
        } else if (v.superblocks_with_evidence > 0) {
          out << "      but every superblock under it names this root, so the data belongs here\n"
              << "      and it is the pin that is stale\n";
        }
        AppendSideAnomalies(v, "      ", &out);
        break;

      case FsRootState::kPinUnparseable:
        out << "  " << v.root << " has a pin file whose contents are unusable\n";
        out << "      " << v.pin_path << ": " << v.read_error << "\n";
        out << "      Treating it as unpinned would re-certify whatever layout is current and\n"
            << "      hide a swap, so it is a failure rather than a fresh start.\n";
        AppendSideAnomalies(v, "      ", &out);
        break;

      case FsRootState::kEvidenceConflict:
        out << "  " << v.root << " has no pin file, and " << v.disagreeing_superblocks.size()
            << " of "
            << Plural(v.superblocks_with_evidence, "tablet superblock")
            << " under it record a different data root\n";
        AppendTabletLines(v.disagreeing_superblocks, &out);
        out << "      Not writing a pin: the evidence contradicts the layout we would record.\n";
        AppendSideAnomalies(v, "      ", &out);
        break;

      case FsRootState::kVerified:
      case FsRootState::kCertifiable:
      case FsRootState::kUncertified:
      case FsRootState::kPending:
      case FsRootState::kPinIoError:
      case FsRootState::kUnformatted:
        break;
    }
  }

  if (MountsPermutedAmongThemselves(mismatched)) {
    std::vector<std::string> names;
    for (const auto* v : mismatched) {
      names.push_back(v->root);
    }
    out << "\n";
    if (names.size() == 2) {
      out << names[0] << " and " << names[1] << " appear to be swapped.\n";
    } else {
      out << JoinStrings(names, ", ")
          << " appear to have been remounted onto each other's mount points.\n";
    }
  }

  if (!dropped_roots.empty()) {
    out << "\nThis report is incomplete: " << JoinStrings(dropped_roots, ", ")
        << " could not be opened, so tablets on those roots were not examined.\n";
  }

  out << "\nRemount the data volumes where they were and restart. No data has been lost: every\n"
      << "file moved with its volume, and only the absolute paths recorded in tablet metadata\n"
      << "disagree with the mounts. Do not delete anything, and in particular do not remove\n"
      << "duplicate tablet directories.\n";

  if (any_repin_safe) {
    out << "\nOne or more roots above hold data that does belong to them, with only a stale pin\n"
        << "recording otherwise. `yb-fs_pin repin` rewrites exactly those pins, and refuses the\n"
        << "roots whose superblocks show the volume really moved.\n";
  }
  if (audience == Audience::kServerLog) {
    out << "\nRun `yb-fs_pin survey` for the full per-tablet detail.\n";
  }
  return out.str();
}

std::string FsRootPinReport::WarningMessage() const {
  if (Severity() == FsRootSeverity::kOk) {
    return std::string();
  }

  std::ostringstream out;
  for (const auto& v : verdicts) {
    switch (v.state) {
      case FsRootState::kUnformatted:
        out << "  " << v.root << " has no instance file while other roots do. It was never\n"
            << "  formatted, was wiped, is a blank or newly added volume, or -- the case worth\n"
            << "  ruling out first -- its volume did not mount, leaving an empty directory on\n"
            << "  the root filesystem.\n";
        if (v.likely_unmounted) {
          out << "  It is on the same device as / while its sibling data roots are not, which is\n"
              << "  what an unmounted mount point looks like from in here. Treat that as the\n"
              << "  likely explanation unless you know this disk was just replaced or added.\n";
        }
        // Say what is about to happen, not merely what was observed. Startup does not refuse on
        // this state, so CreateFileSystemRoots() is about to adopt the root by writing a fresh
        // instance file into it -- right for a genuine disk replacement, exactly wrong for a
        // volume that failed to mount, and irreversible in the sense that the evidence of the
        // original state is gone.
        out << "  This root will be adopted as a new, empty data directory on this start. If its\n"
            << "  volume simply failed to mount, stop the server now and mount it.\n";
        break;
      case FsRootState::kPinIoError:
        out << "  " << v.root << ": pin file " << v.pin_path << " could not be read: "
            << v.read_error << "\n"
            << "  Leaving it alone: it is neither trusted nor replaced, and this root will not\n"
            << "  be certified while the fault persists.\n";
        if (!v.disagreeing_superblocks.empty()) {
          out << "  " << v.disagreeing_superblocks.size() << " of "
              << Plural(v.superblocks_with_evidence, "tablet superblock")
              << " under it record a different data root. With the pin unreadable, whether this\n"
              << "  volume is mounted where it belongs cannot be verified.\n";
          AppendTabletLines(v.disagreeing_superblocks, &out);
        }
        break;
      default:
        break;
    }
    // Only kVerified establishes the claim below: the pin was read and it names this root. Other
    // warning-severity states (kPinIoError, kUnformatted) can also carry disagreeing superblocks,
    // but there the pin was unreadable or absent, and "the volume has not moved" is not known.
    if (!v.disagreeing_superblocks.empty() && v.state == FsRootState::kVerified) {
      out << "  " << v.root << " is pinned here and the volume has not moved, but "
          << v.disagreeing_superblocks.size() << " of "
          << Plural(v.superblocks_with_evidence, "tablet superblock")
          << " under it record a different data root. Something moved tablet\n"
          << "  directories by hand; those tablets will not find their data.\n";
      AppendTabletLines(v.disagreeing_superblocks, &out);
    }
    // Side anomalies are reported for every root that FailureMessage() does not already cover.
    // A failing root gets them there, next to its own diagnosis, so repeating them here would
    // double them up now that both messages reach the log.
    if (v.Severity() != FsRootSeverity::kFailure) {
      AppendSideAnomalies(v, "  ", &out);
    }
  }
  if (!dropped_roots.empty()) {
    out << "  " << JoinStrings(dropped_roots, ", ")
        << " could not be opened and were not examined; a clean verdict here is not a clean node\n";
  }
  return out.str();
}

std::string FsRootPinReport::SummaryLine() const {
  size_t verified = 0, certifiable = 0, uncertified = 0, pending = 0, failing = 0, warning = 0;
  for (const auto& v : verdicts) {
    switch (v.state) {
      case FsRootState::kVerified: ++verified; break;
      case FsRootState::kCertifiable: ++certifiable; break;
      case FsRootState::kUncertified: ++uncertified; break;
      case FsRootState::kPending: ++pending; break;
      case FsRootState::kPinIoError:
      case FsRootState::kUnformatted: ++warning; break;
      case FsRootState::kMismatched:
      case FsRootState::kPinUnparseable:
      case FsRootState::kEvidenceConflict: ++failing; break;
    }
  }
  return Format(
      "data root pins: $0 roots, $1 verified, $2 certifiable, $3 uncertified (no tablets), "
      "$4 not yet checked, $5 needing attention, $6 with a wrong layout",
      verdicts.size(), verified, certifiable, uncertified, pending, warning, failing);
}

std::string FsRootPinReport::ToJson() const {
  std::stringstream ss;
  JsonWriter w(&ss, JsonWriter::PRETTY);
  w.StartObject();

  w.String("severity");
  w.String(ToString(Severity()));
  w.String("has_failure");
  w.Bool(HasFailure());
  w.String("exit_code");
  w.Int(FsPinExitCodeForReport(*this));
  w.String("summary");
  w.String(SummaryLine());

  w.String("roots");
  w.StartArray();
  for (const auto& v : verdicts) {
    w.StartObject();
    w.String("root");
    w.String(v.root);
    w.String("state");
    w.String(ToString(v.state));
    w.String("severity");
    w.String(ToString(v.Severity()));
    w.String("repin");
    w.String(ToString(DecideRepin(v)));
    w.String("pin_path");
    w.String(v.pin_path);
    if (!v.pin.mount_point.empty()) {
      w.String("pinned_mount_point");
      w.String(v.pin.mount_point);
    }
    if (!v.pin.pinned_at.empty()) {
      w.String("pinned_at");
      w.String(v.pin.pinned_at);
    }
    if (!v.pin.filesystem_uuid.empty()) {
      w.String("filesystem_uuid");
      w.String(v.pin.filesystem_uuid);
    }
    if (!v.read_error.empty()) {
      w.String("read_error");
      w.String(v.read_error);
    }
    w.String("superblocks_seen");
    w.Uint64(v.superblocks_seen);
    w.String("superblocks_with_evidence");
    w.Uint64(v.superblocks_with_evidence);

    const auto write_tablets = [&w](const char* name,
                                    const std::vector<TabletSuperblockEvidence>& tablets) {
      w.String(name);
      w.StartArray();
      for (const auto& t : tablets) {
        w.StartObject();
        w.String("tablet_id");
        w.String(t.tablet_id);
        w.String("containing_root");
        w.String(t.containing_root);
        if (!t.recorded_data_root.empty()) {
          w.String("recorded_data_root");
          w.String(t.recorded_data_root);
        }
        if (!t.recorded_wal_root.empty()) {
          w.String("recorded_wal_root");
          w.String(t.recorded_wal_root);
        }
        if (!t.read_error.empty()) {
          w.String("read_error");
          w.String(t.read_error);
        }
        w.EndObject();
      }
      w.EndArray();
    };
    write_tablets("disagreeing_superblocks", v.disagreeing_superblocks);
    write_tablets("unknown_wal_root_superblocks", v.unknown_wal_root_superblocks);
    write_tablets("unreadable_superblocks", v.unreadable_superblocks);
    w.EndObject();
  }
  w.EndArray();

  w.String("dropped_roots");
  w.StartArray();
  for (const auto& root : dropped_roots) {
    w.String(root);
  }
  w.EndArray();

  w.EndObject();
  return ss.str();
}

// ==========================================================================
//  The decision
// ==========================================================================

// The full table, per root:
//
//   pin file       | usable superblocks under the root      | state
//   ---------------|----------------------------------------|-------------------
//   present, match | any                                    | kVerified
//   present, other | any                                    | kMismatched
//   unparseable    | any                                    | kPinUnparseable
//   unreadable     | any                                    | kPinIoError
//   absent         | >= 1, all name this root               | kCertifiable
//   absent         | >= 1, any names another root           | kEvidenceConflict
//   absent         | 0                                      | kUncertified
//   absent         | evidence not gathered                  | kPending
//
// A root with no instance file short-circuits to kUnformatted, unless no root has one.
FsRootPinReport EvaluateFsRootPins(const FsRootPinInputs& inputs) {
  FsRootPinReport report;
  report.dropped_roots = inputs.dropped_roots;

  // A node where no root has an instance file has simply never been formatted, which is the normal
  // state on a first boot. It is only when the siblings have one that a missing instance file says
  // something about this root.
  const bool any_root_formatted = inputs.unformatted_roots.size() < inputs.data_roots.size();

  // Bucket the evidence by the root each superblock was found under.
  std::map<std::string, std::vector<const TabletSuperblockEvidence*>> evidence_by_root;
  for (const auto& e : inputs.evidence) {
    evidence_by_root[e.containing_root].push_back(&e);
  }

  for (const auto& root : inputs.data_roots) {
    FsRootVerdict v;
    v.root = root;
    v.pin_path = FsRootPinPath(root, inputs.server_type);

    auto ev_it = evidence_by_root.find(root);
    if (ev_it != evidence_by_root.end()) {
      for (const auto* e : ev_it->second) {
        ++v.superblocks_seen;
        if (!e->read_error.empty()) {
          v.unreadable_superblocks.push_back(*e);
          continue;
        }
        if (e->recorded_data_root.empty()) {
          // Superblock located but not parsed, or a tombstoned tablet that carries no rocksdb dir.
          // Either way it is not evidence of anything.
          continue;
        }
        ++v.superblocks_with_evidence;
        if (e->recorded_data_root != root) {
          v.disagreeing_superblocks.push_back(*e);
        }
        if (!e->recorded_wal_root.empty() &&
            inputs.wal_roots.find(e->recorded_wal_root) == inputs.wal_roots.end()) {
          v.unknown_wal_root_superblocks.push_back(*e);
        }
      }
    }

    auto pin_it = inputs.pin_files.find(root);
    const FsRootPinFile* pin_file = (pin_it == inputs.pin_files.end()) ? nullptr : &pin_it->second;
    const auto file_state = pin_file ? pin_file->state : FsRootPinFileState::kAbsent;

    // Missing instance file, siblings have one: nothing below can be trusted about this root, and
    // in particular an empty directory must not read as "a root with no tablets". Checked after the
    // pin state rather than before, so that a volume that was both swapped and wiped is still
    // reported as the swap it is -- the pin is the stronger signal and must not be masked.
    if (file_state == FsRootPinFileState::kAbsent && any_root_formatted &&
        inputs.unformatted_roots.find(root) != inputs.unformatted_roots.end()) {
      v.state = FsRootState::kUnformatted;
      v.likely_unmounted = inputs.likely_unmounted_roots.contains(root);
      report.verdicts.push_back(std::move(v));
      continue;
    }

    switch (file_state) {
      case FsRootPinFileState::kUnparseable:
        v.state = FsRootState::kPinUnparseable;
        v.read_error = pin_file->error;
        break;

      case FsRootPinFileState::kIoError:
        v.state = FsRootState::kPinIoError;
        v.read_error = pin_file->error;
        break;

      case FsRootPinFileState::kPresent:
        v.pin = pin_file->pin;
        // The pin is the strong evidence: it says which mount point this physical volume was
        // certified at. If it names this root, the volume has not moved, full stop -- a superblock
        // that disagrees is a separate (hand-moved directory) problem, reported as a warning.
        v.state = (pin_file->pin.mount_point == root) ? FsRootState::kVerified
                                                      : FsRootState::kMismatched;
        break;

      case FsRootPinFileState::kAbsent:
        if (!inputs.evidence_complete) {
          // The FsManager pass runs before tablets are enumerated. "No evidence gathered" must not
          // be mistaken for "no tablets", so leave the decision to the later pass.
          v.state = FsRootState::kPending;
        } else if (v.superblocks_with_evidence == 0) {
          // With no tablets there is no evidence the root is where it belongs, and we will not
          // record a mapping we cannot prove. Nothing is at risk: a swap can only cause harm if
          // data moved.
          v.state = FsRootState::kUncertified;
        } else if (v.disagreeing_superblocks.empty()) {
          v.state = FsRootState::kCertifiable;
        } else {
          v.state = FsRootState::kEvidenceConflict;
        }
        break;
    }

    report.verdicts.push_back(std::move(v));
  }

  // data_roots is a std::set, so verdicts come out sorted by root already.
  return report;
}

// ==========================================================================
//  I/O
// ==========================================================================

std::string FsRootPinPath(const std::string& root, const std::string& server_type) {
  return JoinPathSegments(GetServerTypeDataPath(root, server_type), kFsRootPinFileName);
}

FsRootPinFile ReadFsRootPinFile(Env* env, const std::string& pin_path) {
  FsRootPinFile result;

  // Deliberately no FileExists() precheck: that is access(F_OK) with no errno inspection, so a pin
  // that exists but cannot be reached (EACCES on the parent, ELOOP, ENAMETOOLONG) would report as
  // absent, and a root with clean evidence would then overwrite it with the current mount point --
  // the same "bless whatever happens to be mounted" hazard that kPinUnparseable exists to close.
  // Reading and classifying the failure distinguishes ENOENT from everything else.
  auto size = env->GetFileSize(pin_path);
  if (!size.ok()) {
    result.state = size.status().IsNotFound() ? FsRootPinFileState::kAbsent
                                              : FsRootPinFileState::kIoError;
    if (result.state == FsRootPinFileState::kIoError) {
      result.error = size.status().ToString(/* include_file_and_line = */ false);
    }
    return result;
  }
  if (*size > kMaxFsRootPinFileSize) {
    result.state = FsRootPinFileState::kUnparseable;
    result.error = Format(
        "file is $0 bytes, far larger than any pin file (cap is $1)", *size,
        kMaxFsRootPinFileSize);
    return result;
  }

  faststring contents;
  Status s = ReadFileToString(env, pin_path, &contents);
  if (!s.ok()) {
    result.state = s.IsNotFound() ? FsRootPinFileState::kAbsent : FsRootPinFileState::kIoError;
    if (result.state == FsRootPinFileState::kIoError) {
      result.error = s.ToString(/* include_file_and_line = */ false);
    }
    return result;
  }

  auto pin = FsRootPin::ParseJson(contents.ToString());
  if (!pin.ok()) {
    result.state = FsRootPinFileState::kUnparseable;
    result.error = pin.status().message().ToBuffer();
    return result;
  }

  result.state = FsRootPinFileState::kPresent;
  result.pin = *pin;
  return result;
}

namespace {

// Removes any <pin_path>.tmp.* left behind by a crash between the temp write and the rename. Such a
// file is invisible to DeleteFileSystemLayout's exact-path removal and would fail the
// IsDirectoryEmpty precondition of CreateInitialFileSystemLayout, breaking exactly the
// delete-then-recreate cycle that removing the pin exists to protect.
Status SweepFsRootPinTempFiles(Env* env, const std::string& pin_path) {
  const auto dir = DirName(pin_path);
  const auto prefix = Format("$0$1", BaseName(pin_path), kFsRootPinTempInfix);
  auto children = env->GetChildren(dir, ExcludeDots::kTrue);
  if (!children.ok()) {
    // The directory not being there is not a leftover-temp-file problem.
    return children.status().IsNotFound() ? Status::OK() : children.status();
  }
  for (const auto& child : *children) {
    if (child.rfind(prefix, 0) == 0) {
      const auto stale = JoinPathSegments(dir, child);
      LOG(INFO) << "Removing leftover data root pin temp file " << stale;
      RETURN_NOT_OK_PREPEND(env->DeleteFile(stale), "Unable to remove " + stale);
    }
  }
  return Status::OK();
}

}  // namespace

Status WriteFsRootPinFile(Env* env, const std::string& pin_path, const FsRootPin& pin) {
  WARN_NOT_OK(SweepFsRootPinTempFiles(env, pin_path), "Could not sweep stale pin temp files");

  const std::string json = pin.ToJson();

  // Temp file, fsync, rename, fsync the directory. Unusable pin contents refuse startup, so a torn
  // write would otherwise brick the node on the next restart.
  const auto tmp_template = Format("$0$1XXXXXX", pin_path, kFsRootPinTempInfix);
  std::string tmp_path;
  std::unique_ptr<WritableFile> file;
  RETURN_NOT_OK_PREPEND(
      env->NewTempWritableFile(WritableFileOptions(), tmp_template, &tmp_path, &file),
      "Unable to create temp file for " + pin_path);
  env_util::ScopedFileDeleter tmp_deleter(env, tmp_path);

  RETURN_NOT_OK_PREPEND(file->Append(Slice(json)), "Failed to write " + tmp_path);
  RETURN_NOT_OK_PREPEND(file->Sync(), "Failed to Sync() " + tmp_path);
  RETURN_NOT_OK_PREPEND(file->Close(), "Failed to Close() " + tmp_path);
  RETURN_NOT_OK_PREPEND(env->RenameFile(tmp_path, pin_path), "Failed to rename onto " + pin_path);
  tmp_deleter.Cancel();
  RETURN_NOT_OK_PREPEND(
      env->SyncDir(DirName(pin_path)), "Failed to SyncDir() parent of " + pin_path);
  return Status::OK();
}

Status DeleteFsRootPinFile(Env* env, const std::string& pin_path) {
  RETURN_NOT_OK(SweepFsRootPinTempFiles(env, pin_path));
  const auto s = env->DeleteFile(pin_path);
  return s.IsNotFound() ? Status::OK() : s;
}

std::string FsRootOfYbDataPath(const std::string& path) {
  if (path.empty()) {
    return std::string();
  }
  std::string cur = path;
  for (int i = 0; i < kMaxYbDataAncestorWalk; ++i) {
    if (BaseName(cur) == kYbDataDirName) {
      return DirName(cur);
    }
    std::string parent = DirName(cur);
    if (parent == cur) {
      break;  // Reached "/" or a relative leaf; DirName is a fixed point there.
    }
    cur = std::move(parent);
  }
  return std::string();
}

}  // namespace yb
