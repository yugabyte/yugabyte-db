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
#ifndef KUDU_TABLET_COMPACTION_SVG_DUMP_H_
#define KUDU_TABLET_COMPACTION_SVG_DUMP_H_

#include <ostream>
#include <unordered_set>
#include <vector>

namespace kudu {
namespace tablet {

class RowSet;

class RowSetInfo;

// Dump an SVG file which represents the candidates
// for compaction, highlighting the ones that were selected.
// Dumps in to parameter ostream. If ostream is null, then default ostream
// specified as a flag is used (see svg_dump.cc).
// The last optional parameter controls whether to print an XML header in
// the file. If true, prints the header (xml tag and DOCTYPE). Otherwise, only
// the <svg>...</svg> section is printed.
void DumpCompactionSVG(const std::vector<RowSetInfo>& candidates,
                       const std::unordered_set<RowSet*>& picked,
                       std::ostream* out = NULL,
                       bool print_xml = true);

} // namespace tablet
} // namespace kudu

#endif
