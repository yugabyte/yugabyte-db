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
#include "yb/util/zlib.h"
#include <zlib.h>

#include <memory>
#include <string>

#include "yb/gutil/casts.h"
#include "yb/gutil/macros.h"

#include "yb/util/slice.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"

using std::ostream;
using std::string;
using std::unique_ptr;

#define ZRETURN_NOT_OK(call) \
  RETURN_NOT_OK(ZlibResultToStatus(call))

namespace yb {
namespace zlib {

namespace  {
Status ZlibResultToStatus(int rc) {
  switch (rc) {
    case Z_OK:
      return Status::OK();
    case Z_STREAM_END:
      return STATUS(EndOfFile, "zlib EOF");
    case Z_NEED_DICT:
      return STATUS(Corruption, "zlib error: NEED_DICT");
    case Z_ERRNO:
      return STATUS(IOError, "zlib error: Z_ERRNO");
    case Z_STREAM_ERROR:
      return STATUS(Corruption, "zlib error: STREAM_ERROR");
    case Z_DATA_ERROR:
      return STATUS(Corruption, "zlib error: DATA_ERROR");
    case Z_MEM_ERROR:
      return STATUS(RuntimeError, "zlib error: MEM_ERROR");
    case Z_BUF_ERROR:
      return STATUS(RuntimeError, "zlib error: BUF_ERROR");
    case Z_VERSION_ERROR:
      return STATUS(RuntimeError, "zlib error: VERSION_ERROR");
    default:
      return STATUS_FORMAT(RuntimeError, "zlib error: unknown error $0", rc);
  }
}
} // anonymous namespace

Status Compress(Slice input, ostream* out) {
  return CompressLevel(input, Z_DEFAULT_COMPRESSION, out);
}

// See https://zlib.net/zlib_how.html for context on using zlib.
Status CompressLevel(Slice input, int level, ostream* out) {
  z_stream zs;
  memset(&zs, 0, sizeof(zs));
  ZRETURN_NOT_OK(deflateInit2(&zs, level, Z_DEFLATED,
                              MAX_WBITS + 16 /* enable gzip */,
                              8 /* memory level, max is 9 */,
                              Z_DEFAULT_STRATEGY));

  zs.avail_in = narrow_cast<uInt>(input.size());
  zs.next_in = const_cast<uint8_t*>(input.data());
  const int kChunkSize = 64 * 1024;
  unique_ptr<unsigned char[]> chunk(new unsigned char[kChunkSize]);
  int flush;
  do {
    zs.avail_out = kChunkSize;
    zs.next_out = chunk.get();
    flush = (zs.avail_in == 0) ? Z_FINISH : Z_NO_FLUSH;
    Status s = ZlibResultToStatus(deflate(&zs, flush));
    if (!s.ok() && !s.IsEndOfFile()) {
      deflateEnd(&zs);
      return s;
    }
    auto out_size = zs.next_out - chunk.get();
    if (out_size > 0) {
      out->write(reinterpret_cast<char *>(chunk.get()), out_size);
    }
  } while (flush != Z_FINISH);
  ZRETURN_NOT_OK(deflateEnd(&zs));
  return Status::OK();
}

// See https://zlib.net/zlib_how.html for context on using zlib.
Status Uncompress(const Slice& compressed, std::ostream* out) {
  // Initialize the z_stream at the start of the data with the
  // data size as the available input.
  z_stream zs;
  memset(&zs, 0, sizeof(zs));
  zs.next_in = const_cast<uint8_t*>(compressed.data());
  zs.avail_in = narrow_cast<uInt>(compressed.size());
  // Initialize inflation with the windowBits set to be GZIP compatible.
  // The documentation (https://www.zlib.net/manual.html#Advanced) describes that
  // Adding 16 configures inflate to decode the gzip format.
  ZRETURN_NOT_OK(inflateInit2(&zs, MAX_WBITS + 16 /* enable gzip */));
  // Continue calling inflate, decompressing data into the buffer in `zs.next_out` and writing
  // the buffer content to `out`, until an error is received or there is no more data
  // to decompress.
  Status s;
  do {
    unsigned char buf[4096];
    zs.next_out = buf;
    zs.avail_out = arraysize(buf);
    s = ZlibResultToStatus(inflate(&zs, Z_NO_FLUSH));
    if (!s.ok() && !s.IsEndOfFile()) {
      inflateEnd(&zs);
      return s;
    }
    out->write(reinterpret_cast<char *>(buf), zs.next_out - buf);
  } while (zs.avail_out == 0);
  // If we haven't returned early with a bad status, finalize inflation.
  ZRETURN_NOT_OK(inflateEnd(&zs));
  return Status::OK();
}

} // namespace zlib
} // namespace yb
