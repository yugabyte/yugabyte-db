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

#pragma once

#include "yb/gutil/strings/substitute.h"
#include "yb/yql/cql/ql/ptree/list_node.h"
#include "yb/yql/cql/ql/ptree/pt_name.h"
#include "yb/yql/cql/ql/ptree/pt_property.h"
#include "yb/yql/cql/ql/ptree/pt_select.h"
#include "yb/yql/cql/ql/ptree/tree_node.h"

namespace yb {
namespace ql {

enum class PropertyType : int {
  kTableProperty = 0,
  kClusteringOrder,
  kTablePropertyMap,
};

class PTTableProperty : public PTProperty {
 public:
  enum class KVProperty : int {
    kBloomFilterFpChance,
    kCaching,
    kComment,
    kCompaction,
    kCompression,
    kCrcCheckChance,
    kDclocalReadRepairChance,
    kDefaultTimeToLive,
    kGcGraceSeconds,
    kIndexInterval,
    kMemtableFlushPeriodInMs,
    kMinIndexInterval,
    kMaxIndexInterval,
    kReadRepairChance,
    kSpeculativeRetry,
    kTransactions,
    kNumTablets
  };

  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTTableProperty> SharedPtr;
  typedef MCSharedPtr<const PTTableProperty> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  // Constructor for PropertyType::kTableProperty.
  PTTableProperty(MemoryContext *memctx,
                  YBLocationPtr loc,
                  const MCSharedPtr<MCString>& lhs_,
                  const PTExprPtr& rhs_);

  // Constructor for PropertyType::kClusteringOrder.
  PTTableProperty(MemoryContext *memctx,
                  YBLocationPtr loc,
                  const PTExprPtr& expr,
                  const PTOrderBy::Direction direction);

  PTTableProperty(MemoryContext *memctx,
                  YBLocationPtr loc);

  virtual ~PTTableProperty();

  template<typename... TypeArgs>
  inline static PTTableProperty::SharedPtr MakeShared(MemoryContext *memctx,
                                               TypeArgs&&... args) {
    return MCMakeShared<PTTableProperty>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual Status Analyze(SemContext *sem_context) override;
  void PrintSemanticAnalysisResult(SemContext *sem_context);

  virtual Status SetTableProperty(yb::TableProperties *table_property) const;

  PropertyType property_type() const {
    return property_type_;
  }

  std::string name() const;

  PTOrderBy::Direction direction() const {
    DCHECK_EQ(property_type_, PropertyType::kClusteringOrder);
    return direction_;
  }

 protected:
  bool IsValidProperty(const std::string& property_name) {
    return kPropertyDataTypes.find(property_name) != kPropertyDataTypes.end();
  }

  PTExprPtr order_expr_;
  // We just need some default values. These are overridden in various constructors.
  PTOrderBy::Direction direction_ = PTOrderBy::Direction::kASC;
  PropertyType property_type_ = PropertyType::kTableProperty;

 private:
  Status AnalyzeSpeculativeRetry(const std::string &val);

  static const std::map<std::string, PTTableProperty::KVProperty> kPropertyDataTypes;
};

std::ostream& operator<<(std::ostream& os, const PropertyType& property_type);

class PTTablePropertyListNode : public TreeListNode<PTTableProperty> {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTTablePropertyListNode> SharedPtr;
  typedef MCSharedPtr<const PTTablePropertyListNode> SharedPtrConst;

  explicit PTTablePropertyListNode(MemoryContext *memory_context,
                                   YBLocationPtr loc,
                                   const MCSharedPtr<PTTableProperty>& tnode = nullptr)
      : TreeListNode<PTTableProperty>(memory_context, loc, tnode) {
  }

  virtual ~PTTablePropertyListNode() {
  }

  // Append a PTTablePropertyList to this list.
  void AppendList(const MCSharedPtr<PTTablePropertyListNode>& tnode_list) {
    if (tnode_list == nullptr) {
      return;
    }
    for (const auto& tnode : tnode_list->node_list()) {
      Append(tnode);
    }
  }

  template<typename... TypeArgs>
  inline static PTTablePropertyListNode::SharedPtr MakeShared(MemoryContext *memctx,
                                                              TypeArgs&&...args) {
    return MCMakeShared<PTTablePropertyListNode>(memctx, std::forward<TypeArgs>(args)...);
  }

  virtual Status Analyze(SemContext *sem_context) override;
};

class PTTablePropertyMap : public PTTableProperty {
 public:
  enum class PropertyMapType : int {
    kCaching,
    kCompaction,
    kCompression,
    kTransactions
  };
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<PTTablePropertyMap> SharedPtr;
  typedef MCSharedPtr<const PTTablePropertyMap> SharedPtrConst;

  PTTablePropertyMap(MemoryContext *memctx,
                     YBLocationPtr loc);

  virtual ~PTTablePropertyMap();

  template<typename... TypeArgs>
  inline static PTTablePropertyMap::SharedPtr MakeShared(MemoryContext *memctx,
                                                         TypeArgs&&... args) {
    return MCMakeShared<PTTablePropertyMap>(memctx, std::forward<TypeArgs>(args)...);
  }

  // Node semantics analysis.
  virtual Status Analyze(SemContext *sem_context) override;
  void PrintSemanticAnalysisResult(SemContext *sem_context);

  virtual Status SetTableProperty(yb::TableProperties *table_property) const override;

  void SetPropertyName(MCSharedPtr<MCString> property_name) {
    lhs_ = property_name;
  }

  void AppendMapElement(PTTableProperty::SharedPtr table_property) {
    DCHECK_EQ(property_type_, PropertyType::kTablePropertyMap);
    map_elements_->Append(table_property);
  }

 private:
  Status AnalyzeCaching();
  Status AnalyzeCompaction();
  Status AnalyzeCompression();
  Status AnalyzeTransactions(SemContext *sem_context);

  static const std::map<std::string, PTTablePropertyMap::PropertyMapType> kPropertyDataTypes;
  TreeListNode<PTTableProperty>::SharedPtr map_elements_;
};

struct Compression {
  enum class Subproperty : int {
    kChunkLengthKb,
    kClass,
    kCrcCheckChance,
    kEnabled,
    kSstableCompression
  };

  static const std::map<std::string, Subproperty> kSubpropertyDataTypes;
};

struct Compaction {
  enum class Subproperty : int {
    kBaseTimeSeconds,
    kBucketHigh,
    kBucketLow,
    kClass,
    kCompactionWindowSize,
    kCompactionWindowUnit,
    kEnabled,
    kLogAll,
    kMaxSstableAgeDays,
    kMaxThreshold,
    kMaxWindowSizeSeconds,
    kMinSstableSize,
    kMinThreshold,
    kOnlyPurgeRepairedTombstones,
    kSstableSizeInMb,
    kTimestampResolution,
    kTombstoneCompactionInterval,
    kTombstoneThreshold,
    kUncheckedTombstoneCompaction
  };

  static const std::map<std::string, Subproperty> kSubpropertyDataTypes;

  static const std::map<std::string, std::set<Subproperty>> kClassSubproperties;

  static std::set<std::string> kWindowUnits;
  static std::set<std::string> kTimestampResolutionUnits;
};

struct Transactions {
  enum class Subproperty : int {
    kEnabled,
    kConsistencyLevel,
  };

  static constexpr auto kConsistencyLevelUserEnforced = "user_enforced";

  static const std::map<std::string, Subproperty> kSubpropertyDataTypes;
};

} // namespace ql
} // namespace yb
