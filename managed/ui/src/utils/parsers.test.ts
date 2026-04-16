import { parseDurationToSeconds } from './parsers';

describe('Utility Function - parseDurationToSeconds', () => {
  describe('basic functionality', () => {
    it('parses simple duration strings correctly', () => {
      expect(parseDurationToSeconds('5 minutes')).toBe(300);
      expect(parseDurationToSeconds('3h')).toBe(10800);
      expect(parseDurationToSeconds('10s')).toBe(10);
      expect(parseDurationToSeconds('500ms')).toBe(0.5);
    });
    it('interprets plain numbers as milliseconds', () => {
      expect(parseDurationToSeconds('1000')).toBe(1);
    });
  });

  describe('unit variations', () => {
    it('handles nanosecond variations', () => {
      expect(parseDurationToSeconds('1 nano')).toBe(1e-9);
      expect(parseDurationToSeconds('1 nanos')).toBe(1e-9);
      expect(parseDurationToSeconds('1 nanosecond')).toBe(1e-9);
      expect(parseDurationToSeconds('1 nanoseconds')).toBe(1e-9);
    });
    it('handles microsecond variations', () => {
      expect(parseDurationToSeconds('1 micro')).toBe(1e-6);
      expect(parseDurationToSeconds('1 micros')).toBe(1e-6);
      expect(parseDurationToSeconds('1 microsecond')).toBe(1e-6);
      expect(parseDurationToSeconds('1 microseconds')).toBe(1e-6);
    });
    it('handles millisecond variations', () => {
      expect(parseDurationToSeconds('1 milli')).toBe(1e-3);
      expect(parseDurationToSeconds('1 millis')).toBe(1e-3);
      expect(parseDurationToSeconds('1 millisecond')).toBe(1e-3);
      expect(parseDurationToSeconds('1 milliseconds')).toBe(1e-3);
    });
    it('handles second variations', () => {
      expect(parseDurationToSeconds('1 second')).toBe(1);
      expect(parseDurationToSeconds('1 seconds')).toBe(1);
      expect(parseDurationToSeconds('1s')).toBe(1);
    });
    it('handles minute variations', () => {
      expect(parseDurationToSeconds('1 minute')).toBe(60);
      expect(parseDurationToSeconds('1 minutes')).toBe(60);
      expect(parseDurationToSeconds('1m')).toBe(60);
    });
    it('handles hour variations', () => {
      expect(parseDurationToSeconds('1 hour')).toBe(3600);
      expect(parseDurationToSeconds('1 hours')).toBe(3600);
      expect(parseDurationToSeconds('1h')).toBe(3600);
    });
    it('handles day variations', () => {
      expect(parseDurationToSeconds('1 day')).toBe(86400);
      expect(parseDurationToSeconds('1 days')).toBe(86400);
      expect(parseDurationToSeconds('1d')).toBe(86400);
    });
  });

  describe('edge cases', () => {
    it('is case insensitive', () => {
      expect(parseDurationToSeconds('1 MINUTE')).toBe(60);
      expect(parseDurationToSeconds('1 Minute')).toBe(60);
      expect(parseDurationToSeconds('1 minute')).toBe(60);
    });
    it('handles extra whitespace', () => {
      expect(parseDurationToSeconds('  5   minutes  ')).toBe(300);
    });
    it('handles decimal values', () => {
      expect(parseDurationToSeconds('1.5 hours')).toBe(5400);
      expect(parseDurationToSeconds('0.5 days')).toBe(43200);
    });
    it('handles zero values', () => {
      expect(parseDurationToSeconds('0s')).toBe(0);
      expect(parseDurationToSeconds('0')).toBe(0);
    });
    it('handles very large values', () => {
      expect(parseDurationToSeconds('1000000 days')).toBe(86400000000);
    });
    it('handles very small values', () => {
      expect(parseDurationToSeconds('0.000001 ns')).toBe(1e-15);
    });
  });

  describe('error cases', () => {
    it('throws error for invalid format', () => {
      expect(() => parseDurationToSeconds('invalid')).toThrow('Invalid duration format');
      expect(() => parseDurationToSeconds('5m 30s')).toThrow('Invalid duration format');
      expect(() => parseDurationToSeconds('2 coffee breaks')).toThrow('Invalid duration format');
      expect(() => parseDurationToSeconds('1 long hour')).toThrow('Invalid duration format');
      expect(() => parseDurationToSeconds('1 hour long')).toThrow('Invalid duration format');
    });
    it('throws error for unsupported time unit', () => {
      expect(() => parseDurationToSeconds('5 years')).toThrow('Unsupported time unit');
      expect(() => parseDurationToSeconds('10 weeks')).toThrow('Unsupported time unit');
      expect(() => parseDurationToSeconds('1 eternity')).toThrow('Unsupported time unit');
    });
    it('throws error for empty string', () => {
      expect(() => parseDurationToSeconds('')).toThrow('Invalid duration format');
    });
    it('returns NaN for invalid format with noThrow option', () => {
      expect(parseDurationToSeconds('invalid', { noThrow: true })).toBeNaN();
      expect(parseDurationToSeconds('5m 30s', { noThrow: true })).toBeNaN();
    });
    it('returns NaN for unsupported time unit with noThrow option', () => {
      expect(parseDurationToSeconds('5 years', { noThrow: true })).toBeNaN();
      expect(parseDurationToSeconds('10 weeks', { noThrow: true })).toBeNaN();
    });
    it('returns NaN for empty string with noThrow option', () => {
      expect(parseDurationToSeconds('', { noThrow: true })).toBeNaN();
    });
  });
});
