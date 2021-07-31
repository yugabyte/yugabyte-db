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

#include "yb/rpc/compressed_stream.h"

#include <zlib.h>

#include "yb/rpc/circular_read_buffer.h"
#include "yb/rpc/outbound_data.h"
#include "yb/rpc/refined_stream.h"

#include "yb/util/logging.h"
#include "yb/util/size_literals.h"

using namespace std::literals;

DEFINE_int32(stream_compression_algo, 0, "Algorithm used for stream compression. "
                                         "0 - no compression, 1 - gzip.");

namespace yb {
namespace rpc {

namespace {

class Compressor {
 public:
  virtual std::string ToString() const = 0;

  // Initialize compressor, required since we don't use exceptions to return error from ctor.
  virtual CHECKED_STATUS Init() = 0;

  // Compress specified vector of input buffers into single output buffer.
  virtual CHECKED_STATUS Compress(
      const boost::container::small_vector_base<RefCntBuffer>& input, RefCntBuffer* output) = 0;

  // Decompress specified input slice to specified output buffer.
  virtual Result<size_t> Decompress(Slice* input, void* out, size_t outlen) = 0;

  // Connection header associated with this compressor.
  virtual OutboundDataPtr ConnectionHeader() = 0;

  virtual ~Compressor() = default;
};

template <class Compressor>
OutboundDataPtr GetConnectionHeader() {
  // Compressed stream header has signature YBx, where x - compressor identifier.
  static auto result = std::make_shared<StringOutboundData>(
      "YB"s + Compressor::kId, Compressor::kId + "ConnectionHeader"s);
  return result;
}

class GZipCompressor : public Compressor {
 public:
  static const char kId = 'G';
  static const int kIndex = 1;

  GZipCompressor() {
  }

  ~GZipCompressor() {
    if (deflate_inited_) {
      int res = deflateEnd(&deflate_stream_);
      LOG_IF(WARNING, res != Z_OK && res != Z_DATA_ERROR)
          << "Failed to destroy deflate stream: " << res;
    }
    if (inflate_inited_) {
      int res = inflateEnd(&inflate_stream_);
      LOG_IF(WARNING, res != Z_OK) << "Failed to destroy inflate stream: " << res;
    }
  }

  OutboundDataPtr ConnectionHeader() override {
    return GetConnectionHeader<GZipCompressor>();
  }

  CHECKED_STATUS Init() override {
    memset(&deflate_stream_, 0, sizeof(deflate_stream_));
    int res = deflateInit(&deflate_stream_, /* level= */ Z_DEFAULT_COMPRESSION);
    if (res != Z_OK) {
      return STATUS_FORMAT(RuntimeError, "Cannot init deflate stream: $0", res);
    }
    deflate_inited_ = true;

    memset(&inflate_stream_, 0, sizeof(inflate_stream_));
    res = inflateInit(&inflate_stream_);
    if (res != Z_OK) {
      return STATUS_FORMAT(RuntimeError, "Cannot init inflate stream: $0", res);
    }
    inflate_inited_ = true;

    return Status::OK();
  }

  std::string ToString() const override {
    return "GZip";
  }

  CHECKED_STATUS Compress(
      const boost::container::small_vector_base<RefCntBuffer>& input,
      RefCntBuffer* output) override {
    size_t total_len = 0;
    for (const auto& buf : input) {
      total_len += buf.size();
    }
    *output = RefCntBuffer(deflateBound(&deflate_stream_, total_len));
    deflate_stream_.avail_out = static_cast<unsigned int>(output->size());
    deflate_stream_.next_out = output->udata();

    for (auto it = input.begin(); it != input.end();) {
      const auto& buf = *it++;
      deflate_stream_.next_in = const_cast<Bytef*>(buf.udata());
      deflate_stream_.avail_in = static_cast<unsigned int>(buf.size());

      for (;;) {
        auto res = deflate(&deflate_stream_, it == input.end() ? Z_PARTIAL_FLUSH : Z_NO_FLUSH);
        if (res == Z_STREAM_END) {
          if (deflate_stream_.avail_in != 0) {
            return STATUS_FORMAT(
                RuntimeError, "Stream end when input data still available: $0",
                deflate_stream_.avail_in);
          }
          break;
        }
        if (res != Z_OK) {
          return STATUS_FORMAT(RuntimeError, "Compression failed: $0", res);
        }
        if (deflate_stream_.avail_in == 0) {
          break;
        }
      }
    }

    output->Shrink(deflate_stream_.next_out - output->udata());

    return Status::OK();
  }

  Result<size_t> Decompress(Slice* input, void* out, size_t outlen) override {
    inflate_stream_.next_in = const_cast<Bytef*>(pointer_cast<const Bytef*>(input->data()));
    inflate_stream_.avail_in = input->size();
    inflate_stream_.next_out = static_cast<Bytef*>(out);
    inflate_stream_.avail_out = outlen;

    int res = inflate(&inflate_stream_, Z_NO_FLUSH);
    if (res != Z_OK && res != Z_BUF_ERROR) {
      return STATUS_FORMAT(RuntimeError, "Decompression failed: $0", res);
    }

    input->remove_prefix(input->size() - inflate_stream_.avail_in);
    return outlen - inflate_stream_.avail_out;
  }

