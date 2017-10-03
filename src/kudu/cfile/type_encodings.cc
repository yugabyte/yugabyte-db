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
#include "kudu/cfile/type_encodings.h"

#include <unordered_map>
#include <memory>
#include <utility>

#include <glog/logging.h>

#include "kudu/cfile/bshuf_block.h"
#include "kudu/cfile/gvint_block.h"
#include "kudu/cfile/plain_bitmap_block.h"
#include "kudu/cfile/plain_block.h"
#include "kudu/cfile/rle_block.h"
#include "kudu/cfile/binary_dict_block.h"
#include "kudu/cfile/binary_plain_block.h"
#include "kudu/cfile/binary_prefix_block.h"
#include "kudu/common/types.h"
#include "kudu/gutil/strings/substitute.h"

namespace kudu {
namespace cfile {

using std::unordered_map;
using std::shared_ptr;


template<DataType Type, EncodingType Encoding>
struct DataTypeEncodingTraits {};

// Instantiate this template to get static access to the type traits.
template<DataType Type, EncodingType Encoding> struct TypeEncodingTraits
  : public DataTypeEncodingTraits<Type, Encoding> {

  static const DataType type = Type;
  static const EncodingType encoding_type = Encoding;
};

// Generic, fallback, partial specialization that should work for all
// fixed size types.
template<DataType Type>
struct DataTypeEncodingTraits<Type, PLAIN_ENCODING> {

  static Status CreateBlockBuilder(BlockBuilder **bb, const WriterOptions *options) {
    *bb = new PlainBlockBuilder<Type>(options);
    return Status::OK();
  }

  static Status CreateBlockDecoder(BlockDecoder **bd, const Slice &slice,
                                   CFileIterator *iter) {
    *bd = new PlainBlockDecoder<Type>(slice);
    return Status::OK();
  }
};

// Generic, fallback, partial specialization that should work for all
// fixed size types.
template<DataType Type>
struct DataTypeEncodingTraits<Type, BIT_SHUFFLE> {

  static Status CreateBlockBuilder(BlockBuilder **bb, const WriterOptions *options) {
    *bb = new BShufBlockBuilder<Type>(options);
    return Status::OK();
  }

  static Status CreateBlockDecoder(BlockDecoder **bd, const Slice &slice,
                                   CFileIterator *iter) {
    *bd = new BShufBlockDecoder<Type>(slice);
    return Status::OK();
  }
};

// Template specialization for plain encoded string as they require a
// specific encoder/decoder.
template<>
struct DataTypeEncodingTraits<BINARY, PLAIN_ENCODING> {

  static Status CreateBlockBuilder(BlockBuilder **bb, const WriterOptions *options) {
    *bb = new BinaryPlainBlockBuilder(options);
    return Status::OK();
  }

  static Status CreateBlockDecoder(BlockDecoder **bd, const Slice &slice,
                                   CFileIterator *iter) {
    *bd = new BinaryPlainBlockDecoder(slice);
    return Status::OK();
  }
};

// Template specialization for packed bitmaps
template<>
struct DataTypeEncodingTraits<BOOL, PLAIN_ENCODING> {

  static Status CreateBlockBuilder(BlockBuilder **bb, const WriterOptions *options) {
    *bb = new PlainBitMapBlockBuilder();
    return Status::OK();
  }

  static Status CreateBlockDecoder(BlockDecoder **bd, const Slice &slice,
                                   CFileIterator *iter) {
    *bd = new PlainBitMapBlockDecoder(slice);
    return Status::OK();
  }
};


// Template specialization for RLE encoded bitmaps
template<>
struct DataTypeEncodingTraits<BOOL, RLE> {

  static Status CreateBlockBuilder(BlockBuilder** bb, const WriterOptions *options) {
    *bb = new RleBitMapBlockBuilder();
    return Status::OK();
  }

