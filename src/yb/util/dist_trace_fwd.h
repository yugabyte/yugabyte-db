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

namespace opentelemetry {
inline namespace v1 {

namespace nostd {
class string_view;
template <class T> class shared_ptr;
} // namespace nostd

namespace trace {
class Span;
class Tracer;
class SpanContext;
} // namespace trace

} // namespace v1
} // namespace opentelemetry
