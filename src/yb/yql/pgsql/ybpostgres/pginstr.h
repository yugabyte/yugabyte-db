//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------
#ifndef YB_YQL_PGSQL_YBPOSTGRES_PGINSTR_H_
#define YB_YQL_PGSQL_YBPOSTGRES_PGINSTR_H_

#include "yb/yql/pgsql/ybpostgres/pgdefs.h"
#include "yb/yql/pgsql/ybpostgres/pg_stringinfo.h"
#include "yb/yql/pgsql/ybpostgres/pg_pqcomm.h"

namespace yb {
namespace pgapi {

using std::to_string;

enum class PGInstrOpcode : uint32_t {
  kNone = 0,                // default
  kBind = 1,                // postgres msgtype = 'B'
  kCancelEOF = 2,           // EOF
  kCancelX = 3,             // 'X'
  kCopyCompleted = 4,       // 'c' - lower case
  kCopyData = 5,            // 'd' - lower case
  kCopyFailed = 6,          // 'f' - lower case
  kDescribePortal = 7,      // 'D'
  kDescribeStatement = 8,   // 'D'
  kDropPortal = 9,          // 'C'
  kDropStatement = 10,      // 'C'
  kExec = 11,               // 'E'
  kFlush = 12,              // 'H'
  kFunctionCall = 13,       // 'F'
  kParse = 14,              // 'P'
  kQuery = 15,              // 'Q'
  kSync = 16,               // 'S'

  // The following are internal instructions as they are not associated with specific requests.
  kStartup = 101,
};

// This class represent an instruction from client. Server will parse the instruction and execute
// it accordingly.
class PGInstr {
 public:
  typedef std::shared_ptr<PGInstr> SharedPtr;

  explicit PGInstr(const StringInfo& command, PGInstrOpcode op);
  virtual ~PGInstr() {
  }

  static CHECKED_STATUS ReadCommand_B(const StringInfo& command, PGInstr::SharedPtr *instr);
  static CHECKED_STATUS ReadCommand_EOF(const StringInfo& command, PGInstr::SharedPtr *instr);
  static CHECKED_STATUS ReadCommand_X(const StringInfo& command, PGInstr::SharedPtr *instr);
  static CHECKED_STATUS ReadCommand_d(const StringInfo& command, PGInstr::SharedPtr *instr);
  static CHECKED_STATUS ReadCommand_c(const StringInfo& command, PGInstr::SharedPtr *instr);
  static CHECKED_STATUS ReadCommand_f(const StringInfo& command, PGInstr::SharedPtr *instr);
  static CHECKED_STATUS ReadCommand_D(const StringInfo& command, PGInstr::SharedPtr *instr);
  static CHECKED_STATUS ReadCommand_C(const StringInfo& command, PGInstr::SharedPtr *instr);
  static CHECKED_STATUS ReadCommand_E(const StringInfo& command, PGInstr::SharedPtr *instr);
  static CHECKED_STATUS ReadCommand_H(const StringInfo& command, PGInstr::SharedPtr *instr);
  static CHECKED_STATUS ReadCommand_F(const StringInfo& command, PGInstr::SharedPtr *instr);
  static CHECKED_STATUS ReadCommand_S(const StringInfo& command, PGInstr::SharedPtr *instr);
  static CHECKED_STATUS ReadCommand_P(const StringInfo& command, PGInstr::SharedPtr *instr);
  static CHECKED_STATUS ReadCommand_Q(const StringInfo& command, PGInstr::SharedPtr *instr);

  PGInstrOpcode op() const {
    return op_;
  }

  virtual string ToString() const {
    return command_->data;
  }

  const StringInfo& command() {
    return command_;
  }
 protected:
  StringInfo command_;
  PGInstrOpcode op_ = PGInstrOpcode::kNone;
};

class PGInstrStartup : public PGInstr {
 public:
  typedef std::shared_ptr<PGInstrStartup> SharedPtr;

  explicit PGInstrStartup(const StringInfo& postgres_packet, ProtocolVersion protocol);
  virtual ~PGInstrStartup() { }

  virtual string ToString() const {
    return "Startup Instr";
  }

  const StringInfo& packet() {
    return command_;
  }

  ProtocolVersion protocol() {
    return protocol_;
  }

 private:
  ProtocolVersion protocol_;
};

//--------------------------------------------------------------------------------------------------

class PGInstrBind : public PGInstr {
 public:
  typedef std::shared_ptr<PGInstrBind> SharedPtr;

  explicit PGInstrBind(const StringInfo& command);
  virtual ~PGInstrBind() { }

  virtual string ToString() const {
    return "Bind Instr";
  }

 private:
};

//--------------------------------------------------------------------------------------------------

class PGInstrCancel : public PGInstr {
 public:
  typedef std::shared_ptr<PGInstrCancel> SharedPtr;

