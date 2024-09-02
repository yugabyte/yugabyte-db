// Copyright (c) YugaByte, Inc.
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
#include <iostream>

#include "yb/master/catalog_entity_info.pb.h"
#include "yb/master/catalog_entity_parser.h"
#include "yb/master/master_backup.pb.h"
#include "yb/master/master_types.pb.h"

#include "yb/consensus/consensus.h"
#include "yb/consensus/log.h"
#include "yb/consensus/log_anchor_registry.h"

#include "yb/gutil/bind.h"
#include "yb/gutil/strings/util.h"

#include "yb/master/master_options.h"
#include "yb/master/sys_catalog.h"

#include "yb/server/logical_clock.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_bootstrap_if.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tools/tool_arguments.h"

#include "yb/util/date_time.h"
#include "yb/util/jsonreader.h"
#include "yb/util/logging.h"
#include "yb/util/pb_util.h"
#include "yb/util/string_trim.h"
#include "yb/util/string_util.h"
#include "yb/util/version_info.h"

DEFINE_RUNTIME_bool(show_raw_id, false, "Print binary id in raw format also");

DECLARE_string(fs_data_dirs);

namespace po = boost::program_options;

using google::protobuf::Descriptor;
using google::protobuf::EnumDescriptor;
using google::protobuf::FieldDescriptor;
using google::protobuf::Message;

using std::array;
using std::endl;
using std::flush;
using std::string;
using std::unique_ptr;
using std::vector;
using std::unordered_set;
using std::unordered_map;

namespace yb {

using master::SysRowEntryType;

namespace tools {

static constexpr const char* const kSectionSeparator = "----------------------------------------"
                                                       "----------------------------------------";

// Output streams.
auto& sinfo = std::cerr;
auto& serr  = std::cerr;
auto& sjson = std::cout;

// ------------------------------------------------------------------------------------------------
// Single peer mini SysCatalogTable for SysCatalog handling in tools.
// ------------------------------------------------------------------------------------------------
class MiniSysCatalogTable;
using MiniSysCatalogTablePtr = unique_ptr<MiniSysCatalogTable>;

class MiniSysCatalogTable {
 public:
  static Result<MiniSysCatalogTablePtr> Load(FsManager* fs_manager);

  tablet::TabletPtr tablet() const { return tablet_; }

  const consensus::ConsensusMetadataPB& ConsensusPB() const {
    return consensus_metadata_->GetConsensusMetadataPB();
  }

 private:
  MiniSysCatalogTable();

  Status LoadTablet(FsManager* fs_manager);

  Status LoadConsensusMetadata(FsManager* fs_manager, const string& tablet_id);

  Status OpenTablet(const scoped_refptr<tablet::RaftGroupMetadata>& metadata,
                    const Schema& expected_schema);

  scoped_refptr<server::Clock> clock_;
  unique_ptr<ThreadPool> thread_pool_;

  unique_ptr<MetricRegistry> metric_registry_;
  unique_ptr<consensus::ConsensusMetadata> consensus_metadata_;
  std::promise<client::YBClient*> client_promise_;
  const tablet::TabletOptions tablet_options_;

