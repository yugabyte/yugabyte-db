/** Shared QoS multi-tenancy field constraints (max DB CPU %, max DB count). Universe form + edit modal. */

export const MULTI_TENANCY_QOS_MAX_DB_CPU_PERCENT_MIN = 0;
export const MULTI_TENANCY_QOS_MAX_DB_CPU_PERCENT_MAX = 100;
export const MULTI_TENANCY_QOS_MAX_DB_CPU_PERCENT_STEP = 1;
/** Used when the CPU % field is empty or not a finite number on blur/submit. */
export const MULTI_TENANCY_QOS_MAX_DB_CPU_PERCENT_FALLBACK = 100;

export function clampMultiTenancyQosMaxDbCpuPercent(value: unknown): number {
  const n = typeof value === 'number' ? value : parseFloat(String(value));
  if (!Number.isFinite(n)) {
    return MULTI_TENANCY_QOS_MAX_DB_CPU_PERCENT_FALLBACK;
  }
  return Math.min(
    MULTI_TENANCY_QOS_MAX_DB_CPU_PERCENT_MAX,
    Math.max(MULTI_TENANCY_QOS_MAX_DB_CPU_PERCENT_MIN, n)
  );
}

export const MULTI_TENANCY_QOS_MAX_DB_COUNT_MIN = 1;
export const MULTI_TENANCY_QOS_MAX_DB_COUNT_STEP = 1;
/** Used when the field is empty or not a finite number on blur/submit. */
export const MULTI_TENANCY_QOS_MAX_DB_COUNT_FALLBACK = 50;

export function clampMultiTenancyQosMaxDbCount(value: unknown): number {
  const n = typeof value === 'number' ? value : parseInt(String(value), 10);
  if (!Number.isFinite(n)) {
    return MULTI_TENANCY_QOS_MAX_DB_COUNT_FALLBACK;
  }
  return Math.max(MULTI_TENANCY_QOS_MAX_DB_COUNT_MIN, Math.floor(n));
}