  explicit PGInstrCancel(const StringInfo& command, PGInstrOpcode op);
  virtual ~PGInstrCancel() { }

  virtual string ToString() const {
    return "Cancel Instr";
  }

 private:
};

//--------------------------------------------------------------------------------------------------

class PGInstrCopy : public PGInstr {
 public:
  typedef std::shared_ptr<PGInstrCopy> SharedPtr;

  explicit PGInstrCopy(const StringInfo& command, PGInstrOpcode op);
  virtual ~PGInstrCopy() { }

  virtual string ToString() const {
    return "Copy Instr";
  }

 private:
};

//--------------------------------------------------------------------------------------------------

class PGInstrDescribe : public PGInstr {
 public:
  typedef std::shared_ptr<PGInstrDescribe> SharedPtr;

  explicit PGInstrDescribe(const StringInfo& command, PGInstrOpcode op, const string& target_name);
  virtual ~PGInstrDescribe() { }

  virtual string ToString() const {
    string s = "Describe Instr: ";
    s += target_name_;
    return s;
  }

 private:
  string target_name_;
};

//--------------------------------------------------------------------------------------------------

class PGInstrDrop : public PGInstr {
 public:
  typedef std::shared_ptr<PGInstrDrop> SharedPtr;

  explicit PGInstrDrop(const StringInfo& command, PGInstrOpcode op, const string& target_name);
  virtual ~PGInstrDrop() { }

  virtual string ToString() const {
    string s = "Drop InstrL: ";
    s += target_name_;
    return s;
  }
 private:
  string target_name_;
};

//--------------------------------------------------------------------------------------------------

class PGInstrExec : public PGInstr {
 public:
  typedef std::shared_ptr<PGInstrExec> SharedPtr;

  explicit PGInstrExec(const StringInfo& command, const string& portal_name, int max_rows);
  virtual ~PGInstrExec() { }

  int max_rows() const {
    return max_rows_;
  }

  const string& portal_name() {
    return portal_name_;
  }

  virtual string ToString() const {
    string s = "Exec Instr: ";
    s += portal_name_ + ", " + to_string(max_rows_);
    return s;
  }

 private:
  string portal_name_;
  int max_rows_;
};

//--------------------------------------------------------------------------------------------------

class PGInstrFlush : public PGInstr {
 public:
  typedef std::shared_ptr<PGInstrFlush> SharedPtr;

  explicit PGInstrFlush(const StringInfo& command);
  virtual ~PGInstrFlush() { }

  virtual string ToString() const {
    return "Flush Instr";
  }
 private:
};

//--------------------------------------------------------------------------------------------------

class PGInstrFunctionCall : public PGInstr {
 public:
  typedef std::shared_ptr<PGInstrFunctionCall> SharedPtr;

  explicit PGInstrFunctionCall(const StringInfo& command);
  virtual ~PGInstrFunctionCall() { }

  virtual string ToString() const {
    return "Function Call Instr";
  }
 private:
};

//--------------------------------------------------------------------------------------------------

class PGInstrParse : public PGInstr {
 public:
  typedef std::shared_ptr<PGInstrParse> SharedPtr;

  explicit PGInstrParse(const StringInfo& command, const string& stmt_name, const string& stmt);
  virtual ~PGInstrParse() { }

  void AddParamType(PGOid param_type);

  virtual string ToString() const {
    string s = "Parse Instr: ";
    s += stmt_name_ + "< " + stmt_ + " > < ";
    for (PGOid ptype : param_types_) {
      s += to_string(ptype) + " ";
    }
    s += ">";
    return s;
  }
 private:
  string stmt_name_;
  string stmt_;
  vector<PGOid> param_types_;
};

//--------------------------------------------------------------------------------------------------

class PGInstrQuery : public PGInstr {
 public:
  typedef std::shared_ptr<PGInstrQuery> SharedPtr;

  explicit PGInstrQuery(const StringInfo& command, const string& stmt);
  virtual ~PGInstrQuery() { }

  virtual string ToString() const {
    string s = "Query Instr: < ";
    s += stmt_ + " >";
    return s;
  }

  const string& stmt() {
    return stmt_;
  }
 private:
  string stmt_;
};

//--------------------------------------------------------------------------------------------------

class PGInstrSync : public PGInstr {
 public:
  typedef std::shared_ptr<PGInstrSync> SharedPtr;

  explicit PGInstrSync(const StringInfo& command);
  virtual ~PGInstrSync() { }

  virtual string ToString() const {
    return "Sync Instr";
  }

 private:
};

}  // namespace pgapi
}  // namespace yb

#endif  // YB_YQL_PGSQL_YBPOSTGRES_PGINSTR_H_