  tablet::TabletPtr tablet_;
  tablet::TabletPeerPtr tablet_peer_;
};

Result<MiniSysCatalogTablePtr> MiniSysCatalogTable::Load(FsManager* fs_manager) {
  MiniSysCatalogTablePtr ret(new MiniSysCatalogTable);
  RETURN_NOT_OK(ret->LoadTablet(fs_manager));
  return ret;
}

MiniSysCatalogTable::MiniSysCatalogTable()
    : clock_(server::LogicalClock::CreateStartingAt(HybridTime::kInitial)),
      metric_registry_(new MetricRegistry) {
  CHECK_OK(ThreadPoolBuilder("pool").set_min_threads(1).Build(&thread_pool_));
}

Status MiniSysCatalogTable::LoadTablet(FsManager* fs_manager) {
  LOG(INFO) << "Trying to load previous SysCatalogTable data from disk";
  // Load Metadata Information from disk.
  auto metadata = VERIFY_RESULT(tablet::RaftGroupMetadata::Load(
      DCHECK_NOTNULL(fs_manager), master::kSysCatalogTabletId));

  const Schema expected_schema = master::SysCatalogTable::BuildTableSchema();
  // Verify that the schema is the current one.
  if (!metadata->schema()->Equals(expected_schema)) {
    return STATUS(Corruption, "Unexpected schema", metadata->schema()->ToString());
  }

  // Check if partition schema is from old SysCatalogTable.
  // Current SysCatalogTable must be non-partitioned.
  if (metadata->partition_schema()->IsHashPartitioning()) {
    return STATUS(
        Corruption, "SysCatalog Table must be non-partitioned", metadata->schema()->ToString());
  }

  // Optional loading of the tablet Consensus Metadata & checks.
  const string tablet_id = metadata->raft_group_id();
  const Status s = LoadConsensusMetadata(fs_manager, tablet_id);
  ERROR_NOT_OK(s, "Unable to load consensus metadata for tablet " + tablet_id);

  return OpenTablet(metadata, expected_schema);
}

Status MiniSysCatalogTable::LoadConsensusMetadata(FsManager* fs_manager, const string& tablet_id) {
  LOG(INFO) << "Loading Consensus Metadata for tablet ID: " << tablet_id;
  RETURN_NOT_OK(consensus::ConsensusMetadata::Load(
      fs_manager, tablet_id, fs_manager->uuid(), &consensus_metadata_));

  const consensus::RaftConfigPB& loaded_config = consensus_metadata_->active_config();
  // Some peer consensus metadata optional checks.
  if (loaded_config.peers().size() == 1) {
    LOG(INFO) << "Checking consensus for local operation...";
    // We know we have exactly one peer.
    const auto& peer = loaded_config.peers().Get(0);
    LOG_IF(ERROR, !peer.has_permanent_uuid())
        << "Loaded consensus metadata, but peer did not have a uuid";
    LOG_IF(ERROR, peer.permanent_uuid() != fs_manager->uuid())
        << "Loaded consensus metadata, but peer uuid (" << peer.permanent_uuid()
        << ") was different than our uuid (" << fs_manager->uuid() << ")";
  } else if (loaded_config.peers().empty()) {
    LOG(ERROR) << "Loaded consensus metadata, but had no peers";
  } else {
    LOG(INFO) << "Loaded consensus metadata - got " << loaded_config.peers().size() << " peers";
  }

  return Status::OK();
}

Status MiniSysCatalogTable::OpenTablet(const scoped_refptr<tablet::RaftGroupMetadata>& metadata,
                                       const Schema& expected_schema) {
  struct DoNothing {
    static void fn(std::shared_ptr<StateChangeContext>) {}
  };
  std::shared_future<client::YBClient*> client_future = client_promise_.get_future();
  tablet_peer_ = std::make_shared<tablet::TabletPeer>(
      metadata,
      consensus::RaftPeerPB(),
      clock_,
      metadata->fs_manager()->uuid(),
      Bind(&DoNothing::fn),
      metric_registry_.get(),
      nullptr, // tablet_splitter
      client_future);

  tablet::TabletInitData tablet_init_data = {
      .metadata = metadata,
      .client_future = client_future,
      .clock = clock_,
      .parent_mem_tracker = nullptr,
      .block_based_table_mem_tracker = nullptr,
      .metric_registry = metric_registry_.get(),
      .log_anchor_registry = nullptr,
      .tablet_options = tablet_options_,
      .log_prefix_suffix = "",
      .transaction_participant_context = tablet_peer_.get(),
      .local_tablet_filter = nullptr,
      .transaction_coordinator_context = nullptr,
      // Curently TabletBootstrap::PlaySegments() fails if txns_enabled == false.
      .txns_enabled = tablet::TransactionsEnabled::kTrue,
      .is_sys_catalog = tablet::IsSysCatalogTablet::kTrue,
      .snapshot_coordinator = nullptr,
      .tablet_splitter = nullptr,
      .allowed_history_cutoff_provider = nullptr,
      .transaction_manager_provider = nullptr,
      .auto_flags_manager = nullptr,
      .full_compaction_pool = nullptr,
      .admin_triggered_compaction_pool = nullptr,
      .post_split_compaction_added = nullptr,
      .metadata_cache = nullptr,
  };

  tablet::BootstrapTabletData data = {
      .tablet_init_data = tablet_init_data,
      .listener = tablet_peer_->status_listener(),
      .append_pool = thread_pool_.get(),
      .allocation_pool = thread_pool_.get(),
      .log_sync_pool = thread_pool_.get(),
  };

  scoped_refptr<log::Log> log;
  consensus::ConsensusBootstrapInfo consensus_info;
  RETURN_NOT_OK(BootstrapTablet(data, &tablet_, &log, &consensus_info));

  if (!tablet_->schema()->Equals(expected_schema)) {
    return STATUS(Corruption, "Unexpected schema", tablet_->schema()->ToString());
  }

  return Status::OK();
}

// ------------------------------------------------------------------------------------------------
// Helper exception with a error Status inside.
// ------------------------------------------------------------------------------------------------
class StatusException : public std::exception {
 public:
  explicit StatusException(const Status& s) : status_(s), what_(s.ToString()) {}
  virtual ~StatusException() throw() = default;
  const char* what() const throw() override {
    return what_.c_str();
  }
  const Status& status() const throw() {
    return status_;
  }
 private:
  const Status status_;
  const string what_;
};

Status IsValid_SysRowEntryType(int entry_type) {
  SCHECK_FORMAT(master::SysRowEntryType_IsValid(entry_type),
                InvalidArgument, "Unknown entry type $0", entry_type);
  return Status::OK();
}

Result<SysRowEntryType> Parse_SysRowEntryType(const string& type_name) {
  SysRowEntryType type = SysRowEntryType::UNKNOWN;
  SCHECK(master::SysRowEntryType_Parse(type_name, &type),
      InvalidArgument, "Unknown entry type: " + type_name);
  return type;
}

// ------------------------------------------------------------------------------------------------
// Flags and filtering helpers.
// ------------------------------------------------------------------------------------------------
YB_DEFINE_ENUM(OptionFlag,
    // Flags for filtering.
    (kUseNestedFiltering)
    // Show/hide sections.
    (kNoHeader)(kNoSysCatalog)(kNoFooter)
    // Options in HEADER section.
    (kNoYBVersionStr)(kNoYBVersionPB)(kNoDescriptors)(kNoRaft)(kNoConsensus)
    // Options in Entry.
    (kNoHexData)(kNoRawData)(kNoDataPB)(kNoData)(kNoAction)
    // For 'show-changes' command.
    (kShowNoOp));

using ReadFlags = EnumBitSet<OptionFlag>;

class ReadFilters {
 public:
  ReadFilters(unordered_set<ObjectId>&& include_ids,
              unordered_set<ObjectId>&& exclude_ids,
              unordered_set<SysRowEntryType>&& skip_types,
              ReadFlags&& flags) :
      include_ids_(include_ids), exclude_ids_(exclude_ids),
      skip_types_(skip_types), flags_(flags) {}

  explicit ReadFilters(ReadFlags&& flags) : flags_(flags) {}

  bool NeedToShow(SysRowEntryType type, const ObjectId& id) const;

  Result<bool> NeedToShow(int8_t entry_type, const Slice& id) const {
    RETURN_NOT_OK(IsValid_SysRowEntryType(entry_type));
    return NeedToShow(static_cast<SysRowEntryType>(entry_type), id.ToBuffer());
  }

  bool IsSet(OptionFlag flag) const {
    return flags_.Test(flag);
  }

  bool IsNotSet(OptionFlag flag) const {
    return !IsSet(flag);
  }

  string ToString() const {
    return YB_STRUCT_TO_STRING(include_ids_, exclude_ids_, skip_types_, flags_);
  }

