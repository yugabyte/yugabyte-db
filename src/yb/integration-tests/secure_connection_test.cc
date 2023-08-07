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

#include "yb/client/ql-dml-test-base.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"

#include "yb/common/ql_value.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/secure_stream.h"

#include "yb/server/secure.h"

#include "yb/util/size_literals.h"
#include "yb/util/env_util.h"

#include "yb/yql/cql/ql/util/errcodes.h"
#include "yb/yql/cql/ql/util/statement_result.h"

DECLARE_bool(TEST_private_broadcast_address);
DECLARE_bool(allow_insecure_connections);
DECLARE_bool(enable_stream_compression);
DECLARE_bool(node_to_node_encryption_use_client_certificates);
DECLARE_bool(use_client_to_server_encryption);
DECLARE_bool(use_node_to_node_encryption);
DECLARE_bool(verify_client_endpoint);
DECLARE_bool(verify_server_endpoint);
DECLARE_int32(TEST_nodes_per_cloud);
DECLARE_int32(stream_compression_algo);
DECLARE_int32(yb_client_admin_operation_timeout_sec);
DECLARE_string(TEST_public_hostname_suffix);
DECLARE_string(cert_file_pattern);
DECLARE_string(certs_dir);
DECLARE_string(cipher_list);
DECLARE_string(ciphersuites);
DECLARE_string(key_file_pattern);
DECLARE_string(node_to_node_encryption_required_uid);
DECLARE_string(ssl_protocols);

using namespace std::literals;

namespace yb {

class SecureConnectionTest : public client::KeyValueTableTest<MiniCluster> {
 public:
  SecureConnectionTest() {
  }

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_node_to_node_encryption) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_use_client_to_server_encryption) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_allow_insecure_connections) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_public_hostname_suffix) = ".ip.yugabyte";
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_private_broadcast_address) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_certs_dir) = CertsDir();

    KeyValueTableTest::SetUp();

    DontVerifyClusterBeforeNextTearDown(); // Verify requires insecure connection.
  }

  virtual std::string CertsDir() { return GetCertsDir(); }

  Status CreateClient() override {
    auto host = "127.0.0.52";
    client_ = VERIFY_RESULT(cluster_->CreateSecureClient(host, host, &secure_context_));
    return Status::OK();
  }

  Result<std::unique_ptr<client::YBClient>> CreateBadClient() {
    google::FlagSaver flag_saver;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_yb_client_admin_operation_timeout_sec) = 5;
    auto name = "127.0.0.54";
    auto host = "127.0.0.52";
    return cluster_->CreateSecureClient(name, host, &bad_secure_context_);
  }

  void TestSimpleOps();

  std::unique_ptr<rpc::SecureContext> secure_context_;
  std::unique_ptr<rpc::SecureContext> bad_secure_context_;
};

void SecureConnectionTest::TestSimpleOps() {
  CreateTable(client::Transactional::kFalse);

  const int32_t kKey = 1;
  const int32_t kValue = 2;

  {
    auto session = NewSession();
    auto op = ASSERT_RESULT(WriteRow(session, kKey, kValue));
    ASSERT_EQ(op->response().status(), QLResponsePB::YQL_STATUS_OK);
  }

  {
    auto value = ASSERT_RESULT(SelectRow(NewSession(), kKey));
    ASSERT_EQ(kValue, value);
  }
}

TEST_F(SecureConnectionTest, Simple) {
  TestSimpleOps();
}

TEST_F(SecureConnectionTest, CertificateDetails) {
  TestSimpleOps();

  auto certDetails = secure_context_->GetCertificateDetails();
  ASSERT_STR_CONTAINS(certDetails, "Node certificate details");
  ASSERT_STR_CONTAINS(certDetails, "Issuer");
  ASSERT_STR_CONTAINS(certDetails, "Serial Number");
  ASSERT_STR_CONTAINS(certDetails, "Validity");
  ASSERT_STR_CONTAINS(certDetails, "Not Before");
  ASSERT_STR_CONTAINS(certDetails, "Not After");
  ASSERT_STR_CONTAINS(certDetails, "Subject");
}

