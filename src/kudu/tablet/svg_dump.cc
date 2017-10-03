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

#include "kudu/tablet/svg_dump.h"

#include <glog/logging.h>
#include <time.h>

#include <fstream>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#include "kudu/common/encoded_key.h"
#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/stringprintf.h"
#include "kudu/gutil/strings/util.h"
#include "kudu/tablet/rowset_info.h"
#include "kudu/util/flag_tags.h"

using std::ostream;
using std::unordered_set;
using std::vector;

namespace kudu {
namespace tablet {

// Flag to dump SVGs of every compaction decision.
//
// After dumping, these may be converted to an animation using a series of
// commands like:
// $ for x in compaction-*svg ; do convert $x $x.png ; done
// $ mencoder mf://compaction*png -mf fps=1 -ovc lavc -o compactions.avi

DEFINE_string(compaction_policy_dump_svgs_pattern, "",
              "File path into which to dump SVG visualization of "
              "selected compactions. This is mostly useful in "
              "the context of unit tests and benchmarks. "
              "The special string 'TIME' will be substituted "
              "with the compaction selection timestamp.");
TAG_FLAG(compaction_policy_dump_svgs_pattern, hidden);

namespace {

// Organize the input rowsets into rows for presentation.  This simply
// distributes 'rowsets' into separate vectors in 'rows' such that
// within any given row, none of the rowsets overlap in keyspace.
void OrganizeSVGRows(const vector<RowSetInfo>& candidates,
                     vector<vector<const RowSetInfo*> >* rows) {
  rows->push_back(vector<const RowSetInfo *>());

  for (const RowSetInfo &candidate : candidates) {
    // Slot into the first row of the output which fits it
    bool found_slot = false;
    for (vector<const RowSetInfo *> &row : *rows) {
      // If this candidate doesn't intersect any other candidates in this
      // row, we can put it here.
      bool fits_in_row = true;
      for (const RowSetInfo *already_in_row : row) {
        if (candidate.Intersects(*already_in_row)) {
          fits_in_row = false;
          break;
        }
      }
      if (fits_in_row) {
        row.push_back(&candidate);
        found_slot = true;
        break;
      }
    }

    // If we couldn't find a spot in any existing row, add a new row
    // to the bottom of the SVG.
    if (!found_slot) {
      vector<const RowSetInfo *> new_row;
      new_row.push_back(&candidate);
      rows->push_back(new_row);
    }
  }
}

void DumpSVG(const vector<RowSetInfo>& candidates,
             const unordered_set<RowSet*>& picked,
             ostream* outptr) {
  CHECK(outptr) << "Dump SVG expects an ostream";
  CHECK(outptr->good()) << "Dump SVG expects a good ostream";
  using std::endl;
  ostream& out = *outptr;

  vector<vector<const RowSetInfo*> > svg_rows;
  OrganizeSVGRows(candidates, &svg_rows);

  const char *kPickedColor = "#f66";
  const char *kDefaultColor = "#666";
  const double kTotalWidth = 1200;
  const int kRowHeight = 15;
  const double kHeaderHeight = 60;
  const double kTotalHeight = kRowHeight * svg_rows.size() + kHeaderHeight;

  out << "<svg version=\"1.1\" width=\"" << kTotalWidth << "\" height=\""
      << kTotalHeight << "\""
      << " viewBox=\"0 0 " << kTotalWidth << " " << kTotalHeight << "\""
      << " xmlns=\"http://www.w3.org/2000/svg\" >" << endl;

  // Background
  out << "<rect x=\"0.0\" y=\"0\" width=\"1200.0\" height=\"" << kTotalHeight << "\""
      << " fill=\"#fff\" />" << endl;

  for (int row_index = 0; row_index < svg_rows.size(); row_index++) {
    const vector<const RowSetInfo *> &row = svg_rows[row_index];

    int y = kRowHeight * row_index + kHeaderHeight;
    for (const RowSetInfo *cand : row) {
      bool was_picked = ContainsKey(picked, cand->rowset());
      const char *color = was_picked ? kPickedColor : kDefaultColor;

      double x = cand->cdf_min_key() * kTotalWidth;
      double width = cand->width() * kTotalWidth;
      out << StringPrintf("<rect x=\"%f\" y=\"%d\" width=\"%f\" height=\"%d\" "
                          "stroke=\"#000\" fill=\"%s\"/>",
                          x, y, width, kRowHeight, color) << endl;
      out << StringPrintf("<text x=\"%f\" y=\"%d\" width=\"%f\" height=\"%d\" "
                          "fill=\"rgb(0,0,0)\">%dMB</text>",
                          x, y + kRowHeight, width, kRowHeight, cand->size_mb()) << endl;
    }
  }

  out << "</svg>" << endl;
}

void PrintXMLHeader(ostream* o) {
  CHECK(o) << "XML header printer expects an ostream";
  CHECK(o->good()) << "XML header printer expects a good ostream";
  *o << "<?xml version=\"1.0\" standalone=\"no\"?>" << std::endl;
  *o << "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" "
     << "\"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\">" << std::endl;
}

// Prepares ofstream to default dump location.
// In case any of the preparation fails or default pattern is empty,
// NULL is returned.
gscoped_ptr<ostream> PrepareOstream() {
  using std::ofstream;
  gscoped_ptr<ofstream> out;
  // Get default file name
  const string &pattern = FLAGS_compaction_policy_dump_svgs_pattern;
  if (pattern.empty()) return gscoped_ptr<ostream>();
  const string path = StringReplace(pattern, "TIME", StringPrintf("%ld", time(nullptr)), true);

  // Open
  out.reset(new ofstream(path.c_str()));
  if (!out->is_open()) {
    LOG(WARNING) << "Could not dump compaction output to " << path << ": file open failed";
    return gscoped_ptr<ostream>();
  }

  return out.PassAs<ostream>();
}

} // anonymous namespace

void DumpCompactionSVG(const vector<RowSetInfo>& candidates,
                       const unordered_set<RowSet*>& picked,
                       ostream* out,
                       bool print_xml) {
  // Get the desired pointer to the ostream
  gscoped_ptr<ostream> dfl;
  if (!out) {
    dfl = PrepareOstream();
    out = dfl.get();
    if (!out) return;
  }

  // Print out with the correct ostream
  LOG(INFO) << "Dumping SVG of DiskRowSetLayout with"
            << (print_xml ? "" : "out") << " XML header";
  if (print_xml) {
    PrintXMLHeader(out);
  }
  DumpSVG(candidates, picked, out);
}

} // namespace tablet
} // namespace kudu
