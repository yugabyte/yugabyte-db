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

// This utility converts a proto ascii format file into a binary proto file in the yb format.

#include "google/protobuf/text_format.h"

#include "yb/consensus/metadata.pb.h"

#include "yb/master/master_backup.pb.h"

#include "yb/tablet/metadata.pb.h"

#include "yb/tools/pbc_tools_lib.h"

#include "yb/util/env.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/pb_util.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"

namespace yb::pb_util {

struct Arguments {
  std::string protobuf_file_path;
  std::string output_file_path;
  std::string key_id;
  std::string key_file_path;
};

Result<std::unique_ptr<google::protobuf::Message>> ProtoMessageType(std::string_view type);

Result<std::string> ParseHeader(std::string& file_contents);

Status WriteProtoFile(const Arguments& args) {
  auto tool_env = VERIFY_RESULT(PBToolEnv::Create(args.key_id, args.key_file_path));
  faststring fs_file_contents;
  RETURN_NOT_OK(ReadFileToString(&tool_env.env(), args.protobuf_file_path, &fs_file_contents));
  auto file_contents = fs_file_contents.ToString();
  auto protobuf_type = VERIFY_RESULT(ParseHeader(file_contents));
  auto proto = VERIFY_RESULT(ProtoMessageType(protobuf_type));
  if (!google::protobuf::TextFormat::ParseFromString(file_contents, proto.get())) {
    return STATUS_FORMAT(
        InvalidArgument, "Failed to parse text file into proto type $0", protobuf_type);
  }
  return pb_util::WritePBContainerToPath(
      &tool_env.env(), args.output_file_path, *proto, pb_util::OVERWRITE, pb_util::SYNC);
}

}  // namespace yb::pb_util

int main(int argc, char** argv) {
  yb::ParseCommandLineFlags(&argc, &argv, true);
  yb::InitGoogleLoggingSafe(argv[0]);

  if (argc != 3 && argc != 5) {
    std::cerr << "usage: " << argv[0]
              << " <protobuf_container_filename> <output_file_path> [<keyfile> <keyid>]"
              << std::endl;
    return 2;
  }

  yb::pb_util::Arguments args{argv[1], argv[2], "", ""};
  if (argc == 5) {
    args.key_file_path = argv[3];
    args.key_id = argv[4];
  }
  auto s = WriteProtoFile(args);
  if (!s.ok()) {
    std::cerr << "Failed to write proto file: " << s << std::endl;
    return 1;
  }
  return 0;
}

namespace yb::pb_util {

Result<std::unique_ptr<google::protobuf::Message>> ProtoMessageType(std::string_view type) {
  tablet::RaftGroupReplicaSuperBlockPB super_block;
  consensus::ConsensusMetadataPB consensus;
  master::SnapshotInfoPB snapshot;
  if (type == super_block.GetTypeName()) {
    return std::make_unique<tablet::RaftGroupReplicaSuperBlockPB>();
  } else if (type == consensus.GetTypeName()) {
    return std::make_unique<consensus::ConsensusMetadataPB>();
  } else if (type == snapshot.GetTypeName()) {
    return std::make_unique<master::SnapshotInfoPB>();
  }
  return STATUS_FORMAT(InvalidArgument, "$0 is not a supported proto type", type);
}

Result<std::string> ParseHeader(std::string& file_contents) {
  // The header our dump file uses for proto text files, assuming the file contains a single proto,
  // is 2 lines. Example header of a consensus proto file:

  // yb.consensus.ConsensusMetadataPB 0
  // -------
  // committed_config {
  // ...

  // This function obtains the type from the header and returns it.
  // It also strips the header from the file_contents parameter.
  auto pos = file_contents.find(' ');
  if (pos == std::string::npos) {
    return STATUS(InvalidArgument, "Cannot find type in header of text proto file");
  }
  auto type = file_contents.substr(0, pos);
  pos = file_contents.find('\n');
  if (pos == std::string::npos) {
    return STATUS(InvalidArgument, "Cannot parse header of text proto file");
  }
  pos = file_contents.find('\n', pos + 1);
  if (pos != std::string::npos) {
    file_contents.erase(0, pos + 1);
  } else {
    file_contents.clear();
  }
  return type;
}

}  // namespace yb::pb_util