 private:
  z_stream deflate_stream_;
  z_stream inflate_stream_;
  bool deflate_inited_ = false;
  bool inflate_inited_ = false;
};

std::unique_ptr<Compressor> CreateCompressor(char sign) {
  switch (sign) {
    case GZipCompressor::kId:
      return std::make_unique<GZipCompressor>();
    default:
      return nullptr;
  }
}

std::unique_ptr<Compressor> CreateOutboundCompressor() {
  auto algo = FLAGS_stream_compression_algo;
  if (!algo) {
    return nullptr;
  }
  switch (algo) {
    case GZipCompressor::kIndex:
      return std::make_unique<GZipCompressor>();
    default:
      YB_LOG_EVERY_N_SECS(DFATAL, 5) << "Unknown compression algorithm: " << algo;
      return nullptr;
  }
}

class CompressedRefiner : public StreamRefiner {
 public:
  CompressedRefiner() = default;

 private:
  void Start(RefinedStream* stream) override {
    stream_ = stream;
  }

  CHECKED_STATUS ProcessHeader() override {
    constexpr int kHeaderLen = 3;

    auto data = stream_->ReadBuffer().AppendedVecs();
    if (data.empty() || data[0].iov_len < kHeaderLen) {
      // Did not receive enough bytes to make a decision.
      // So just wait more bytes.
      return Status::OK();
    }

    const auto* bytes = static_cast<const uint8_t*>(data[0].iov_base);
    if (bytes[0] == 'Y' && bytes[1] == 'B') {
      compressor_ = CreateCompressor(bytes[2]);
      if (compressor_) {
        RETURN_NOT_OK(compressor_->Init());
        RETURN_NOT_OK(stream_->StartHandshake());
        stream_->ReadBuffer().Consume(kHeaderLen, Slice());
        return Status::OK();
      }
    }

    // Don't use compression on this stream.
    return stream_->Established(RefinedStreamState::kDisabled);
  }

  CHECKED_STATUS Send(OutboundDataPtr data) override {
    boost::container::small_vector<RefCntBuffer, 10> input;
    data->Serialize(&input);
    RefCntBuffer buffer;
    RETURN_NOT_OK(compressor_->Compress(input, &buffer));
    VLOG_WITH_PREFIX(4) << __func__ << ", " << buffer.AsSlice().ToDebugString();
    auto compressed_data = std::make_shared<SingleBufferOutboundData>(
        std::move(buffer), std::move(data));
    return stream_->SendToLower(std::move(compressed_data));
  }

  CHECKED_STATUS Handshake() override {
    if (stream_->local_side() == LocalSide::kClient) {
      compressor_ = CreateOutboundCompressor();
      if (!compressor_) {
        return stream_->Established(RefinedStreamState::kDisabled);
      }
      RETURN_NOT_OK(compressor_->Init());
      RETURN_NOT_OK(stream_->SendToLower(compressor_->ConnectionHeader()));
    }

    return stream_->Established(RefinedStreamState::kEnabled);
  }

  CHECKED_STATUS Read(StreamReadBuffer* out) override {
    VLOG_WITH_PREFIX(4) << __func__;

    auto& inp = stream_->ReadBuffer();
    size_t consumed = 0;

    auto out_vecs = VERIFY_RESULT(out->PrepareAppend());
    auto out_it = out_vecs.begin();
    size_t appended = 0;

    for (const auto& iov : inp.AppendedVecs()) {
      Slice slice(static_cast<char*>(iov.iov_base), iov.iov_len);
      for (;;) {
        if (out_it->iov_len == 0) {
          if (++out_it == out_vecs.end()) {
            break;
          }
        }
        size_t len = VERIFY_RESULT(compressor_->Decompress(
            &slice, out_it->iov_base, out_it->iov_len));
        appended += len;
        IoVecRemovePrefix(len, &*out_it);
        if (slice.empty()) {
          break;
        }
      }
      consumed += iov.iov_len - slice.size();
      if (!slice.empty()) {
        break;
      }
    }
    out->DataAppended(appended);
    inp.Consume(consumed, Slice());
    return Status::OK();
  }

  const Protocol* GetProtocol() override {
    return CompressedStreamProtocol();
  }

  std::string ToString() const override {
    return compressor_ ? compressor_->ToString() : "PLAIN";
  }

  const std::string& LogPrefix() const {
    return stream_->LogPrefix();
  }

  RefinedStream* stream_ = nullptr;
  std::unique_ptr<Compressor> compressor_ = nullptr;
};

} // namespace

const Protocol* CompressedStreamProtocol() {
  static Protocol result("tcpc");
  return &result;
}

StreamFactoryPtr CompressedStreamFactory(
    StreamFactoryPtr lower_layer_factory, const MemTrackerPtr& buffer_tracker) {
  return std::make_shared<RefinedStreamFactory>(
      std::move(lower_layer_factory), buffer_tracker, [](const StreamCreateData& data) {
    return std::make_unique<CompressedRefiner>();
  });
}

}  // namespace rpc
}  // namespace yb
