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

#include "yb/vector_index/distance_metric.h"

#include <type_traits>

#include "yb/util/logging.h"

#include "yb/vector_index/simsimd_include_wrapper_internal.h"

namespace yb::vector_index {

static_assert(std::is_same_v<DistanceMetric::Kernel, simsimd_metric_dense_punned_t>);

namespace {

simsimd_metric_kind_t ToSimsimdMetricKind(DistanceKind distance_kind) {
  switch (distance_kind) {
    case DistanceKind::kL2Squared:
      return simsimd_metric_l2sq_k;
    case DistanceKind::kInnerProduct:
      return simsimd_metric_dot_k;
    case DistanceKind::kCosine:
      return simsimd_metric_cos_k;
  }
  FATAL_INVALID_ENUM_VALUE(DistanceKind, distance_kind);
}

simsimd_datatype_t ToSimsimdDatatype(CoordinateKind coordinate_kind) {
  switch (coordinate_kind) {
    case CoordinateKind::kFloat32:
      return simsimd_datatype_f32_k;
    case CoordinateKind::kFloat64:
      return simsimd_datatype_f64_k;
    case CoordinateKind::kInt8:
      return simsimd_datatype_i8_k;
    default:
      LOG(FATAL) << "No SimSIMD kernel for coordinate kind " << AsString(coordinate_kind);
  }
}

const char* IsaName(simsimd_capability_t capability) {
  switch (capability) {
    case simsimd_cap_serial_k: return "serial";
    case simsimd_cap_haswell_k: return "haswell";
    case simsimd_cap_skylake_k: return "skylake";
    case simsimd_cap_ice_k: return "ice";
    case simsimd_cap_genoa_k: return "genoa";
    case simsimd_cap_sapphire_k: return "sapphire";
    case simsimd_cap_neon_k: return "neon";
    case simsimd_cap_neon_f16_k: return "neon_f16";
    case simsimd_cap_neon_bf16_k: return "neon_bf16";
    case simsimd_cap_neon_i8_k: return "neon_i8";
    case simsimd_cap_sve_k: return "sve";
    case simsimd_cap_sve_f16_k: return "sve_f16";
    case simsimd_cap_sve_bf16_k: return "sve_bf16";
    case simsimd_cap_sve_i8_k: return "sve_i8";
    case simsimd_cap_sve2_k: return "sve2";
    default: return "unknown";
  }
}

} // namespace

DistanceMetric::DistanceMetric(
    size_t dimensions, DistanceKind distance_kind, CoordinateKind coordinate_kind)
    : dimensions_(dimensions), reverse_(distance_kind == DistanceKind::kInnerProduct) {
  // CPU feature detection runs once per process.
  static const simsimd_capability_t kCapabilities = simsimd_capabilities();
  simsimd_metric_dense_punned_t kernel = nullptr;
  simsimd_capability_t used_capability = simsimd_cap_serial_k;
  simsimd_find_metric_punned(
      ToSimsimdMetricKind(distance_kind), ToSimsimdDatatype(coordinate_kind), kCapabilities,
      simsimd_cap_any_k, &kernel, &used_capability);
  LOG_IF(FATAL, kernel == nullptr)
      << "No SimSIMD kernel for " << AsString(distance_kind) << " over "
      << AsString(coordinate_kind);
  kernel_ = kernel;
  isa_name_ = IsaName(used_capability);
}

}  // namespace yb::vector_index
