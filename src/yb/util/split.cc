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

#include "yb/util/split.h"

#include "yb/util/status.h"

using std::string;

namespace yb {
namespace util {

using yb::Status;
using yb::Slice;

template<class Out>
Status SplitArgsImpl(const Slice& line, Out* out_vector) {
  out_vector->clear();

  // Points to the current position we are looking at.
  const char* current_position = line.cdata();
  // Points to the end.
  const char* ptr_end = line.cend();

  while (current_position != ptr_end) {
    // Skip blanks.
    while (current_position != ptr_end && isspace(*current_position)) {
      current_position++;
    }

    if (current_position != ptr_end) {
      const char* current_token = current_position;
      int current_token_length = 0;
      // Get a token.
      if (*current_position == '"' || *current_position == '\'') {
        const char quotation = *current_position;
        current_token++;  // Skip the begining quote.
        current_position++;
        while (true) {
          if (current_position == ptr_end) {
            // Unterminated quotes.
            out_vector->clear();
            return STATUS(Corruption, "Unterminated quotes.");
          }

          if (quotation == *current_position) {
            // Reached the end of the quoted token.
            //
            // Closing quote must be followed by a space or nothing at all.
            current_position++;
            if (current_position != ptr_end && !isspace(*current_position)) {
              out_vector->clear();
              return STATUS(Corruption, "A closing quote must be followed by a white space "
                  "or end of input");
            }
            break;
          }
          current_token_length++;
          current_position++;
        }
      } else {
        // Token is not begining with quotes. Process until the next space character.
        while (current_position != ptr_end && !isspace(*current_position)) {
          if (*current_position == '"' || *current_position == '\'') {
            out_vector->clear();
            return STATUS(Corruption, "A closing quote must be followed by a white space "
                "or end of input");
          }
          current_token_length++;
          current_position++;
        }
      }
      // Save the token. Get ready for the next one.
      out_vector->emplace_back(current_token, current_token_length);
    }
  }
  return Status::OK();
}

Status SplitArgs(const Slice& line, std::vector<Slice>* out_vector) {
  return SplitArgsImpl(line, out_vector);
}

Status SplitArgs(const Slice& line, boost::container::small_vector_base<Slice>* out_vector) {
  return SplitArgsImpl(line, out_vector);
}

}  // namespace util
}  // namespace yb
