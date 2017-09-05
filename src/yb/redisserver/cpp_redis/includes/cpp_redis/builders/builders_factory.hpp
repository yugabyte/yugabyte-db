#pragma once

#include <memory>

#include "cpp_redis/builders/builder_iface.hpp"

namespace cpp_redis {

namespace builders {

//! create a builder corresponding to the given id
//!  + for simple strings
//!  - for errors
//!  : for integers
//!  $ for bulk strings
//!  * for arrays
std::unique_ptr<builder_iface> create_builder(char id);

} //! builders

} //! cpp_redis
