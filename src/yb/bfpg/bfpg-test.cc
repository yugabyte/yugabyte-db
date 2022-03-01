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
// This is a simple test to verify the correctness of builtin function library.

#include <memory>
#include <vector>

#include "yb/bfpg/bfpg.h"

#include "yb/common/ql_value.h"

#include "yb/util/test_util.h"

namespace yb {
namespace bfpg {

using std::shared_ptr;
using std::make_shared;
using std::to_string;
using std::vector;
//--------------------------------------------------------------------------------------------------
// BFTestValue is a data value to be used with builtin library for both phases - compilation and
// execution. Note that the plan is to have two different data structures for two different.
// - PGSQL treenode is used during compilation.
// - QLValue is used during execution.
class BFTestValue : public QLValue {
 public:
  typedef std::shared_ptr<BFTestValue> SharedPtr;
  typedef std::shared_ptr<const BFTestValue> SharedPtrConst;

  BFTestValue() : QLValue() {
    ql_type_id_ = DataType::UNKNOWN_DATA;
  }

  virtual DataType ql_type_id() const {
    return ql_type_id_;
  }
  virtual void set_ql_type_id(DataType ql_type_id) {
    ql_type_id_ = ql_type_id;
  }

 private:
  DataType ql_type_id_;
};

//--------------------------------------------------------------------------------------------------
// Construct BFApiTest classes from the builtin template. This test is kept to be generic for all
// applications, so it defines its own type. For specific application, appropriate datatypes should
// be used in its tests.
using BFCompileApiTest = BFCompileApi<BFTestValue, BFTestValue>;
using BFExecApiTest = BFExecImmediateApi<BFTestValue, BFTestValue>;

//--------------------------------------------------------------------------------------------------
// This is the driver for bfpg-test.
class BfPgsqlTest : public YBTest {
 public:
  // Constructor and destructor.
  BfPgsqlTest() {
  }

  virtual ~BfPgsqlTest() {
  }

  //------------------------------------------------------------------------------------------------
  // Test start and cleanup functions.
  void SetUp() override {
    YBTest::SetUp();
  }

  void TearDown() override {
    YBTest::TearDown();
  }

