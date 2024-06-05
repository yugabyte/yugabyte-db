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
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include <gtest/gtest.h>

#include "yb/common/common_fwd.h"
#include "yb/common/transaction.h"
#include "yb/common/transaction.fwd.h"
#include "yb/common/transaction.messages.h"
#include "yb/common/transaction.pb.h"

#include "yb/util/result.h"
#include "yb/util/status_fwd.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/uint_set.h"

namespace yb {
namespace tablet {

class SubtxnSetAndPBTest : public YBTest {
 protected:
  bool VerifySet(const std::shared_ptr<SubtxnSetAndPB>& subtxn_info) const {
    return set == subtxn_info->set();
  }

  bool VerifyPB(const std::shared_ptr<SubtxnSetAndPB>& subtxn_info) const {
    return boost::range::equal(pb.set(), subtxn_info->pb().set());
  }

  bool Verify(const std::shared_ptr<SubtxnSetAndPB>& subtxn_info) const {
    return VerifySet(subtxn_info) && VerifyPB(subtxn_info);
  }

  Status SetRange(uint32_t lo, uint32_t hi) {
    RETURN_NOT_OK(set.SetRange(lo, hi));
    pb.clear_set();
    set.ToPB(pb.mutable_set());
    lw_subtxn_set_pb = rpc::MakeSharedMessage<::yb::LWSubtxnSetPB>();
    set.ToPB(lw_subtxn_set_pb->mutable_set());
    return Status::OK();
  }

  SubtxnSet set;
  SubtxnSetPB pb;
  std::shared_ptr<::yb::LWSubtxnSetPB> lw_subtxn_set_pb;
};

TEST_F(SubtxnSetAndPBTest, TestCreate) {
  ASSERT_OK(SetRange(1, 5));
  auto res = SubtxnSetAndPB::Create(*lw_subtxn_set_pb);
  ASSERT_OK(res);
  ASSERT_TRUE(Verify(*res));

  ASSERT_OK(SetRange(6, 7));
  ASSERT_FALSE(Verify(*res));
  SubtxnSetPB pb_copy = pb;
  res = SubtxnSetAndPB::Create(std::move(pb_copy));
  ASSERT_OK(res);
  ASSERT_TRUE(Verify(*res));

  ASSERT_OK(SetRange(9, 10));
  ASSERT_FALSE(Verify(*res));
  res = SubtxnSetAndPB::Create(*lw_subtxn_set_pb);
  ASSERT_OK(res);
  ASSERT_TRUE(Verify(*res));
}

} // namespace tablet
} // namespace yb
