export const NodeAggregation = {
  AVERAGE: 'AVG',
  MAX: 'MAX',
  MIN: 'MIN',
  SUM: 'SUM'
} as const;
export type NodeAggregation = typeof NodeAggregation[keyof typeof NodeAggregation];

export const TimeAggregation = {
  DEFAULT: '',
  MIN: 'min_over_time',
  MAX: 'max_over_time',
  AVG: 'avg_over_time'
} as const;
export type TimeAggregation = typeof TimeAggregation[keyof typeof TimeAggregation];

/**
 * Options for whether and how we split the time series data into their
 * into separate lines.
 *
 * Java Enum
 * Source: managed/src/main/java/com/yugabyte/yw/metrics/SplitMode.java
 */
export const SplitMode = {
  NONE: 'NONE',
  TOP: 'TOP',
  BOTTOM: 'BOTTOM'
} as const;
export type SplitMode = typeof SplitMode[keyof typeof SplitMode];

/**
 * Options for splitting a metrics query.
 *
 * Java Enum
 * Source: managed/src/main/java/com/yugabyte/yw/metrics/SplitType.java
 */
export const SplitType = {
  NONE: 'NONE',
  NODE: 'NODE',
  TABLE: 'TABLE',
  NAMESPACE: 'NAMESPACE'
} as const;
export type SplitType = typeof SplitType[keyof typeof SplitType];