 private:
  unordered_set<ObjectId> include_ids_;
  unordered_set<ObjectId> exclude_ids_;
  unordered_set<SysRowEntryType> skip_types_;
  ReadFlags flags_;
};

bool ReadFilters::NeedToShow(SysRowEntryType type, const ObjectId& id) const {
  // Including by id.
  if (include_ids_.find(id) != include_ids_.end()) {
    return true;
  }

  // Including by decoded id.
  ObjectId dec_id;
  if (!IsPrint(id)) {
    const Uuid txn_id = Uuid::TryFullyDecode(id);
    if (!txn_id.IsNil()) {
      dec_id = txn_id.ToString();
      if (include_ids_.find(dec_id) != include_ids_.end()) {
        return true;
      }
    }
  }

  if (skip_types_.find(type) != skip_types_.end() || // Excluding by type.
      exclude_ids_.find(id) != exclude_ids_.end() || // Excluding by id.
      // Excluding by decoded id.
      (!dec_id.empty() && exclude_ids_.find(dec_id) != exclude_ids_.end())) {
    return false;
  }

  // Return True/Show - if the include-list is not used.
  // Else return False/Hide - if the id is not found in the include-list.
  return include_ids_.empty();
}

// ------------------------------------------------------------------------------------------------
// Actions for the entries.
// ------------------------------------------------------------------------------------------------
enum ActionType {
  kNO_OP,
  // Writable actions:
  kCHANGE,
  kADD,
  kREMOVE,
  kUNKNOWN,
  kNUM_ACTIONS
};

string ToString(ActionType act) {
  switch (act) {
    case kNO_OP:  return "NO-OP";
    case kCHANGE: return "CHANGE";
    case kADD:    return "ADD";
    case kREMOVE: return "REMOVE";
    default:      return "UNKNOWN";
  }
}

using ActionNameMap = unordered_map<string, ActionType>;

ActionNameMap InitActionNames() {
  ActionNameMap act_name_map;
  for (int a = 0; a < kNUM_ACTIONS; ++a) {
    const ActionType act = static_cast<ActionType>(a);
    act_name_map[ToString(act)] = act;
  }
  return act_name_map;
}

Result<ActionType> Parse_ActionType(const string& act_str) {
  if (act_str.empty()) {
    return kNO_OP;
  }

  static const ActionNameMap act_name_map = InitActionNames();
  const ActionNameMap::const_iterator it = act_name_map.find(act_str);
  return it == act_name_map.end() ? kUNKNOWN : it->second;
}

// ------------------------------------------------------------------------------------------------
// JSON writer with special handling of SysRowEntry.
// ------------------------------------------------------------------------------------------------
class SysRowJsonWriter : public JsonWriter {
 public:
  // Stream 'out' and 'filters' must be alive during the object life-time.
  SysRowJsonWriter(std::stringstream* out, Mode mode, const ReadFilters& filters) :
      JsonWriter(out, mode), filters_(filters) {}
  virtual ~SysRowJsonWriter() = default;

  Status WriteEntryPB(SysRowEntryType type, const Slice& id, const Slice& data);

  Status WritePB(const Message& data);

 protected:
  void ProtobufRepeatedField(const Message& pb, const FieldDescriptor* field, int index) override;

  void SysRow(const Message& message);

  void AddAction();
  void AddHexData(const Slice& data);

  void AddDataPB(const Message& pb);

  Status AddProtobuf(const Message& pb);

 private:
  const ReadFilters& filters_;
};

void SysRowJsonWriter::ProtobufRepeatedField(
    const Message& pb, const FieldDescriptor* field, int index) {
  if (field->cpp_type() == FieldDescriptor::CPPTYPE_MESSAGE &&
      field->message_type() &&
      field->message_type()->full_name() == "yb.master.SysRowEntry") {
    SysRow(pb.GetReflection()->GetRepeatedMessage(pb, field, index));
  } else {
    JsonWriter::ProtobufRepeatedField(pb, field, index);
  }
}

void SysRowJsonWriter::SysRow(const Message& message) {
  const master::SysRowEntry& entry = dynamic_cast<const master::SysRowEntry&>(message);
  if (filters_.IsSet(OptionFlag::kUseNestedFiltering) &&
      !filters_.NeedToShow(entry.type(), entry.id()) ) {
    return; // Hide this entry.
  }

  const Status s = WriteEntryPB(entry.type(), Slice(entry.id()), Slice(entry.data()));
  if (!s.ok()) {
    LOG_WITH_FUNC(WARNING) << s;
    // Raise exception as JsonWriter methods do not return status.
    throw StatusException(s);
  }
}

void SysRowJsonWriter::AddAction() {
  if (filters_.IsNotSet(OptionFlag::kNoAction)) {
    String("__ACTION__");
    String(""); // Action: "" == NoOp, ADD, REMOVE, CHANGE
  }
}

void SysRowJsonWriter::AddDataPB(const Message& pb) {
  if (filters_.IsNotSet(OptionFlag::kNoDataPB)) {
    String("DATA-PB");
    String(pb.GetDescriptor()->full_name());
  }
}

void SysRowJsonWriter::AddHexData(const Slice& data) {
  if (filters_.IsNotSet(OptionFlag::kNoHexData)) {
    String("DATA-HASH");
    Uint64(data.hash());

    String("DATA-HEX");
    const string data_str = data.ToBuffer();
    String(ByteStringToAscii(data_str));

    if (filters_.IsNotSet(OptionFlag::kNoRawData)) {
      String("DATA-RAW");
      String(data_str); // PB as text data with escaped non-printable symbols.
    }
  }
}

Status SysRowJsonWriter::AddProtobuf(const Message& pb) {
  if (filters_.IsNotSet(OptionFlag::kNoData)) {
    String("DATA");
    try {
      Protobuf(pb);
    } catch(StatusException& e) {
      return e.status();
    }
  }
  return Status::OK();
}

Status SysRowJsonWriter::WritePB(const Message& pb) {
  StartObject(); // entry
#define WRITABLE_RAFT_AND_CONSENSUS_INFO 0 // Enable if PB can be written by the tool.
#if WRITABLE_RAFT_AND_CONSENSUS_INFO
  AddAction();
#endif

  AddDataPB(pb);

#if WRITABLE_RAFT_AND_CONSENSUS_INFO
  if (filters_.IsNotSet(OptionFlag::kNoHexData)) {
    AddHexData(Slice(pb.SerializeAsString()));
  }
#endif

  RETURN_NOT_OK(AddProtobuf(pb));
  EndObject(); // entry
  return Status::OK();
}

Status SysRowJsonWriter::WriteEntryPB(SysRowEntryType type, const Slice& id, const Slice& data) {
  StartObject(); // entry
  AddAction();

  String("TYPE");
  String(master::SysRowEntryType_Name(type));

  string id_str = id.ToBuffer();
  if (!IsPrint(id_str)) {
    const Uuid txn_id = Uuid::TryFullyDecode(id_str);
    if (!txn_id.IsNil()) {
      if (FLAGS_show_raw_id) {
        String("ID-RAW");
        String(id_str);
      }
      id_str = txn_id.ToString();
    }
  }

  String("ID");
  String(id_str);

  auto pb = VERIFY_RESULT(SliceToCatalogEntityPB(type, data));

  AddDataPB(*pb.get());
  AddHexData(data);

  if (filters_.IsNotSet(OptionFlag::kNoData)) {
    RETURN_NOT_OK(AddProtobuf(*pb.get()));
  }

  EndObject(); // entry
  return Status::OK();
}

// ------------------------------------------------------------------------------------------------
// JSON reader with special handling of SysRowEntry.
// ------------------------------------------------------------------------------------------------
class SysRowHandlerIf {
 public:
  virtual ~SysRowHandlerIf() {}
  virtual Status ReadAndProcess() = 0;
};

class SysRowJsonReader : public JsonReader {
 public:
  typedef std::function<unique_ptr<SysRowHandlerIf>(
      const rapidjson::Value& value, master::SysRowEntry* entry)> CreateSysRowHandlerFn;

