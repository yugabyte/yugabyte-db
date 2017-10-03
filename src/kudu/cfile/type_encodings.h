// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
#ifndef KUDU_CFILE_TYPE_ENCODINGS_H_
#define KUDU_CFILE_TYPE_ENCODINGS_H_

#include "kudu/common/common.pb.h"
#include "kudu/util/status.h"

namespace kudu {
class TypeInfo;

namespace cfile {
class BlockBuilder;
class BlockDecoder;
class CFileReader;
class CFileIterator;
struct WriterOptions;

// Runtime Information for type encoding/decoding
// including the ability to build BlockDecoders and BlockBuilders
// for each supported encoding
// Mimicked after common::TypeInfo et al
class TypeEncodingInfo {
 public:

  static Status Get(const TypeInfo* typeinfo, EncodingType encoding, const TypeEncodingInfo** out);

  static const EncodingType GetDefaultEncoding(const TypeInfo* typeinfo);

  EncodingType encoding_type() const { return encoding_type_; }

  Status CreateBlockBuilder(BlockBuilder **bb, const WriterOptions *options) const;

  // Create a BlockDecoder. Sets *bd to the newly created decoder,
  // if successful, otherwise returns a non-OK Status.
  //
  // iter parameter will only be used when it is dictionary encoding
  Status CreateBlockDecoder(BlockDecoder **bd, const Slice &slice,
                            CFileIterator *iter) const;
 private:

  friend class TypeEncodingResolver;
  template<typename TypeEncodingTraitsClass> TypeEncodingInfo(TypeEncodingTraitsClass t);

  EncodingType encoding_type_;

  typedef Status (*CreateBlockBuilderFunc)(BlockBuilder **, const WriterOptions *);
  const CreateBlockBuilderFunc create_builder_func_;

  typedef Status (*CreateBlockDecoderFunc)(BlockDecoder **, const Slice &,
                                           CFileIterator *);
  const CreateBlockDecoderFunc create_decoder_func_;

  DISALLOW_COPY_AND_ASSIGN(TypeEncodingInfo);
};


} // namespace cfile
} // namespace kudu

#endif /* KUDU_CFILE_TYPE_ENCODINGS_H_ */
