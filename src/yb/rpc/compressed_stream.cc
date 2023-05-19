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

#include <lz4.h>
#include <snappy-sinksource.h>
#include <snappy.h>
#include <zlib.h>

#include <boost/preprocessor/cat.hpp>
#include <boost/range/iterator_range.hpp>

#include "yb/gutil/casts.h"

#include "yb/rpc/circular_read_buffer.h"
#include "yb/rpc/outbound_data.h"
#include "yb/rpc/refined_stream.h"
#include "yb/rpc/reactor_thread_role.h"

#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/size_literals.h"
#include "yb/util/status_format.h"
#include "yb/util/flags.h"

using namespace std::literals;

DEFINE_UNKNOWN_int32(stream_compression_algo, 0, "Algorithm used for stream compression. "
                                         "0 - no compression, 1 - gzip, 2 - snappy, 3 - lz4.");

namespace yb {
namespace rpc {

using SmallRefCntBuffers = ByteBlocks;

namespace {

class Compressor {
 public:
  virtual std::string ToString() const = 0;

  // Initialize compressor, required since we don't use exceptions to return error from ctor.
  virtual Status Init() = 0;

  // Compress specified vector of input buffers into single output buffer.
  virtual Status Compress(
      const SmallRefCntBuffers& input, RefinedStream* stream, OutboundDataPtr data) = 0;

  // Decompress specified input slice to specified output buffer.
  virtual Result<ReadBufferFull> Decompress(StreamReadBuffer* inp, StreamReadBuffer* out) = 0;

  // Connection header associated with this compressor.
  virtual OutboundDataPtr ConnectionHeader() = 0;