  explicit SysRowJsonReader(string text) : JsonReader(std::move(text)) {}

  Status ExtractSysRowMessage(const rapidjson::Value& value, Message* pb) const;

  int GetNesting() const {
    return nesting_;
  }

  void SetSysRowHandlerCreator(const CreateSysRowHandlerFn& fn) {
    create_handler_fn_ = fn;
  }

 private:
  Status ExtractProtobufRepeatedField(
      const rapidjson::Value& value, Message* pb, const FieldDescriptor* field) const override;

  CreateSysRowHandlerFn create_handler_fn_;
  mutable int nesting_ = 0;
};

Status SysRowJsonReader::ExtractProtobufRepeatedField(
    const rapidjson::Value& value, Message* pb, const FieldDescriptor* field) const {
  if (field->cpp_type() == FieldDescriptor::CPPTYPE_MESSAGE &&
      field->message_type() &&
      field->message_type()->full_name() == "yb.master.SysRowEntry") {
    return ExtractSysRowMessage(value,
        DCHECK_NOTNULL(DCHECK_NOTNULL(pb)->GetReflection())->AddMessage(pb, field));
  } else {
    return JsonReader::ExtractProtobufRepeatedField(value, pb, field);
  }
}

Status SysRowJsonReader::ExtractSysRowMessage(const rapidjson::Value& value, Message* pb) const {
  ++nesting_;
  LOG_IF(DFATAL, !create_handler_fn_) << "Handler-creator function is not initialized";
  const unique_ptr<SysRowHandlerIf> sys_row =
      create_handler_fn_(value, dynamic_cast<master::SysRowEntry*>(pb));
  const Status s = sys_row->ReadAndProcess();
  --nesting_;
  return s;
}

// Helper for debug output.
string EntryDetails(const SysRowJsonReader& r, const rapidjson::Value& json) {
  string type, id;
  Status s = r.ExtractString(&json, "TYPE", &type);
  if (!s.ok()) {
    serr << "Failed to extract TYPE: " << s << endl;
  }

  s = r.ExtractString(&json, "ID", &id);
  if (!s.ok()) {
    serr << "Failed to extract ID: " << s << endl;
  }

  return type + " ID=" + id;
}

Status ProcessSysEntryJson(const SysRowJsonReader& r, const rapidjson::Value& value) {
  // The result entry is ignored.
  // ExtractSysRowMessage is called to call finally the SysRowHandler methods.
  master::SysRowEntry entry;
  return r.ExtractSysRowMessage(value, &entry);
}

// ------------------------------------------------------------------------------------------------
// Base SysRowEntry loader.
// ------------------------------------------------------------------------------------------------
class SysRowHandler : public SysRowHandlerIf {
 public:
  SysRowHandler(
      const SysRowJsonReader& r, const rapidjson::Value& value, master::SysRowEntry* entry)
      : reader_(r), json_entry_(value), entry_(entry) {}

  Status ReadAndProcess() override;
  // Hooks for command-specific handling in derived classes.
  virtual Status PreProcess() { return Status::OK(); }
  virtual Status PostProcess() { return Status::OK(); }

  SysRowEntryType GetEntryType() const {
    return DCHECK_NOTNULL(entry_)->type();
  }

 protected:
  Status ReadSysRowPB(SysRowEntryType type);

 protected:
  const SysRowJsonReader& reader_;

  const rapidjson::Value& json_entry_;
  master::SysRowEntry* entry_;
  unique_ptr<Message> sys_row_pb_new_; // From 'DATA' JSON member.
};

Status SysRowHandler::ReadSysRowPB(SysRowEntryType type) {
  // Parse PB from DATA.
  sys_row_pb_new_ = VERIFY_RESULT(CatalogEntityPBForType(type));
  RETURN_NOT_OK(reader_.ExtractProtobuf(&json_entry_, "DATA", sys_row_pb_new_.get()));
  DCHECK_NOTNULL(entry_)->set_data(sys_row_pb_new_->SerializeAsString());
  return Status::OK();
}

Status SysRowHandler::ReadAndProcess() {
  string type_str, id_str;
  RETURN_NOT_OK(reader_.ExtractString(&json_entry_, "TYPE", &type_str));
  const SysRowEntryType type = VERIFY_RESULT(Parse_SysRowEntryType(type_str));

  RETURN_NOT_OK(reader_.ExtractString(&json_entry_, "ID", &id_str));

  // Fill this SysRowEntry.
  DCHECK_NOTNULL(entry_)->set_type(type);
  entry_->set_id(id_str);

  RETURN_NOT_OK(PreProcess());
  // Fill this SysRowEntry 'data' via SysRowHandler::ReadSysRowPB().
  RETURN_NOT_OK(ReadSysRowPB(type));
  return PostProcess();
}

// ------------------------------------------------------------------------------------------------
// Tool to administer SysCatalog from the CLI.
// ------------------------------------------------------------------------------------------------
class SysCatalogTool {
 public:
  virtual ~SysCatalogTool() = default;

  Result<string> ReadIntoString(const ReadFilters& filters);

  Status ShowChangesIn(const string& file_name, const ReadFilters& filters);

 protected:
  Status InitFsManager();
  Status Init();

  Status WriteEnumDescriptor(const EnumDescriptor& descriptor);

  Status WritePBDescriptor(const Descriptor& descriptor, const string& type = "");

  Status WriteEntryDescriptor(SysRowEntryType type) {
    auto pb = VERIFY_RESULT(CatalogEntityPBForType(type));
    return WritePBDescriptor(*pb->GetDescriptor(), master::SysRowEntryType_Name(type));
  }

 private:
  unique_ptr<FsManager> fs_manager_;
  MiniSysCatalogTablePtr sys_catalog_;

