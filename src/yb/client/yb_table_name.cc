// Copyright (c) YugaByte, Inc.

#include "yb/client/yb_table_name.h"
#include "yb/master/master_defaults.h"
#include "yb/master/master.pb.h"

namespace yb {
namespace client {

using std::string;

void YBTableName::SetIntoTableIdentifierPB(master::TableIdentifierPB* id) const {
    id->set_table_name(table_name());
    id->mutable_namespace_()->set_name(resolved_namespace_name());
}

const string& YBTableName::default_namespace() {
    static const string defalt_namespace_name(master::kDefaultNamespaceName);
    return defalt_namespace_name;
}

} // namespace client
} // namespace yb
