// Copyright (c) YugabyteDB, Inc.
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

#include <span>
#include <utility>

#include <boost/container/small_vector.hpp>

#include "yb/gutil/macros.h"
#include "yb/gutil/ref_counted.h"

#include "yb/yql/pggate/pg_tools.h"
#include "yb/yql/pggate/pg_doc_op.h"

#include "yb/util/result.h"

namespace yb::pggate {

class PgSession;

class YbctidReaderProvider {
  using PgSessionPtr = scoped_refptr<PgSession>;
  using Buffers = boost::container::small_vector<RefCntBuffer, 8>;
  using LightweightTableYbctids = boost::container::small_vector<LightweightTableYbctid, 8>;

 public:
  class Reader {
   public:
    ~Reader() {
      ybctids_.clear();
      holders_.clear();
    }

    void Reserve(size_t capacity) {
      DCHECK(!read_called_);
      ybctids_.reserve(capacity);
    }

    void Add(const LightweightTableYbctid& ybctid) {
      DCHECK(!read_called_);
      ybctids_.push_back(ybctid);
    }

    auto Read(
        PgOid database_id, const TableLocalityMap& tables_locality,
        const ExecParametersMutator& exec_params_mutator) {
      DCHECK(!read_called_);
      read_called_ = true;
      return DoRead(database_id, tables_locality, exec_params_mutator);
    }

   private:
    friend class YbctidReaderProvider;

    Reader(const PgSessionPtr& session, Buffers& holders, LightweightTableYbctids& ybctids)
        : session_(session), holders_(holders), ybctids_(ybctids) {
      DCHECK(holders_.empty() && ybctids_.empty());
    }

    Result<std::span<LightweightTableYbctid>> DoRead(
        PgOid database_id, const TableLocalityMap& tables_locality,
        const ExecParametersMutator& exec_params_mutator);

    const PgSessionPtr& session_;
    Buffers& holders_;
    LightweightTableYbctids& ybctids_;
    bool read_called_ = false;

    DISALLOW_COPY_AND_ASSIGN(Reader);
  };

  explicit YbctidReaderProvider(std::reference_wrapper<const PgSessionPtr> session)
      : session_(session) {}

  Reader operator()() {
    return Reader{session_, holders_, ybctids_};
  }

 private:
  const PgSessionPtr& session_;
  LightweightTableYbctids ybctids_;
  Buffers holders_;

  DISALLOW_COPY_AND_ASSIGN(YbctidReaderProvider);
};

} // namespace yb::pggate