  unique_ptr<SysRowJsonWriter> writer_;
};

Status SysCatalogTool::InitFsManager() {
  // Init File System Manager.
  FsManagerOpts fs_opts;
  fs_opts.server_type = master::MasterOptions::kServerType;
  fs_manager_.reset(new FsManager(Env::Default(), fs_opts));
  return Status::OK();
}

Status SysCatalogTool::Init() {
  SCHECK(!FLAGS_fs_data_dirs.empty(),
      InvalidArgument, "ERROR: gflag '--fs_data_dirs' was not set");

  RETURN_NOT_OK(InitFsManager());
  RETURN_NOT_OK(fs_manager_->CheckAndOpenFileSystemRoots());
  vector<string> tablet_ids = VERIFY_RESULT(fs_manager_->ListTabletIds());
  sinfo << "Found Tablet IDs: " << yb::ToString(tablet_ids) << endl;

  // Load SysCatalog Tablet.
  sys_catalog_ = VERIFY_RESULT(MiniSysCatalogTable::Load(fs_manager_.get()));
  return Status::OK();
}

// ------------------------------------------------------------------------------------------------
// Dump command
// ------------------------------------------------------------------------------------------------
Status SysCatalogTool::WriteEnumDescriptor(const EnumDescriptor& descriptor) {
  writer_->String(descriptor.full_name());
  writer_->StartObject();

  writer_->String("DESCRIPTOR");
  writer_->StartArray();
  auto lines = StringSplit(descriptor.DebugString(), '\n');
  for (const string& str : lines) {
    writer_->String(str);
  }
  writer_->EndArray();

  writer_->EndObject();
  return Status::OK();
}

Status SysCatalogTool::WritePBDescriptor(const Descriptor& descriptor, const string& type) {
  writer_->String(descriptor.full_name());
  writer_->StartObject();

  if (!type.empty()) {
    writer_->String("TYPE");
    writer_->String(type);
  }

  writer_->String("DESCRIPTOR");
  writer_->StartArray();
  auto lines = StringSplit(descriptor.DebugString(), '\n');
  for (const string& str : lines) {
    writer_->String(str);
  }
  writer_->EndArray();

  writer_->EndObject();
  return Status::OK();
}

Result<string> SysCatalogTool::ReadIntoString(const ReadFilters& filters) {
  LOG(INFO) << "Running the dump command. Options=" << filters.ToString();
  const Timestamp start_time = DateTime::TimestampNow();

  std::stringstream stream;
  writer_.reset(new SysRowJsonWriter(&stream, JsonWriter::PRETTY_ESCAPE_STR, filters));
  writer_->StartObject(); // root

  if (filters.IsNotSet(OptionFlag::kNoHeader)) {
    writer_->String("HEADER");
    writer_->StartObject(); // header

    writer_->String("FORMAT-VERSION");
    writer_->Int(1);

    if (filters.IsNotSet(OptionFlag::kNoYBVersionStr)) {
      writer_->String("YB-VERSION-STR");
      writer_->String(VersionInfo::GetShortVersionString());
    }

    if (filters.IsNotSet(OptionFlag::kNoYBVersionPB)) {
      VersionInfoPB ver_pb;
      VersionInfo::GetVersionInfoPB(&ver_pb);
      writer_->String("YB-VERSION");
      writer_->Protobuf(ver_pb);
    }

    if (filters.IsNotSet(OptionFlag::kNoDescriptors)) {
      writer_->String("PROTO-BUFFERS");
      writer_->StartObject(); // proto-buffers
      RETURN_NOT_OK(WritePBDescriptor(*tablet::RaftGroupReplicaSuperBlockPB::descriptor()));
      RETURN_NOT_OK(WritePBDescriptor(*consensus::ConsensusMetadataPB::descriptor()));
      RETURN_NOT_OK(WriteEnumDescriptor(*master::SysRowEntryType_descriptor()));
      RETURN_NOT_OK(WritePBDescriptor(*master::SysRowEntry::descriptor()));

      for (int type = master::SysRowEntryType_MIN; type <= master::SysRowEntryType_MAX; ++type) {
        if (type != SysRowEntryType::UNKNOWN) {
          RETURN_NOT_OK(WriteEntryDescriptor(static_cast<SysRowEntryType>(type)));
        }
      }
      writer_->EndObject(); // proto-buffers
    }
  }

  RETURN_NOT_OK(Init());

  tablet::TabletPtr tablet = sys_catalog_->tablet();
  SCHECK(tablet, ShutdownInProgress, "SysCatalog tablet is not ready.");

  if (filters.IsNotSet(OptionFlag::kNoHeader)) {
    if (filters.IsNotSet(OptionFlag::kNoRaft)) {
      tablet::RaftGroupReplicaSuperBlockPB superblock;
      tablet->metadata()->ToSuperBlock(&superblock);
      writer_->String("RAFT");
      RETURN_NOT_OK(writer_->WritePB(superblock));
    }

    if (filters.IsNotSet(OptionFlag::kNoConsensus)) {
      writer_->String("CONSENSUS");
      RETURN_NOT_OK(writer_->WritePB(sys_catalog_->ConsensusPB()));
    }

    writer_->EndObject(); // header
  }

  if (filters.IsNotSet(OptionFlag::kNoSysCatalog)) {
    writer_->String("SYS-CATALOG");
    writer_->StartArray(); // sys-catalog
  }

  sinfo << "Reading " << flush;
  int num_entries = 0, filtered_entries = 0;
  const Status s = master::EnumerateAllSysCatalogEntries(
      tablet.get(), *tablet->schema(),
      [this, &filters, &num_entries, &filtered_entries](
          int8_t entry_type, const Slice& id, const Slice& data) -> Status {
        if (filters.IsNotSet(OptionFlag::kNoSysCatalog) &&
            VERIFY_RESULT(filters.NeedToShow(entry_type, id))) {
          RETURN_NOT_OK(IsValid_SysRowEntryType(entry_type));
          RETURN_NOT_OK(writer_->WriteEntryPB(static_cast<SysRowEntryType>(entry_type), id, data));
          ++filtered_entries;
        }

        ++num_entries;
        if (num_entries % 200 == 0) {
          sinfo << "." << flush; // Show progress.
        }

        return Status::OK();
      });

  sinfo << endl;
  if (!s.ok()) {
    serr << "ERROR: " << s << endl;
    return STATUS(Corruption, "The tool execution was interrupted due to error");
  }

  if (filters.IsNotSet(OptionFlag::kNoSysCatalog)) {
    writer_->EndArray(); // sys-catalog
  }

  if (filters.IsNotSet(OptionFlag::kNoFooter)) {
    writer_->String("FOOTER");
    writer_->StartObject(); // footer

    writer_->String("TOTAL-ENTRIES");
    writer_->Int(num_entries);

    writer_->String("FILTERED-ENTRIES");
    writer_->Int(filtered_entries);

    writer_->String("TIME-ZONE");
    writer_->String(DateTime::SystemTimezone());

    writer_->String("TIME-START");
    writer_->String(start_time.ToHumanReadableTime());

    const Timestamp end_time = DateTime::TimestampNow();
    writer_->String("TIME-END");
    writer_->String(end_time.ToHumanReadableTime());

    const MonoDelta duration =
        MonoDelta::FromMicroseconds(end_time.ToInt64() - start_time.ToInt64());
    writer_->String("TIME-DURATION");
    writer_->String(duration.ToString());

    writer_->EndObject(); // footer
  }

  writer_->EndObject(); // root
  sinfo << "Filtered " << filtered_entries << " entries from " << num_entries
        << " read entries" << endl << flush;
  return stream.str();
}

// ------------------------------------------------------------------------------------------------
// Show-changes command
// ------------------------------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------
// SysRowHandler for show-changes command.
// ------------------------------------------------------------------------------------------------
class ShowChangesCmdSysRowHandler : public SysRowHandler {
 public:
  ShowChangesCmdSysRowHandler(
      const SysRowJsonReader& r, const rapidjson::Value& value, master::SysRowEntry* entry,
      bool show_no_op) : SysRowHandler(r, value, entry), show_no_op_(show_no_op) {}

