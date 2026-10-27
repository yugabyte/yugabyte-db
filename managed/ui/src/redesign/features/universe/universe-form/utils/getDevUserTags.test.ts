import { describe, expect, it } from 'vitest';
import { getDevUserTags } from './helpers';

const DEV_TAGS = '[{"name":"yb_owner","value":"dev"},{"name":"yb_task","value":"dev"}]';

describe('getDevUserTags', () => {
  it('returns dev tags when tags are enforced', () => {
    expect(getDevUserTags('true', DEV_TAGS)).toEqual([
      { name: 'yb_owner', value: 'dev' },
      { name: 'yb_task', value: 'dev' }
    ]);
  });

  it('returns nothing when tags are not enforced', () => {
    expect(getDevUserTags('false', DEV_TAGS)).toEqual([]);
    expect(getDevUserTags(undefined, DEV_TAGS)).toEqual([]);
  });

  it('returns nothing when dev tags are empty or missing', () => {
    expect(getDevUserTags('true', '[]')).toEqual([]);
    expect(getDevUserTags('true', undefined)).toEqual([]);
  });

  it('accepts "key:value" strings, as set through the runtime config API', () => {
    expect(getDevUserTags('true', '["test:test","url:http://x"]')).toEqual([
      { name: 'test', value: 'test' },
      { name: 'url', value: 'http://x' }
    ]);
  });

  it('accepts the escaped value the API returns for an overridden key', () => {
    expect(getDevUserTags('true', '[\\"test:test\\"]')).toEqual([{ name: 'test', value: 'test' }]);
  });

  it('ignores malformed values', () => {
    expect(getDevUserTags('true', 'not json')).toEqual([]);
    expect(getDevUserTags('true', '{"name":"a","value":"b"}')).toEqual([]);
    expect(
      getDevUserTags(
        'true',
        '[{"name":"a","value":"b"},{"name":"","value":"x"},{"name":"c"},null,"nocolon",":v","k:",5]'
      )
    ).toEqual([{ name: 'a', value: 'b' }]);
  });
});