  virtual ~Compressor() = default;
};

size_t EntrySize(const RefCntSlice& buffer) {
  return buffer.size();
}

size_t EntrySize(const iovec& iov) {
  return iov.iov_len;
}

const char* EntryData(const RefCntSlice& buffer) {
  return buffer.data();
}

const char* EntryData(const iovec& iov) {
  return static_cast<char*>(iov.iov_base);
}

template <class Collection>
size_t TotalLen(const Collection& input) {
  size_t result = 0;
  for (const auto& buf : input) {
    result += EntrySize(buf);
  }
  return result;
}

template <class Compressor>
OutboundDataPtr GetConnectionHeader() {
  // Compressed stream header has signature YBx, where x - compressor identifier.
  static auto result = std::make_shared<StringOutboundData>(
      "YB"s + Compressor::kId, Compressor::kId + "ConnectionHeader"s);
  return result;
}

template <class SliceDecompressor>
Result<ReadBufferFull> DecompressBySlices(
    StreamReadBuffer* inp, StreamReadBuffer* out, const SliceDecompressor& slice_decompressor) {
  size_t consumed = 0;
  auto out_vecs = VERIFY_RESULT(out->PrepareAppend());
  auto out_it = out_vecs.begin();
  size_t appended = 0;

  for (const auto& iov : inp->AppendedVecs()) {
    Slice slice(static_cast<char*>(iov.iov_base), iov.iov_len);
    for (;;) {
      if (out_it->iov_len == 0) {
        if (++out_it == out_vecs.end()) {
          break;
        }
      }
      size_t len = VERIFY_RESULT(slice_decompressor(&slice, out_it->iov_base, out_it->iov_len));
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
  inp->Consume(consumed, Slice());
  return ReadBufferFull(out->Full());
}

class ZlibCompressor : public Compressor {
 public:
  static const char kId = 'G';
  static const int kIndex = 1;

  explicit ZlibCompressor(MemTrackerPtr mem_tracker) {
  }

  ~ZlibCompressor() {
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
    return GetConnectionHeader<ZlibCompressor>();
  }

  Status Init() override {
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
    return "Zlib";
  }

  Status Compress(
      const SmallRefCntBuffers& input, RefinedStream* stream, OutboundDataPtr data)
      ON_REACTOR_THREAD override {
    RefCntBuffer output(deflateBound(&deflate_stream_, TotalLen(input)));
    deflate_stream_.avail_out = static_cast<unsigned int>(output.size());
    deflate_stream_.next_out = output.udata();

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

    output.Shrink(deflate_stream_.next_out - output.udata());

    // Send compressed data to underlying stream.
    return stream->SendToLower(std::make_shared<SingleBufferOutboundData>(
        std::move(output), std::move(data)));
  }

  Result<ReadBufferFull> Decompress(StreamReadBuffer* inp, StreamReadBuffer* out) override {
    return DecompressBySlices(
        inp, out, [this](Slice* input, void* out, size_t outlen) -> Result<size_t> {
      inflate_stream_.next_in = const_cast<Bytef*>(pointer_cast<const Bytef*>(input->data()));
      inflate_stream_.avail_in = narrow_cast<uInt>(input->size());
      inflate_stream_.next_out = static_cast<Bytef*>(out);
      inflate_stream_.avail_out = narrow_cast<uInt>(outlen);

      int res = inflate(&inflate_stream_, Z_NO_FLUSH);
      if (res != Z_OK && res != Z_BUF_ERROR) {
        return STATUS_FORMAT(RuntimeError, "Decompression failed: $0", res);
      }

      input->remove_prefix(input->size() - inflate_stream_.avail_in);
      return outlen - inflate_stream_.avail_out;
    });
  }

 private:
  z_stream deflate_stream_;
  z_stream inflate_stream_;
  bool deflate_inited_ = false;
  bool inflate_inited_ = false;
};

// Source implementation that provides input from range of buffers.
template <class It>
class RangeSource : public snappy::Source {
 public:
  explicit RangeSource(const It& begin, const It& end)
      : current_(begin), end_(end),
        available_(TotalLen(boost::make_iterator_range(begin, end))) {}

  void Limit(size_t value) {
    available_ = value;
  }

  size_t Available() const override {
    return available_;
  }

  const char* Peek(size_t* len) override {
    if (available_ == 0) {
      *len = 0;
      return nullptr;
    }
    *len = std::min(EntrySize(*current_) - current_pos_, available_);
    return EntryData(*current_) + current_pos_;
  }

  void Rewind(size_t n) {
    available_ += n;
    for (;;) {
      if (current_pos_ >= n) {
        current_pos_ -= n;
        break;
      }
      n -= current_pos_;
      --current_;
      current_pos_ = EntrySize(*current_);
    }
  }

  void Skip(size_t n) override {
    if (!n) {
      return;
    }
    available_ -= n;
    if ((current_pos_ += n) >= EntrySize(*current_)) {
      current_pos_ = 0;
      ++current_;
      DCHECK(available_ == 0 ? current_ == end_ : current_ < end_)
          << "Available: " << available_ << ", left buffers: " << std::distance(current_, end_)
          << ", n: " << n;
    }
  }

 private:
  It current_; // Current buffer that we are reading.
  It end_;
  size_t current_pos_ = 0; // Position in current buffer.
  size_t available_; // How many bytes left in all buffers.
};

// Sink implementation that provides output to io vecs.
class IoVecsSink : public snappy::Sink {
 public:
  explicit IoVecsSink(IoVecs* out) : out_(out->begin()) {}

  size_t total_appended() const {
    return total_appended_;
  }

  void Append(const char* bytes, size_t n) override {
    total_appended_ += n;

    if (bytes == out_->iov_base) {
      if (out_->iov_len == n) {
        ++out_;
      } else {
        IoVecRemovePrefix(n, &*out_);
      }
      return;
    }

    while (n > 0) {
      size_t current_len = out_->iov_len;
      if (current_len >= n) {
        memcpy(out_->iov_base, bytes, n);
        IoVecRemovePrefix(n, &*out_);
        n = 0;
      } else {
        memcpy(out_->iov_base, bytes, current_len);
        bytes += current_len;
        n -= current_len;
        ++out_;
      }
    }
  }

  char* GetAppendBufferVariable(
      size_t min_size, size_t desired_size_hint, char* scratch,
      size_t scratch_size, size_t* allocated_size) override {
    if (min_size <= out_->iov_len) {
      *allocated_size = out_->iov_len;
      return static_cast<char*>(out_->iov_base);
    }
    return Sink::GetAppendBufferVariable(
        min_size, desired_size_hint, scratch, scratch_size, allocated_size);
  }

 private:
  IoVecs::iterator out_;
  size_t total_appended_ = 0;
};

// Binary search to find max value so that max_compressed_len(value) fits into header_len bytes.
template <class F>
static size_t FindMaxChunkSize(size_t header_len, const F& max_compressed_len) {
  size_t max_value = (1ULL << (8 * header_len)) - 1;
  size_t l = 1;
  size_t r = max_value;
  while (r > l) {
    size_t m = (l + r + 1) / 2;
    if (implicit_cast<size_t>(max_compressed_len(narrow_cast<int>(m))) > max_value) {
      r = m - 1;
    } else {
      l = m;
    }
  }
  return l;
}

// Snappy does not support stream compression.
// So we have to add block len, to start decompression only from block start.
// We use 2 bytes for block len, so have to limit uncompressed block so that in worst case
// compressed block size would fit into 2 bytes.
constexpr size_t kSnappyHeaderLen = 2;
const size_t kSnappyMaxChunkSize = FindMaxChunkSize(kSnappyHeaderLen, &snappy::MaxCompressedLength);

class SnappyCompressor : public Compressor {
 public:
  static constexpr char kId = 'S';
  static constexpr int kIndex = 2;
  static constexpr size_t kHeaderLen = kSnappyHeaderLen;

  explicit SnappyCompressor(MemTrackerPtr mem_tracker) {
  }

  ~SnappyCompressor() {
  }

  OutboundDataPtr ConnectionHeader() override {
    return GetConnectionHeader<SnappyCompressor>();
  }

  Status Init() override {
    return Status::OK();
  }

  std::string ToString() const override {
    return "Snappy";
  }

  Status Compress(
      const SmallRefCntBuffers& input, RefinedStream* stream, OutboundDataPtr data)
      ON_REACTOR_THREAD override {
    RangeSource<SmallRefCntBuffers::const_iterator> source(input.begin(), input.end());
    auto input_size = source.Available();
    bool stop = false;
    while (!stop) {
      // Split input into chunks of size kSnappyMaxChunkSize or less.
      if (input_size > kSnappyMaxChunkSize) {
        source.Limit(kSnappyMaxChunkSize);
        input_size -= kSnappyMaxChunkSize;
      } else {
        source.Limit(input_size);
        stop = true;
      }
      RefCntBuffer output(kHeaderLen + snappy::MaxCompressedLength(source.Available()));
      snappy::UncheckedByteArraySink sink(output.data() + kHeaderLen);
      auto compressed_len = snappy::Compress(&source, &sink);
      BigEndian::Store16(output.data(), compressed_len);
      output.Shrink(kHeaderLen + compressed_len);
      RETURN_NOT_OK(stream->SendToLower(std::make_shared<SingleBufferOutboundData>(
          std::move(output),
          // We processed last buffer, attach data to it, so it will be notified when this buffer
          // is transferred.
          stop ? std::move(data) : nullptr)));
    }
    return Status::OK();
  }

  Result<ReadBufferFull> Decompress(StreamReadBuffer* inp, StreamReadBuffer* out) override {
    auto inp_vecs = inp->AppendedVecs();
    RangeSource<IoVecs::const_iterator> source(inp_vecs.begin(), inp_vecs.end());

    auto total_consumed = 0;
    auto out_vecs = VERIFY_RESULT(out->PrepareAppend());
    auto total_output = IoVecsFullSize(out_vecs);
    IoVecsSink sink(&out_vecs);

    size_t input_size = source.Available();
    auto result = ReadBufferFull::kFalse;
    while (total_consumed + kHeaderLen <= input_size) {
      source.Limit(input_size - total_consumed);
      // Fetch chunk len.
      size_t len = 0;
      auto data = source.Peek(&len);
      size_t size;
      if (len >= kHeaderLen) {
        // Size fully contained in the first buffer.
        size = BigEndian::Load16(data);
        source.Skip(kHeaderLen);
      } else {
        // Size distributed between 2 blocks.
        // Since blocks are not empty and size exactly 2, we should expect that first block
        // contains 1 byte of size.
        if (len != 1) {
          return STATUS(RuntimeError, "Expected to peek at least one byte");
        }
        char buf[kHeaderLen];
        buf[0] = *data;
        source.Skip(1);
        data = source.Peek(&len);
        buf[1] = *data;
        source.Skip(1);
        size = BigEndian::Load16(buf);
      }
      // Check whether we already received full chunk.
      VLOG_WITH_FUNC(4)
          << "Total consumed: " << total_consumed << ", size: " << size << ", input: "
          << input_size;
      if (total_consumed + kHeaderLen + size > input_size) {
        break;
      }
      source.Limit(size);
      uint32_t length = 0;
      auto old_available = source.Available();
      if (!snappy::GetUncompressedLength(&source, &length)) {
        return STATUS(RuntimeError, "GetUncompressedLength failed");
      }

      // Check if we have space for decompressed block.
      VLOG_WITH_FUNC(4)
          << "Total appended: " << sink.total_appended() << ", length: " << length
          << ", total_output: " << total_output;
      if (sink.total_appended() + length > total_output) {
        result = ReadBufferFull::kTrue;
        break;
      }
      // Rollback to state before GetUncompressedLength.
      source.Rewind(old_available - source.Available());
      if (!snappy::Uncompress(&source, &sink)) {
        return STATUS(RuntimeError, "Decompression failed");
      }
      total_consumed += kHeaderLen + size;
    }

    out->DataAppended(sink.total_appended());
    inp->Consume(total_consumed, Slice());

    return result;
  }
};

// LZ4 could compress/decompress only continuous block of memory into similar block.
// So we do that same as for Snappy, but decompression is even more complex.
constexpr size_t kLZ4HeaderLen = 2;
const size_t kLZ4MaxChunkSize = FindMaxChunkSize(kLZ4HeaderLen, &LZ4_compressBound);
const size_t kLZ4BufferSize = 64_KB;

class LZ4DecompressState {
 public:
  LZ4DecompressState(char* input_buffer, char* output_buffer, Slice* prev_decompress_data_left)
      : input_buffer_(input_buffer), output_buffer_(output_buffer),
        prev_decompress_data_left_(prev_decompress_data_left) {}

  Result<ReadBufferFull> Decompress(StreamReadBuffer* inp, StreamReadBuffer* out) {
    outvecs_ = VERIFY_RESULT(out->PrepareAppend());
    out_it_ = outvecs_.begin();

    VLOG_WITH_FUNC(4) << "prev_decompress_data_left: " << prev_decompress_data_left_->size();

    // Check if we previously decompressed some data that did not fit into output buffer.
    // So copy it now. See DecompressChunk for details.
    while (!prev_decompress_data_left_->empty()) {
      auto len = std::min(prev_decompress_data_left_->size(), out_it_->iov_len);
      memcpy(out_it_->iov_base, prev_decompress_data_left_->data(), len);
      total_add_ += len;
      IoVecRemovePrefix(len, &*out_it_);
      prev_decompress_data_left_->remove_prefix(len);
      if (out_it_->iov_len == 0) {
        if (++out_it_ == outvecs_.end()) {
          out->DataAppended(total_add_);
          return ReadBufferFull(out->Full());
        }
      }
    }

    // Remaining piece of data in the previous vec.
    Slice prev_input_slice;
    for (const auto& input_vec : inp->AppendedVecs()) {
      Slice input_slice(static_cast<char*>(input_vec.iov_base), input_vec.iov_len);
      VLOG_WITH_FUNC(4) << "input_slice: " << input_slice.size();
      if (!prev_input_slice.empty()) {
        size_t chunk_size;
        if (prev_input_slice.size() >= kLZ4HeaderLen) {
          // Size fully contained in the previous block.
          chunk_size = BigEndian::Load16(prev_input_slice.data());
        } else if (prev_input_slice.size() + input_slice.size() < kLZ4HeaderLen) {
          // Did not receive header yet. So exit and wait for more data received.
          break;
        } else {
          // Size distributed between 2 blocks.
          char buf[kLZ4HeaderLen];
          memcpy(buf, prev_input_slice.data(), prev_input_slice.size());
          memcpy(buf + prev_input_slice.size(), input_slice.data(),
                 kLZ4HeaderLen - prev_input_slice.size());
          chunk_size = BigEndian::Load16(buf);
        }
        if (kLZ4HeaderLen + chunk_size > prev_input_slice.size() + input_slice.size()) {
          // Did not receive full block yet. So exit and wait for more data received.
          // TODO Here we rely on the fact that we use circular buffer and could have at most 2
          //      blocks. In general case chunk could be distributed across several buffers.
          break;
        }
        if (prev_input_slice.size() > kLZ4HeaderLen) {
          // Data is distributed between 2 buffers.
          // Have to copy it into separate input buffer so it will be a single continuous block.
          size_t size_in_prev_slice = prev_input_slice.size() - kLZ4HeaderLen;
          size_t size_in_current_slice = chunk_size - size_in_prev_slice;
          memcpy(input_buffer_, prev_input_slice.data() + kLZ4HeaderLen, size_in_prev_slice);
          memcpy(input_buffer_ + size_in_prev_slice, input_slice.data(), size_in_current_slice);
          RETURN_NOT_OK(DecompressChunk(Slice(input_buffer_, chunk_size)));
          input_slice.remove_prefix(size_in_current_slice);
        } else {
          // Only header (or part of header) was in previous buffer.
          // Could decompress from current buffer.
          input_slice.remove_prefix(kLZ4HeaderLen - prev_input_slice.size());
          RETURN_NOT_OK(DecompressChunk(input_slice.Prefix(chunk_size)));
          input_slice.remove_prefix(chunk_size);
        }
      }
      // Decompress all chunks contained in current buffer.
      while (input_slice.size() >= kLZ4HeaderLen && out_it_ != outvecs_.end()) {
        size_t chunk_size = BigEndian::Load16(input_slice.data());
        if (input_slice.size() < kLZ4HeaderLen + chunk_size) {
          break;
        }
        input_slice.remove_prefix(kLZ4HeaderLen);
        RETURN_NOT_OK(DecompressChunk(input_slice.Prefix(chunk_size)));
        input_slice.remove_prefix(chunk_size);
      }
      prev_input_slice = input_slice;
      // DecompressChunk could increase out_it_, so check whether we still have output space.
      if (out_it_ == outvecs_.end()) {
        break;
      }
    }
    inp->Consume(total_consumed_, Slice());
    out->DataAppended(total_add_);
    return ReadBufferFull(out->Full());
  }

 private:
  Status DecompressChunk(const Slice& input) {
    int res = LZ4_decompress_safe(
        input.cdata(), static_cast<char*>(out_it_->iov_base), narrow_cast<int>(input.size()),
        narrow_cast<int>(out_it_->iov_len));
    if (res <= 0) {
      // Unfortunately LZ4 does not provide information whether decryption failed because
      // of wrong data or it just does not fit into output buffer.
      // Try to decode to buffer that is big enough for max possible decompressed chunk.
      res = LZ4_decompress_safe(
          input.cdata(), output_buffer_, narrow_cast<int>(input.size()), kLZ4BufferSize);
      if (res <= 0) {
        return STATUS_FORMAT(RuntimeError, "Decompress failed: $0", res);
      }

      // Copy data from output buffer to provided read buffer.
      size_t size = res;
      char* buf = output_buffer_;
      while (out_it_ != outvecs_.end()) {
        size_t len = std::min<size_t>(size, out_it_->iov_len);
        memcpy(out_it_->iov_base, buf, len);
        IoVecRemovePrefix(len, &*out_it_);
        size -= len;
        total_add_ += len;
        buf += len;
        if (out_it_->iov_len == 0) {
          // Need to fill next output io vec.
          ++out_it_;
        }
        if (size == 0) {
          // Fully copied all decompressed data.
          break;
        }
      }
      if (size != 0) {
        // Have more decompressed data than provided read buffer could accept.
        *prev_decompress_data_left_ = Slice(buf, size);
      }
    } else {
      IoVecRemovePrefix(res, &*out_it_);
      total_add_ += res;
      if (out_it_->iov_len == 0) {
        ++out_it_;
      }
    }

    // Header was consumed by the caller, so we also add it here.
    total_consumed_ += kLZ4HeaderLen + input.size();
    return Status::OK();
  }

  // LZ4 operates on continuous chunks of memory, so we use input and output buffers to combine
  // several iovecs into one continuous chunk when necessary.
  char* input_buffer_;
  char* output_buffer_;
  Slice* prev_decompress_data_left_;

  IoVecs outvecs_;
  IoVecs::iterator out_it_;
  size_t total_add_ = 0;
  size_t total_consumed_ = 0;
};

class LZ4Compressor : public Compressor {
 public:
  static constexpr char kId = 'L';
  static constexpr int kIndex = 3;
  static constexpr size_t kHeaderLen = kLZ4HeaderLen;

  explicit LZ4Compressor(MemTrackerPtr mem_tracker) {
    if (mem_tracker) {
      consumption_ = ScopedTrackedConsumption(std::move(mem_tracker), 2 * kLZ4BufferSize);
    }
  }

  OutboundDataPtr ConnectionHeader() override {
    return GetConnectionHeader<LZ4Compressor>();
  }

  Status Init() override {
    return Status::OK();
  }

  std::string ToString() const override {
    return "LZ4";
  }

  Status Compress(
      const SmallRefCntBuffers& input, RefinedStream* stream, OutboundDataPtr data)
      ON_REACTOR_THREAD override {
    // Increment iterator in loop body to be able to check whether it is last iteration or not.
    VLOG_WITH_FUNC(4) << "input: " << CollectionToString(input, [](const auto& buf) {
      return buf.size();
    });
    for (auto input_it = input.begin(); input_it != input.end();) {
      Slice input_slice = input_it->AsSlice();
      ++input_it;
      while (!input_slice.empty()) {
        Slice chunk;
        // Split input into chunks of size kLZ4MaxChunkSize or less.
        if (input_slice.size() > kLZ4MaxChunkSize) {
          chunk = input_slice.Prefix(kLZ4MaxChunkSize);
        } else {
          chunk = input_slice;
        }
        VLOG_WITH_FUNC(4) << "chunk: " << chunk.size();
        input_slice.remove_prefix(chunk.size());
        RefCntBuffer output(kHeaderLen + LZ4_compressBound(narrow_cast<int>(chunk.size())));
        int res = LZ4_compress(
            chunk.cdata(), output.data() + kHeaderLen, narrow_cast<int>(chunk.size()));
        if (res <= 0) {
          return STATUS_FORMAT(RuntimeError, "LZ4 compression failed: $0", res);
        }
        BigEndian::Store16(output.data(), res);
        output.Shrink(kHeaderLen + res);
        RETURN_NOT_OK(stream->SendToLower(std::make_shared<SingleBufferOutboundData>(
            std::move(output),
            // We processed last buffer, attach data to it, so it will be notified when this buffer
            // is transferred.
            input_slice.empty() && input_it == input.end() ? std::move(data) : nullptr)));
      }
    }

    return Status::OK();
  }

  Result<ReadBufferFull> Decompress(StreamReadBuffer* inp, StreamReadBuffer* out) override {
    LZ4DecompressState state(
        decompress_input_buf_, decompress_output_buf_, &prev_decompress_data_left_);
    return state.Decompress(inp, out);
  }

 private:
  char decompress_input_buf_[kLZ4BufferSize];
  char decompress_output_buf_[kLZ4BufferSize];
  Slice prev_decompress_data_left_;
  ScopedTrackedConsumption consumption_;
};

#undef LZ4
#define YB_COMPRESSION_ALGORITHMS (Zlib)(Snappy)(LZ4)

#define YB_CREATE_COMPRESSOR_CASE(r, data, name) \
  case BOOST_PP_CAT(name, Compressor)::data: \
      return std::make_unique<BOOST_PP_CAT(name, Compressor)>(std::move(mem_tracker));

std::unique_ptr<Compressor> CreateCompressor(char sign, MemTrackerPtr mem_tracker) {
  switch (sign) {
BOOST_PP_SEQ_FOR_EACH(YB_CREATE_COMPRESSOR_CASE, kId, YB_COMPRESSION_ALGORITHMS)
    default:
      return nullptr;
  }
}

std::unique_ptr<Compressor> CreateOutboundCompressor(MemTrackerPtr mem_tracker) {
  auto algo = FLAGS_stream_compression_algo;
  if (!algo) {
    return nullptr;
  }
  switch (algo) {
BOOST_PP_SEQ_FOR_EACH(YB_CREATE_COMPRESSOR_CASE, kIndex, YB_COMPRESSION_ALGORITHMS)
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

  Status ProcessHeader() ON_REACTOR_THREAD override {
    constexpr int kHeaderLen = 3;

    auto data = stream_->ReadBuffer().AppendedVecs();
    if (data.empty() || data[0].iov_len < kHeaderLen) {
      // Did not receive enough bytes to make a decision.
      // So just wait more bytes.
      return Status::OK();
    }

    const auto* bytes = static_cast<const uint8_t*>(data[0].iov_base);
    if (bytes[0] == 'Y' && bytes[1] == 'B') {
      compressor_ = CreateCompressor(bytes[2], stream_->buffer_tracker());
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

  Status Send(OutboundDataPtr data) ON_REACTOR_THREAD override {
    boost::container::small_vector<RefCntSlice, 10> input;
    data->Serialize(&input);
    return compressor_->Compress(input, stream_, std::move(data));
  }

  Status Handshake() ON_REACTOR_THREAD override {
    if (stream_->local_side() == LocalSide::kClient) {
      compressor_ = CreateOutboundCompressor(stream_->buffer_tracker());
      if (!compressor_) {
        return stream_->Established(RefinedStreamState::kDisabled);
      }
      RETURN_NOT_OK(compressor_->Init());
      RETURN_NOT_OK(stream_->SendToLower(compressor_->ConnectionHeader()));
    }

    return stream_->Established(RefinedStreamState::kEnabled);
  }

  Result<ReadBufferFull> Read(StreamReadBuffer* out) override {
    VLOG_WITH_PREFIX(4) << __func__;

    return compressor_->Decompress(&stream_->ReadBuffer(), out);
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