  Status PreProcess() override;

  Status PostProcess() override;

 protected:
  Status ReadAndCheckPB(SysRowEntryType type);

  bool show_no_op_;
  ActionType act_ = kUNKNOWN;
  string old_data_;
  string prefix_;
};

Status ShowChangesCmdSysRowHandler::PreProcess() {
  string act_str;
  RETURN_NOT_OK(reader_.ExtractString(&json_entry_, "__ACTION__", &act_str));
  act_ = VERIFY_RESULT(Parse_ActionType(act_str));

  if (act_ != kNO_OP || show_no_op_) {
    prefix_.clear();
    for (int i = 1; i < reader_.GetNesting(); ++i) {
      prefix_ += "|-> ";
    }
    sinfo << prefix_  << ToString(act_) << ": " << master::SysRowEntryType_Name(GetEntryType())
          << " " << entry_->id() << endl;
  }

  if (act_ != kNO_OP) {
    sinfo << kSectionSeparator << endl;
  }

  if (act_ != kADD) {
    string hex;
    RETURN_NOT_OK(reader_.ExtractString(&json_entry_, "DATA-HEX", &hex));
    uint64_t hash;
    RETURN_NOT_OK(reader_.ExtractUInt64(&json_entry_, "DATA-HASH", &hash));

    SCHECK(ByteStringFromAscii(hex, &old_data_), InvalidArgument,
        EntryDetails(reader_, json_entry_) + ": Cannot parse DATA-HEX from: " + hex);
    const uint64_t expected_hash = Slice(old_data_).hash();
    SCHECK(hash == expected_hash, InvalidArgument, EntryDetails(reader_, json_entry_) +
        ": Incorrect DATA-HASH value: " + yb::ToString(hash) +
        " expected " + yb::ToString(expected_hash));

    // Convert binary data back into hex (format of DATA-HEX).
    SCHECK(ByteStringToAscii(old_data_) == hex, InternalError,
        EntryDetails(reader_, json_entry_) + ": Invalid DATA-HEX: " + hex);
  }

  return Status::OK();
}

string AddPrefixToAllLines(const string& prefix, const string& str) {
  string s = prefix + util::RightTrimStr(str);
  GlobalReplaceSubstring("\n", "\n" + prefix, &s);
  return s + "\n";
}

Status ShowChangesCmdSysRowHandler::ReadAndCheckPB(SysRowEntryType type) {
  switch (act_) {
    case kNO_OP: FALLTHROUGH_INTENDED;
    case kCHANGE: {
      // Parse PB from DATA-HEX.
      auto old_pb = VERIFY_RESULT(SliceToCatalogEntityPB(type, Slice(old_data_)));

      // Compare PBs.
      string diff_str;
      const bool pbs_equal = pb_util::ArePBsEqual(*old_pb.get(), *sys_row_pb_new_, &diff_str);
      if (act_ == kCHANGE) {
        SCHECK(!pbs_equal, InvalidArgument, "ERROR: No data changes found!");
        sinfo << AddPrefixToAllLines(prefix_, diff_str);
      } else {
        SCHECK(pbs_equal, InvalidArgument, "ERROR: Unexpected data changes: " + diff_str);
      }
    } break;

    case kADD: {
      sinfo << AddPrefixToAllLines(prefix_, sys_row_pb_new_->DebugString());
    }
    break;

    case kREMOVE: {
      // Parse PB from DATA-HEX.
      auto old_pb = VERIFY_RESULT(SliceToCatalogEntityPB(type, Slice(old_data_)));
      sinfo << AddPrefixToAllLines(prefix_, old_pb->DebugString());
    } break;

    default: return STATUS(InternalError, "Unexpected action", ToString(act_));
  }

  return Status::OK();
}

Status ShowChangesCmdSysRowHandler::PostProcess() {
  const Status s = ReadAndCheckPB(GetEntryType());

  if (act_ != kNO_OP) {
    sinfo << kSectionSeparator << endl;
  }
  return s;
}

Status SysCatalogTool::ShowChangesIn(const string& file_name, const ReadFilters& filters) {
  using rapidjson::Value;

  LOG(INFO) << "Running the show-changes command: input JSON file=" << file_name;
  const Timestamp start_time = DateTime::TimestampNow();

  RETURN_NOT_OK(InitFsManager());

  faststring data;
  sinfo << "Reading from file " << file_name << flush;
  RETURN_NOT_OK(ReadFileToString(fs_manager_->env(), file_name, &data));
  sinfo << " - done" << endl;

  sinfo << "Parsing JSON data " << flush;
  SysRowJsonReader r(data.ToString());
  RETURN_NOT_OK(r.Init());

  r.SetSysRowHandlerCreator(
      [&r, &filters](const rapidjson::Value& value, master::SysRowEntry* entry)
          -> unique_ptr<SysRowHandlerIf> {
        return std::make_unique<ShowChangesCmdSysRowHandler>(
            r, value, entry, filters.IsSet(OptionFlag::kShowNoOp));
      });

  sinfo << " - done" << endl;

  array<vector<const Value*>, kNUM_ACTIONS> act_to_values;
  sinfo << "Searching for changed entries:" << endl;
  vector<const Value*> sys;
  RETURN_NOT_OK(r.ExtractObjectArray(r.root(), "SYS-CATALOG", &sys));

  for (const Value* v : sys) {
    string act_str;
    RETURN_NOT_OK(r.ExtractString(v, "__ACTION__", &act_str));
    const ActionType act = VERIFY_RESULT(Parse_ActionType(act_str));
    act_to_values[act].push_back(v);
    if (act == kUNKNOWN) {
      sinfo << "WARNING: " << EntryDetails(r, *v) << ": Unknown __ACTION__: " << act_str << endl;
    }
  }

  for (int act = 0; act < kNUM_ACTIONS; ++act) {
    if (act_to_values[act].empty() ||
        act == kUNKNOWN ||
        (act == kNO_OP && filters.IsNotSet(OptionFlag::kShowNoOp))) {
      continue;
    }

    sinfo << endl
          << "Show details for " << act_to_values[act].size() <<  " "
          << ToString(static_cast<ActionType>(act)) << " actions..."
          << endl << kSectionSeparator << endl;
    for (const Value* v : act_to_values[act]) {
      RETURN_NOT_OK(ProcessSysEntryJson(r, *v));
    }

    if (act == kNO_OP) {
      sinfo << kSectionSeparator << endl;
    }
  }

  sinfo << endl << "Total entries in SYS-CATALOG: " << sys.size() << endl;
  for (int act = kNO_OP; act < kNUM_ACTIONS; ++act) {
    sinfo << "    " << ToString(static_cast<ActionType>(act)) << ": "
          << act_to_values[act].size() << endl;
  }

  if (0 == act_to_values[kCHANGE].size() +
           act_to_values[kADD].size() + act_to_values[kREMOVE].size()) {
    return STATUS(InvalidArgument, "ERROR: No actions found");
  }

  const Timestamp end_time = DateTime::TimestampNow();
  const MonoDelta duration = MonoDelta::FromMicroseconds(end_time.ToInt64() - start_time.ToInt64());
  sinfo << "Total time: " << duration.ToString() << " (" << start_time.ToHumanReadableTime()
        << " - " << end_time.ToHumanReadableTime() << ")" << endl;
  return Status::OK();
}

// ------------------------------------------------------------------------------------------------
// SysCatalogAction enum
// ------------------------------------------------------------------------------------------------
#define SYS_CATALOG_WRITE_ENABLED 1

// API: dump         : read SysCatalog tablet and dump all entries into JSON file
//      show-changes : no read, only show differences in the stored-file
//      update       : includes pre-check + data writing +
//                     post-check-result: real SysCatalog vs stored-file
//      rollback     : includes pre-check, old data writing back, post-check-result
#if SYS_CATALOG_WRITE_ENABLED
#define SYS_CATALOG_ACTIONS (Help)(Dump)(ShowChanges)
// (Update)(Rollback)
#else
#define SYS_CATALOG_ACTIONS (Help)(Dump)
#endif // SYS_CATALOG_WRITE_ENABLED

YB_DEFINE_ENUM(SysCatalogAction, SYS_CATALOG_ACTIONS);

// ------------------------------------------------------------------------------------------------
// Help command
// ------------------------------------------------------------------------------------------------
const string kHelpDescription = kCommonHelpDescription;

using HelpArguments = CommonHelpArguments;

unique_ptr<OptionsDescription> HelpOptions() {
  return CommonHelpOptions();
}

Status HelpExecute(const HelpArguments& args) {
  return CommonHelpExecute<SysCatalogAction>(args);
}

// ------------------------------------------------------------------------------------------------
// Dump command
// ------------------------------------------------------------------------------------------------
const string kDumpDescription = "Dump entries from the SysCatalog. Dump all entries by default. "
                                "Arguments";

struct DumpArguments {
  string file;

