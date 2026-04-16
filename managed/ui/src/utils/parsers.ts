const TYPESAFE_CONFIG_DURATION_STRING_TO_SECONDS: { [key: string]: number } = {
  ns: 1e-9,
  nano: 1e-9,
  nanos: 1e-9,
  nanosecond: 1e-9,
  nanoseconds: 1e-9,
  us: 1e-6,
  micro: 1e-6,
  micros: 1e-6,
  microsecond: 1e-6,
  microseconds: 1e-6,
  ms: 1e-3,
  milli: 1e-3,
  millis: 1e-3,
  millisecond: 1e-3,
  milliseconds: 1e-3,
  s: 1,
  second: 1,
  seconds: 1,
  m: 60,
  minute: 60,
  minutes: 60,
  h: 3600,
  hour: 3600,
  hours: 3600,
  d: 86_400,
  day: 86_400,
  days: 86_400
};

/**
 * Parses a valid Typesafe config duration string.
 * Github docs: https://github.com/lightbend/config/blob/main/HOCON.md#duration-format
 *
 * If `options.noThrow` is true, it returns `NaN` instead of throwing.
 * @returns duration in seconds.
 */
export const parseDurationToSeconds = (
  durationString: string,
  options?: { noThrow: boolean }
): number => {
  const noThrow = options?.noThrow ?? false;
  const trimmedDuration = durationString.trim();

  // Group 1 - Duration value. It matches optional decimals as well.
  // Group 2 - Duration unit. It matches any string.
  //           We will handle or throw an error for unsupported units.
  const match = trimmedDuration.match(/^(\d+(?:\.\d+)?)\s*([a-zA-Z]+)$/i);

  if (!match) {
    if (/^\d+$/.test(trimmedDuration)) {
      return parseInt(trimmedDuration, 10) / 1000; // Treat as milliseconds
    }
    if (noThrow) {
      return NaN;
    }
    throw new Error(`Invalid duration format: ${durationString}`);
  }

  const durationValue = parseFloat(match[1]);
  const durationUnit = match[2].toLowerCase();

  const multiplier = TYPESAFE_CONFIG_DURATION_STRING_TO_SECONDS[durationUnit];
  if (multiplier === undefined) {
    if (noThrow) {
      return NaN;
    }
    throw new Error(`Unsupported time unit: ${durationUnit}`);
  }

  return durationValue * TYPESAFE_CONFIG_DURATION_STRING_TO_SECONDS[durationUnit];
};