class SecureConnectionTLS12Test : public SecureConnectionTest {
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ssl_protocols) = "tls12";
    SecureConnectionTest::SetUp();
  }
};

TEST_F_EX(SecureConnectionTest, TLS12, SecureConnectionTLS12Test) {
  TestSimpleOps();

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ssl_protocols) = "ssl2 ssl3,tls10 tls11";
  ASSERT_NOK(CreateBadClient());
}

TEST_F(SecureConnectionTest, BigWrite) {
  client::YBSchemaBuilder builder;
  builder.AddColumn(kKeyColumn)->Type(DataType::INT32)->HashPrimaryKey()->NotNull();
  builder.AddColumn(kValueColumn)->Type(DataType::STRING);

  ASSERT_OK(table_.Create(client::kTableName, 1, client_.get(), &builder));

  const int32_t kKey = 1;
  const std::string kValue(64_KB, 'X');

  auto session = NewSession();
  {
    const auto op = table_.NewWriteOp(QLWriteRequestPB::QL_STMT_INSERT);
    auto* const req = op->mutable_request();
    QLAddInt32HashValue(req, kKey);
    table_.AddStringColumnValue(req, kValueColumn, kValue);
    ASSERT_OK(session->TEST_ApplyAndFlush(op));
    ASSERT_OK(CheckOp(op.get()));
  }

  {
    const auto op = table_.NewReadOp();
    auto* const req = op->mutable_request();
    QLAddInt32HashValue(req, kKey);
    table_.AddColumns({kValueColumn}, req);
    ASSERT_OK(session->TEST_ApplyAndFlush(op));
    ASSERT_OK(CheckOp(op.get()));
    auto rowblock = yb::ql::RowsResult(op.get()).GetRowBlock();
    ASSERT_EQ(rowblock->row_count(), 1);
    ASSERT_EQ(kValue, rowblock->row(0).column(0).string_value());
  }
}

class SecureConnectionWithClientCertificatesTest : public SecureConnectionTest {
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_nodes_per_cloud) = 100;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_node_to_node_encryption_use_client_certificates) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_verify_client_endpoint) = true;
    SecureConnectionTest::SetUp();
  }
};

TEST_F_EX(SecureConnectionTest, ClientCertificates, SecureConnectionWithClientCertificatesTest) {
  TestSimpleOps();

  ASSERT_NOK(CreateBadClient());
}

class SecureConnectionVerifyNameOnlyTest : public SecureConnectionTest {
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_nodes_per_cloud) = 100;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_node_to_node_encryption_use_client_certificates) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_node_to_node_encryption_required_uid) = "yugabyte-test";
    SecureConnectionTest::SetUp();
  }

  std::string CertsDir() override { return JoinPathSegments(GetCertsDir(), "named"); }
};

TEST_F_EX(SecureConnectionTest, VerifyNameOnly, SecureConnectionVerifyNameOnlyTest) {
  TestSimpleOps();
}

class SecureConnectionCipherListTest : public SecureConnectionTest {
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_cipher_list) = "HIGH";
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ssl_protocols) = "tls12";
    SecureConnectionTest::SetUp();
  }
};

TEST_F_EX(SecureConnectionTest, CipherList, SecureConnectionCipherListTest) {
  TestSimpleOps();
}

class SecureConnectionCipherSuitesTest : public SecureConnectionTest {
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ciphersuites) = "TLS_AES_128_CCM_8_SHA256";
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ssl_protocols) = "tls13";
    SecureConnectionTest::SetUp();
  }
};

TEST_F_EX(SecureConnectionTest, CipherSuites, SecureConnectionCipherSuitesTest) {
  TestSimpleOps();
}

class SecureConnectionCompressionTest : public SecureConnectionTest {
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_stream_compression) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_stream_compression_algo) = 1;
    SecureConnectionTest::SetUp();
  }
};

TEST_F_EX(SecureConnectionTest, Compression, SecureConnectionCompressionTest) {
  TestSimpleOps();
}

} // namespace yb