  static Status CreateBlockDecoder(BlockDecoder **bd, const Slice &slice,
                                   CFileIterator *iter) {
    *bd = new RleBitMapBlockDecoder(slice);
    return Status::OK();
  }
};

// Template specialization for plain encoded string as they require a
// specific encoder \/decoder.
template<>
struct DataTypeEncodingTraits<BINARY, PREFIX_ENCODING> {

  static Status CreateBlockBuilder(BlockBuilder **bb, const WriterOptions *options) {
    *bb = new BinaryPrefixBlockBuilder(options);
    return Status::OK();
  }

  static Status CreateBlockDecoder(BlockDecoder **bd, const Slice &slice,
                                   CFileIterator *iter) {
    *bd = new BinaryPrefixBlockDecoder(slice);
    return Status::OK();
  }
};

// Template for dictionary encoding
template<>
struct DataTypeEncodingTraits<BINARY, DICT_ENCODING> {

  static Status CreateBlockBuilder(BlockBuilder **bb, const WriterOptions *options) {
    *bb = new BinaryDictBlockBuilder(options);
    return Status::OK();
  }

  static Status CreateBlockDecoder(BlockDecoder **bd, const Slice &slice,
                                   CFileIterator *iter) {
    *bd = new BinaryDictBlockDecoder(slice, iter);
    return Status::OK();
  }
};


// Optimized grouping variable encoding for 32bit unsigned integers
template<>
struct DataTypeEncodingTraits<UINT32, GROUP_VARINT> {

  static Status CreateBlockBuilder(BlockBuilder **bb, const WriterOptions *options) {
    *bb = new GVIntBlockBuilder(options);
    return Status::OK();
  }

  static Status CreateBlockDecoder(BlockDecoder **bd, const Slice &slice,
                                   CFileIterator *iter) {
    *bd = new GVIntBlockDecoder(slice);
    return Status::OK();
  }
};

template<DataType IntType>
struct DataTypeEncodingTraits<IntType, RLE> {

  static Status CreateBlockBuilder(BlockBuilder** bb, const WriterOptions *options) {
    *bb = new RleIntBlockBuilder<IntType>();
    return Status::OK();
  }

  static Status CreateBlockDecoder(BlockDecoder** bd, const Slice& slice,
                                   CFileIterator *iter) {
    *bd = new RleIntBlockDecoder<IntType>(slice);
    return Status::OK();
  }
};


template<typename TypeEncodingTraitsClass>
TypeEncodingInfo::TypeEncodingInfo(TypeEncodingTraitsClass t)
    : encoding_type_(TypeEncodingTraitsClass::encoding_type),
      create_builder_func_(TypeEncodingTraitsClass::CreateBlockBuilder),
      create_decoder_func_(TypeEncodingTraitsClass::CreateBlockDecoder) {
}

Status TypeEncodingInfo::CreateBlockDecoder(BlockDecoder **bd,
                                            const Slice &slice,
                                            CFileIterator *iter) const {
  return create_decoder_func_(bd, slice, iter);
}

Status TypeEncodingInfo::CreateBlockBuilder(
    BlockBuilder **bb, const WriterOptions *options) const {
  return create_builder_func_(bb, options);
}

struct EncodingMapHash {
  size_t operator()(pair<DataType, EncodingType> pair) const {
    return (pair.first + 31) ^ pair.second;
  }
};

// A resolver for encodings, keeps all the allowed type<->encoding
// combinations. The first combination to be added to the map
// becomes the default encoding for the type.
class TypeEncodingResolver {
 public:
  Status GetTypeEncodingInfo(DataType t, EncodingType e,
                             const TypeEncodingInfo** out) {
    if (e == AUTO_ENCODING) {
      e = GetDefaultEncoding(t);
    }
    const TypeEncodingInfo *type_info = mapping_[make_pair(t, e)].get();
    if (PREDICT_FALSE(type_info == nullptr)) {
      return Status::NotSupported(
          strings::Substitute("Unsupported type/encoding pair: $0, $1",
                              DataType_Name(t),
                              EncodingType_Name(e)));
    }
    *out = type_info;
    return Status::OK();
  }