  // Convert the param values to datatypes that are specified by "bfdecl".
  Status ConvertParams(const BFDecl *bfdecl,
                       const vector<BFTestValue::SharedPtr>& params,
                       vector<BFTestValue::SharedPtr> *converted_params) {
    const auto pcount = params.size();
    converted_params->resize(pcount);
    const std::vector<DataType>& ptypes = bfdecl->param_types();

    bool is_variadic = false;
    vector<BFTestValue::SharedPtr> cast_params(2);
    for (size_t pindex = 0; pindex < pcount; pindex++) {
      if (is_variadic || ptypes[pindex] == DataType::TYPEARGS) {
        // No conversion is needed for the rest of the arguments.
        is_variadic = true;
        (*converted_params)[pindex] = params[pindex];

      } else if (params[pindex]->ql_type_id() == ptypes[pindex]) {
        (*converted_params)[pindex] = params[pindex];

      } else {
        // Casting is needed.
        BFTestValue::SharedPtr converted_param = make_shared<BFTestValue>();
        converted_param->set_ql_type_id(ptypes[pindex]);

        // Converting params.
        cast_params[0] = params[pindex];
        cast_params[1] = converted_param;
        RETURN_NOT_OK(BFExecApiTest::ExecPgsqlFunc(
            bfpg::kCastFuncName, cast_params, converted_param));

        // Save converted value.
        (*converted_params)[pindex] = converted_param;
      }
    }

    return Status::OK();
  }
};

//--------------------------------------------------------------------------------------------------
// The following test cases generally go through the following steps.
//
// - Use the BFTestValue functions to set the values and datatypes for parameter.
// - Similarly, use the BFTestValue functions to set the datatypes for return result.
//   NOTE: When the return type is not set, builtin-library will set it. On the other hand, if the
//   return type is set, the builtin-library will check if the datatype is compatible with the
//   definition of the builtin function.
// - Call FindPgsqlOpcode() to find the opcode. This step should be done during compilation.
// - Call ExecPgsqlFunc() to run the builtin function. This step will be done during execution.

// Test calls to functions with arguments whose datatypes are an exact matched to the signature.
TEST_F(BfPgsqlTest, TestExactMatchSignature) {
  BFOpcode opcode;
  const BFDecl *bfdecl;

  BFTestValue::SharedPtr result = make_shared<BFTestValue>();
  BFTestValue::SharedPtr param0 = make_shared<BFTestValue>();
  BFTestValue::SharedPtr param1 = make_shared<BFTestValue>();

  // Use raw pointer to test the API for raw pointers.
  vector<BFTestValue::SharedPtr> params = { param0, param1 };

  //------------------------------------------------------------------------------------------------
  // Test cases of exact match calls for integer functions.
  param0->set_ql_type_id(DataType::INT64);
  param1->set_ql_type_id(DataType::INT64);

  // Test Case 1: Not specify the return type by setting it to NULL.
  //    UNKNOWN = INT64 + INT64.
  // Set result type to be unknown and let builtin library resolve its type.
  result->set_ql_type_id(DataType::UNKNOWN_DATA);

  // Initialize parameter values.
  int int_val1 = 2133;
  int int_val2 = 1234;
  param0->set_int64_value(int_val1);
  param1->set_int64_value(int_val2);

  // Find and execute the opcode.
  ASSERT_OK(BFCompileApiTest::FindPgsqlOpcode("+", params, &opcode, &bfdecl, result));
  ASSERT_OK(BFExecApiTest::ExecPgsqlOpcode(opcode, params, result));

  // Write the result to an integer and check the result.
  int expected_int_result = int_val1 + int_val2;
  auto return_int_result = result->int64_value();
  ASSERT_EQ(return_int_result, expected_int_result);

  // Test Case 2: The return type is exact match
  //    INT64 = INT64 + INT64.
  // Similar to test #1, but set result type to INT32 let builtin library does the type-checking.
  result->set_ql_type_id(DataType::INT64);
  int_val1 = 4133;
  int_val2 = 7234;
  param0->set_int64_value(int_val1);
  param1->set_int64_value(int_val2);
  ASSERT_OK(BFCompileApiTest::FindPgsqlOpcode("+", params, &opcode, &bfdecl, result));
  ASSERT_EQ(result->ql_type_id(), DataType::INT64);
  ASSERT_OK(BFExecApiTest::ExecPgsqlOpcode(opcode, params, result));

  expected_int_result = int_val1 + int_val2;
  return_int_result = result->int64_value();
  ASSERT_EQ(return_int_result, expected_int_result);

  // Test Case 3: The return type is compatible to force a conversion for return value.
  //    INT16 = INT64 + INT64.
  // Similar to test #1, but set result type to INT16.
  BFTestValue::SharedPtr temp_result = make_shared<BFTestValue>();
  temp_result->set_ql_type_id(DataType::UNKNOWN_DATA);

  int_val1 = 3133;
  int_val2 = 9234;
  param0->set_int64_value(int_val1);
  param1->set_int64_value(int_val2);
  ASSERT_OK(BFCompileApiTest::FindPgsqlOpcode("+", params, &opcode, &bfdecl, temp_result));
  ASSERT_EQ(temp_result->ql_type_id(), DataType::INT64);
  ASSERT_OK(BFExecApiTest::ExecPgsqlOpcode(opcode, params, temp_result));

  // Convert int64 value (temp_result) to int16 value (result).
  result->set_ql_type_id(DataType::INT16);
  vector<BFTestValue::SharedPtr> temp_params = { temp_result, result };
  ASSERT_OK(BFExecApiTest::ExecPgsqlFunc(bfpg::kCastFuncName, temp_params, result));

  // Check result.
  expected_int_result = int_val1 + int_val2;
  return_int_result = result->int16_value();
  ASSERT_EQ(return_int_result, expected_int_result);

  //------------------------------------------------------------------------------------------------
  // Test exact match calls for double functions.
  // The steps in this test is the same as for integer.
  // Parameter type is now set to double.
  param0->set_ql_type_id(DataType::DOUBLE);
  param1->set_ql_type_id(DataType::DOUBLE);

  // - Case 1: Not specify the return type by setting it to NULL.
  //     UNKNOWN = DOUBLE + DOUBLE.
  result->set_ql_type_id(DataType::UNKNOWN_DATA);

  double d_val1 = 777.7;
  double d_val2 = 1111.7;
  param0->set_double_value(d_val1);
  param1->set_double_value(d_val2);
  ASSERT_OK(BFExecApiTest::ExecPgsqlFunc("+", params, result));
  ASSERT_EQ(result->ql_type_id(), DataType::DOUBLE);

  // Write the return value to an int so that we can run EQ check.
  expected_int_result = d_val1 + d_val2;
  return_int_result = result->double_value();
  ASSERT_EQ(return_int_result, expected_int_result);

  // - Case 2: Have exact match return type.
  //     DOUBLE = DOUBLE + DOUBLE.
  result->set_ql_type_id(DataType::DOUBLE);

  d_val1 = 999.9;
  d_val2 = 3333.3;
  param0->set_double_value(d_val1);
  param1->set_double_value(d_val2);
  ASSERT_OK(BFExecApiTest::ExecPgsqlFunc("+", params, result));

  expected_int_result = d_val1 + d_val2;
  return_int_result = result->double_value();
  ASSERT_EQ(return_int_result, expected_int_result);

  // - Case 3: Have compatible return type.
  //     FLOAT = DOUBLE + DOUBLE.
  d_val1 = 888.9;
  d_val2 = 8888.3;
  param0->set_double_value(d_val1);
  param1->set_double_value(d_val2);

  // Execute (double + double) and convert double(temp_result) to float(result).
  result->set_ql_type_id(DataType::FLOAT);
  temp_result->set_ql_type_id(DataType::DOUBLE);
  ASSERT_OK(BFExecApiTest::ExecPgsqlFunc("+", params, temp_result));
  ASSERT_OK(BFExecApiTest::ExecPgsqlFunc("cast", temp_params, result));

  expected_int_result = d_val1 + d_val2;
  return_int_result = result->float_value();
  ASSERT_EQ(return_int_result, expected_int_result);

  //------------------------------------------------------------------------------------------------
  // Test exact match calls for string functions.
  // Test case: STRING = STRING + STRING
  result->set_ql_type_id(DataType::STRING);

  param0->set_ql_type_id(DataType::STRING);
  param1->set_ql_type_id(DataType::STRING);
  param0->set_string_value("First part of String. ");
  param1->set_string_value("Second part of String.");

  ASSERT_OK(BFExecApiTest::ExecPgsqlFunc("+", params, result));
  ASSERT_EQ(result->string_value(), "First part of String. Second part of String.");
}

// Test calls to functions with arguments whose datatypes are convertible but not an exact match to
// the function signature.
TEST_F(BfPgsqlTest, TestCompatibleSignature) {
  BFOpcode opcode;
  const BFDecl *bfdecl;

  BFTestValue::SharedPtr result = make_shared<BFTestValue>();
  BFTestValue::SharedPtr param0 = make_shared<BFTestValue>();
  BFTestValue::SharedPtr param1 = make_shared<BFTestValue>();

  // Use shared pointer to test the API for shared_ptr.
  vector<BFTestValue::SharedPtr> params = { param0, param1 };
  vector<BFTestValue::SharedPtr> converted_params;

  //------------------------------------------------------------------------------------------------
  // Test case: Passing (STRING, INT16) to (STRING, DOUBLE)

  // Set result type to be unknown and let builtin library resolve its type.
  result->set_ql_type_id(DataType::UNKNOWN_DATA);

  // Initialize parameter datatypes and values.
  param0->set_ql_type_id(DataType::STRING);
  param1->set_ql_type_id(DataType::INT16);
  param0->set_string_value("The value is ");
  param1->set_int16_value(100);

  // Find the opcode.
  ASSERT_OK(BFCompileApiTest::FindPgsqlOpcode("+", params, &opcode, &bfdecl, result));
  ASSERT_OK(ConvertParams(bfdecl, params, &converted_params));

  // Execute the opcode.
  ASSERT_OK(BFExecApiTest::ExecPgsqlOpcode(opcode, converted_params, result));

  // Write the result to a string and check the result.
  string expected_result = string("The value is ") + to_string(100.);
  string return_result = result->string_value();
  ASSERT_EQ(return_result, expected_result);

  //------------------------------------------------------------------------------------------------
  // Test case: Passing (INT64, STRING) to (DOUBLE, STRING)

  // Set result type to be unknown and let builtin library resolve its type.
  result->set_ql_type_id(DataType::UNKNOWN_DATA);

  // Initialize parameter datatypes and values.
  param0->set_ql_type_id(DataType::INT64);
  param1->set_ql_type_id(DataType::STRING);
  param0->set_int64_value(100);
  param1->set_string_value(" is the value");

  // Find the opcode.
  ASSERT_OK(BFCompileApiTest::FindPgsqlOpcode("+", params, &opcode, &bfdecl, result));
  ASSERT_OK(ConvertParams(bfdecl, params, &converted_params));

  // Execute the opcode.
  ASSERT_OK(BFExecApiTest::ExecPgsqlOpcode(opcode, converted_params, result));

  // Write the result to a string and check the result.
  expected_result = to_string(100.) + string(" is the value");
  return_result = result->string_value();
  ASSERT_EQ(return_result, expected_result);
}

// Test bad function calls.
TEST_F(BfPgsqlTest, TestErroneousFuncCalls) {
  BFOpcode opcode;
  const BFDecl *bfdecl;

  BFTestValue::SharedPtr result = make_shared<BFTestValue>();
  vector<BFTestValue::SharedPtr> params;

  // Invalid function name.
  ASSERT_NOK(BFCompileApiTest::FindPgsqlOpcode("wrong_name", params, &opcode, &bfdecl, result));

  //------------------------------------------------------------------------------------------------
  // Test for invalid parameter count.
  // Passing 0 argument to '+', which takes exactly 2 arguments.
  ASSERT_NOK(BFCompileApiTest::FindPgsqlOpcode("+", params, &opcode, &bfdecl, result));

  // Passing 1 argument to '+', which takes exactly 2 arguments.
  BFTestValue::SharedPtr param0 = make_shared<BFTestValue>();
  params.push_back(param0);
  param0->set_ql_type_id(DataType::INT32);
  ASSERT_NOK(BFCompileApiTest::FindPgsqlOpcode("+", params, &opcode, &bfdecl, result));

  BFTestValue::SharedPtr param1 = make_shared<BFTestValue>();
  param1->set_ql_type_id(DataType::INT32);
  params.push_back(param1);

  // Passing 3 arguments to '+', which takes exactly 2 arguments.
  BFTestValue::SharedPtr param2 = make_shared<BFTestValue>();
  param2->set_ql_type_id(DataType::INT32);
  params.push_back(param2);
  ASSERT_NOK(BFCompileApiTest::FindPgsqlOpcode("+", params, &opcode, &bfdecl, result));

  //------------------------------------------------------------------------------------------------
  // Test for invalid parameter types.
  params.resize(2);
  param0->set_ql_type_id(DataType::INT32);
  param1->set_ql_type_id(DataType::BOOL);
  ASSERT_NOK(BFCompileApiTest::FindPgsqlOpcode("+", params, &opcode, &bfdecl, result));

  param0->set_ql_type_id(DataType::BOOL);
  param1->set_ql_type_id(DataType::INT32);
  ASSERT_NOK(BFCompileApiTest::FindPgsqlOpcode("+", params, &opcode, &bfdecl, result));

  //------------------------------------------------------------------------------------------------
  // Test for invalid return type.
  // The builtin call will set the return-type after its evaluate the function calls. Howver, if
  // the return_type is set by application, the builtin-call will check if the given return type
  // is compatible.
  param0->set_ql_type_id(DataType::INT32);
  param1->set_ql_type_id(DataType::INT32);

  result->set_ql_type_id(DataType::STRING);
  ASSERT_NOK(BFCompileApiTest::FindPgsqlOpcode("+", params, &opcode, &bfdecl, result));

  result->set_ql_type_id(DataType::BOOL);
  ASSERT_NOK(BFCompileApiTest::FindPgsqlOpcode("+", params, &opcode, &bfdecl, result));

  //------------------------------------------------------------------------------------------------
  // Test for ambiguous signature - Too many builtin match the signature of a function call.
  // The following can be matched with both signature INT(INT, INT) and DOUBLE(DOUBLE, DOUBLE).
  param0->set_ql_type_id(DataType::INT8);
  param1->set_ql_type_id(DataType::INT8);
  ASSERT_NOK(BFCompileApiTest::FindPgsqlOpcode("+", params, &opcode, &bfdecl, result));
}

} // namespace bfpg
} // namespace yb
