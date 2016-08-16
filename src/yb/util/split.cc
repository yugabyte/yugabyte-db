// Copyright (c) YugaByte, Inc.

#include "yb/util/split.h"

using std::string;

namespace yb {
namespace util {

using yb::Status;
using yb::Slice;

Status SplitArgs(const Slice& line, std::vector<Slice>* out_vector) {
  out_vector->clear();

  // Points to the current position we are looking at.
  const char* current_position = reinterpret_cast<const char *>(line.data());
  // Points to the end.
  const char* ptr_end = current_position + line.size();

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
      out_vector->push_back(Slice(current_token, current_token_length));
    }
  }
  return Status::OK();
}

}  // namespace util
}  // namespace yb
