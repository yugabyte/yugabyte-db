// Copyright (c) YugaByte, Inc.

#ifndef YB_DOCDB_DOC_YQL_SCANSPEC_H
#define YB_DOCDB_DOC_YQL_SCANSPEC_H

#include "yb/common/yql_scanspec.h"
#include "yb/docdb/doc_key.h"
#include "yb/docdb/primitive_value.h"

namespace yb {
namespace docdb {

// DocDB variant of YQL scanspec.
class DocYQLScanSpec : public common::YQLScanSpec {
 public:

  DocYQLScanSpec(const Schema& schema, const DocKey& doc_key);

  DocYQLScanSpec(
      const Schema& schema, uint32_t hash_code,
      const std::vector<PrimitiveValue>& hashed_components, const YQLConditionPB* condition,
      const DocKey& start_doc_key = DocKey());

  // Return the inclusive lower and upper bounds of the scan.
  CHECKED_STATUS lower_bound(DocKey* key) const {
    return GetBoundKey(true /* lower_bound */, key);
  }

  CHECKED_STATUS upper_bound(DocKey* key) const  {
    return GetBoundKey(false /* upper_bound */, key);
  }
 private:
  // Return inclusive lower/upper range doc key considering the start_doc_key.
  CHECKED_STATUS GetBoundKey(const bool lower_bound, DocKey* key) const;

  // Returns the lower/upper doc key based on the range components.
  DocKey bound_key(const bool lower_bound) const;

  // The scan range within the hash key when a WHERE condition is specified.
  const std::unique_ptr<const common::YQLScanRange> range_;

  // Schema of the columns to scan.
  const Schema& schema_;

  // Hash code, hashed components and optional WHERE condition clause to scan.
  // The hashed_components and condition are owned by the caller of YQLScanSpec.
  const uint32_t hash_code_;
  const std::vector<PrimitiveValue>* hashed_components_;

  // Specific doc key to scan. The doc key is owned by the caller of YQLScanSpec.
  const DocKey* doc_key_;

  // Starting doc key when requested by the client.
  const DocKey start_doc_key_;

  const DocKey lower_doc_key_;
  const DocKey upper_doc_key_;
};

}  // namespace docdb
}  // namespace yb

#endif // YB_DOCDB_DOC_YQL_SCANSPEC_H