  // Filtering by entry type name.
  vector<string> show;
  vector<string> skip;
  // Filtering by entry id.
  vector<ObjectId> include;
  vector<ObjectId> exclude;
  bool use_nested_filtering;
  // Flags to enable/disable sections or elements in the sections.
  bool no_header;         // Section HEADER
  bool no_sys_catalog;    // Section SYS-CATALOG
  bool no_footer;         // Section FOOTER
  // In HEADER section.
  bool no_yb_version_str;
  bool no_yb_version;
  bool no_descriptors;
  bool no_raft;
  bool no_consensus;
  // Flags to enable/disable parts in the entry.
  bool no_hex_data;
  bool no_raw_data;
  bool no_data_pb;
  bool no_data;
  bool no_action;
};

unique_ptr<OptionsDescription> DumpOptions() {
  string types;
  for (int type = master::SysRowEntryType_MIN; type <= master::SysRowEntryType_MAX; ++type) {
    if (type != SysRowEntryType::UNKNOWN) {
      types += (types.empty() ? " " : ", ");
      types += master::SysRowEntryType_Name(static_cast<SysRowEntryType>(type));
    }
  }

  auto result = std::make_unique<OptionsDescriptionImpl<DumpArguments>>(kDumpDescription);
  auto& args = result->args;
  result->desc.add_options()
      ("file", po::value(&args.file), "Optional JSON file name for output")
      // Filters.
      ("show", po::value(&args.show),
          (string("Filter: show only specified entry types. Options:") + types).c_str())
      ("skip", po::value(&args.skip),
          "Filter: skip specified entry types. Options are the same as for '--show'.")
      ("include", po::value(&args.include), "Filter: show only entries with specified IDs")
      ("exclude", po::value(&args.exclude), "Filter: ignore entries with specified IDs")
      ("use-nested-filtering", po::bool_switch(&args.use_nested_filtering),
          "Apply filters to child entries too")
      // Show/hide sections.
      ("no-header", po::bool_switch(&args.no_header), "Do not show HEADER section")
      ("no-sys-catalog", po::bool_switch(&args.no_sys_catalog), "Do not show SYS-CATALOG section")
      ("no-footer", po::bool_switch(&args.no_footer), "Do not show FOOTER section")
      // Show/hide elements in HEADER section.
      ("no-yb-version-str", po::bool_switch(&args.no_yb_version_str),
          "In HEADER: Do not show YB version as string")
      ("no-yb-version", po::bool_switch(&args.no_yb_version),
          "In HEADER: Do not show YB version as PB")
      ("no-descriptors", po::bool_switch(&args.no_descriptors),
          "In HEADER: Do not show PB descriptors")
      ("no-raft", po::bool_switch(&args.no_raft),
          "In HEADER: Do not show RAFT info")
      ("no-consensus", po::bool_switch(&args.no_consensus),
          "In HEADER: Do not show CONSENSUS info")
      // Show/hide elements in the entry.
      ("no-hex-data", po::bool_switch(&args.no_hex_data), "In entry: Do not show hex data ")
      ("no-raw-data", po::bool_switch(&args.no_raw_data),
          "In entry: Do not show PB as text data with escaped non-printable symbols")
      ("no-data-pb", po::bool_switch(&args.no_data_pb), "In entry: Do not show data PB name")
      ("no-data", po::bool_switch(&args.no_data), "In entry: Do not show detailed data")
      ("no-action", po::bool_switch(&args.no_action), "In entry: Do not add __ACTION__");
  return result;
}

Status DumpExecute(const DumpArguments& args) {
  // Process arguments.
  unordered_set<ObjectId> include_ids, exclude_ids;
  unordered_set<SysRowEntryType> skip_types;
  ReadFlags flags;
  SysRowEntryType type_val = SysRowEntryType::UNKNOWN;

  if (!args.show.empty()) {
    // Skip all.
    for (int type = master::SysRowEntryType_MIN; type <= master::SysRowEntryType_MAX; ++type) {
      skip_types.insert(static_cast<SysRowEntryType>(type));
    }

    for (const string& type_str : SplitAndFlatten(args.show)) {
      // Show only provided entry types.
      type_val = VERIFY_RESULT(Parse_SysRowEntryType(type_str));
      skip_types.erase(type_val);
    }
  }

  for (const string& type_str : SplitAndFlatten(args.skip)) {
    type_val = VERIFY_RESULT(Parse_SysRowEntryType(type_str));
    skip_types.insert(type_val);
  }

  for (const ObjectId& id : SplitAndFlatten(args.include)) {
    include_ids.insert(id);
  }

  for (const ObjectId& id : SplitAndFlatten(args.exclude)) {
    exclude_ids.insert(id);
  }

  flags.Set(OptionFlag::kUseNestedFiltering, args.use_nested_filtering)
       .Set(OptionFlag::kNoHeader, args.no_header)
       .Set(OptionFlag::kNoSysCatalog, args.no_sys_catalog)
       .Set(OptionFlag::kNoFooter, args.no_footer)
       .Set(OptionFlag::kNoYBVersionStr, args.no_yb_version_str)
       .Set(OptionFlag::kNoYBVersionPB, args.no_yb_version)
       .Set(OptionFlag::kNoDescriptors, args.no_descriptors)
       .Set(OptionFlag::kNoRaft, args.no_raft)
       .Set(OptionFlag::kNoConsensus, args.no_consensus)
       .Set(OptionFlag::kNoHexData, args.no_hex_data)
       .Set(OptionFlag::kNoRawData, args.no_raw_data)
       .Set(OptionFlag::kNoDataPB, args.no_data_pb)
       .Set(OptionFlag::kNoData, args.no_data)
       .Set(OptionFlag::kNoAction, args.no_action);

  // Call the handler.
  const string json = VERIFY_RESULT(SysCatalogTool().ReadIntoString(ReadFilters(
      std::move(include_ids), std::move(exclude_ids), std::move(skip_types), std::move(flags))));

  if (args.file.empty()) {
    sinfo << "Result JSON:" << endl << flush;
    sjson << json << endl;
  } else {
    sinfo << "Writing to file " << args.file << flush;
    RETURN_NOT_OK(WriteStringToFileSync(Env::Default(), Slice(json), args.file));
    sinfo << " - done" << endl;
  }
  return Status::OK();
}

// ------------------------------------------------------------------------------------------------
// Show-changes command
// ------------------------------------------------------------------------------------------------
const string kShowChangesDescription = "Show changes in entries in the JSON file. Arguments";

struct ShowChangesArguments {
  string file;
  bool show_no_op;
};

unique_ptr<OptionsDescription> ShowChangesOptions() {
  auto result =
      std::make_unique<OptionsDescriptionImpl<ShowChangesArguments>>(kShowChangesDescription);
  auto& args = result->args;
  result->desc.add_options()
      ("file", po::value(&args.file), "Input JSON file name")
      ("show-no-op", po::bool_switch(&args.show_no_op), "Check entries with NO-OP action");
  return result;
}

Status ShowChangesExecute(const ShowChangesArguments& args) {
  // Process arguments.
  ReadFlags flags;
  flags.Set(OptionFlag::kShowNoOp, args.show_no_op);

  // Call the handler.
  return SysCatalogTool().ShowChangesIn(args.file, ReadFilters(std::move(flags)));
}

// ------------------------------------------------------------------------------------------------
// Update command
// The command includes pre-check + writing + post-check.
// ------------------------------------------------------------------------------------------------
// TODO: Implement the commands.

// ------------------------------------------------------------------------------------------------
// Rollback command
// The command includes pre-check + writing old data back + post-check.
// ------------------------------------------------------------------------------------------------
// TODO: Implement the commands.

// ------------------------------------------------------------------------------------------------
// SysCatalogAction Tool
// ------------------------------------------------------------------------------------------------
YB_TOOL_ARGUMENTS(SysCatalogAction, SYS_CATALOG_ACTIONS);

} // namespace tools
} // namespace yb