  const EncodingType GetDefaultEncoding(DataType t) {
    return default_mapping_[t];
  }

  // Add the encoding mappings
  // the first encoder/decoder to be
  // added to the mapping becomes the default
  //
  // TODO: Fix/work around the issue with RLE/BitWriter which
  //       (currently) makes it impossible to use RLE with
  //       64-bit int types.
 private:
  TypeEncodingResolver() {
    AddMapping<UINT8, PLAIN_ENCODING>();
    AddMapping<UINT8, RLE>();
    AddMapping<UINT8, BIT_SHUFFLE>();
    AddMapping<INT8, PLAIN_ENCODING>();
    AddMapping<INT8, RLE>();
    AddMapping<INT8, BIT_SHUFFLE>();
    AddMapping<UINT16, PLAIN_ENCODING>();
    AddMapping<UINT16, RLE>();
    AddMapping<UINT16, BIT_SHUFFLE>();
    AddMapping<INT16, PLAIN_ENCODING>();
    AddMapping<INT16, RLE>();
    AddMapping<INT16, BIT_SHUFFLE>();
    AddMapping<UINT32, GROUP_VARINT>();
    AddMapping<UINT32, RLE>();
    AddMapping<UINT32, PLAIN_ENCODING>();
    AddMapping<UINT32, BIT_SHUFFLE>();
    AddMapping<INT32, PLAIN_ENCODING>();
    AddMapping<INT32, RLE>();
    AddMapping<INT32, BIT_SHUFFLE>();
    AddMapping<UINT64, PLAIN_ENCODING>();
    AddMapping<UINT64, BIT_SHUFFLE>();
    AddMapping<INT64, PLAIN_ENCODING>();
    AddMapping<INT64, BIT_SHUFFLE>();
    AddMapping<FLOAT, PLAIN_ENCODING>();
    AddMapping<FLOAT, BIT_SHUFFLE>();
    AddMapping<DOUBLE, PLAIN_ENCODING>();
    AddMapping<DOUBLE, BIT_SHUFFLE>();
    AddMapping<BINARY, PLAIN_ENCODING>();
    AddMapping<BINARY, PREFIX_ENCODING>();
    AddMapping<BINARY, DICT_ENCODING>();
    AddMapping<BOOL, RLE>();
    AddMapping<BOOL, PLAIN_ENCODING>();
  }

  template<DataType type, EncodingType encoding> void AddMapping() {
    TypeEncodingTraits<type, encoding> traits;
    pair<DataType, EncodingType> encoding_for_type = make_pair(type, encoding);
    if (mapping_.find(encoding_for_type) == mapping_.end()) {
      default_mapping_.insert(make_pair(type, encoding));
    }
    mapping_.insert(
        make_pair(make_pair(type, encoding),
                  shared_ptr<TypeEncodingInfo>(new TypeEncodingInfo(traits))));
  }

  unordered_map<pair<DataType, EncodingType>,
      shared_ptr<const TypeEncodingInfo>,
      EncodingMapHash > mapping_;

  unordered_map<DataType, EncodingType, std::hash<size_t> > default_mapping_;

  friend class Singleton<TypeEncodingResolver>;
  DISALLOW_COPY_AND_ASSIGN(TypeEncodingResolver);
};

Status TypeEncodingInfo::Get(const TypeInfo* typeinfo,
                             EncodingType encoding,
                             const TypeEncodingInfo** out) {
  return Singleton<TypeEncodingResolver>::get()->GetTypeEncodingInfo(typeinfo->physical_type(),
                                                                     encoding,
                                                                     out);
}

const EncodingType TypeEncodingInfo::GetDefaultEncoding(const TypeInfo* typeinfo) {
  return Singleton<TypeEncodingResolver>::get()->GetDefaultEncoding(typeinfo->physical_type());
}

}  // namespace cfile
}  // namespace kudu

