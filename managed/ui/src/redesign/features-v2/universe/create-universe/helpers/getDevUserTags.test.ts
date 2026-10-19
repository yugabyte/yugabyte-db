import { describe, expect, it } from 'vitest';
import { RuntimeConfigKey } from '@app/redesign/helpers/constants';
import { RunTimeConfig } from '@app/redesign/features/universe/universe-form/utils/dto';
import { getDevUserTags } from './utils';

const config = (isEnforced?: string, devTags?: string): RunTimeConfig =>
  ({
    configEntries: [
      ...(isEnforced === undefined
        ? []
        : [{ key: RuntimeConfigKey.IS_TAGS_ENFORCED, value: isEnforced, inherited: true }]),
      ...(devTags === undefined
        ? []
        : [{ key: RuntimeConfigKey.DEFAULT_DEV_TAGS, value: devTags, inherited: true }])
    ]
  }) as unknown as RunTimeConfig;

const DEV_TAGS = '[{"name":"yb_owner","value":"dev"},{"name":"yb_task","value":"dev"}]';

describe('getDevUserTags', () => {
  it('returns dev tags when tags are enforced', () => {
    expect(getDevUserTags(config('true', DEV_TAGS))).toEqual([
      { name: 'yb_owner', value: 'dev' },
      { name: 'yb_task', value: 'dev' }
    ]);
  });

  it('returns nothing when tags are not enforced', () => {
    expect(getDevUserTags(config('false', DEV_TAGS))).toEqual([]);
    expect(getDevUserTags(config(undefined, DEV_TAGS))).toEqual([]);
    expect(getDevUserTags(undefined)).toEqual([]);
  });

  it('returns nothing when dev tags are empty or missing', () => {
    expect(getDevUserTags(config('true', '[]'))).toEqual([]);
    expect(getDevUserTags(config('true'))).toEqual([]);
  });

  it('accepts "key:value" strings, as set through the runtime config API', () => {
    expect(getDevUserTags(config('true', '["test:test","url:http://x"]'))).toEqual([
      { name: 'test', value: 'test' },
      { name: 'url', value: 'http://x' }
    ]);
  });

  it('accepts the escaped value the API returns for an overridden key', () => {
    expect(getDevUserTags(config('true', '[\\"test:test\\"]'))).toEqual([
      { name: 'test', value: 'test' }
    ]);
  });

  it('ignores malformed values', () => {
    expect(getDevUserTags(config('true', 'not json'))).toEqual([]);
    expect(getDevUserTags(config('true', '{"name":"a","value":"b"}'))).toEqual([]);
    expect(
      getDevUserTags(
        config(
          'true',
          '[{"name":"a","value":"b"},{"name":"","value":"x"},{"name":"c"},null,"nocolon",":v","k:",5]'
        )
      )
    ).toEqual([{ name: 'a', value: 'b' }]);
  });
});