int main(int argc, char** argv) {
  FLAGS_logtostderr = true;
  FLAGS_minloglevel = 2;

  google::SetUsageMessage(
      "<gflags> command <command-args>\n"
      "\n"
      "NOTE: gflag '--fs_data_dirs <dir>' is required.\n"
      "\n"
      "For detailed logging use gflag: '--minloglevel <level>'\n"
      "\n"
      "Example: sys-catalog-tool --fs_data_dirs ~/yugabyte-data/node-1/disk-1 --minloglevel 0 "
      "dump --file ./sys.json"
#if SYS_CATALOG_WRITE_ENABLED
      "\n\n"
      "Usual Pipeline:\n"
      "  1. dump    - Read SysCatalog from tablet into JSON file.\n"
      "  2. To update the SysCatalog manually correct the JSON file. Use __ACTION__ fields: \n"
      "     ADD, REMOVE, CHANGE, \"\" == NoOp.\n"
      "         ADD:    Add the new entry into the SysCatalog.\n"
      "         REMOVE: Remove the entry from the SysCatalog.\n"
      "         CHANGE: Update this entry. Take new values from the JSON file.\n"
      "         \"\":     Do nothing with the entry.\n"
      "  3. show-changes - Read the __ACTION__ fields in the JSON file and print the difference\n"
      "                    to the screen. It does not read or write the SysCatalog tablet.\n"
      "  4. update       - IMPORTANT! THIS WRITES DATA INTO THE SYS-CATALOG. USE WITH CAUTION!\n"
      "                    Write the entries back to the SysCatalog tablet.\n"
      "                    It writes only entries with __ACTION__ = ADD or REMOVE or CHANGE.\n"
      "  5. rollback     - IMPORTANT! THIS WRITES DATA INTO THE SYS-CATALOG. USE WITH CAUTION!\n"
      "                    Rollback changes after previous `Apply` operation.\n"
      "                    It restores back entries with __ACTION__ = ADD or REMOVE or CHANGE."
#endif // SYS_CATALOG_WRITE_ENABLED
      );

  yb::tools::ParseAndCutGFlagsFromCommandLine<yb::tools::SysCatalogAction>(&argc, &argv);
  yb::InitGoogleLoggingSafe(argv[0]);
  return yb::tools::ExecuteTool<yb::tools::SysCatalogAction>(argc, argv);
}
