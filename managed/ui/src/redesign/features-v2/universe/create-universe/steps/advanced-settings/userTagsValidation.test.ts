import { describe, expect, it } from 'vitest';
import * as Yup from 'yup';
import { TFunction } from 'i18next';
import { userTagsSchema } from './userTagsValidation';

const t = ((key: string) => key) as unknown as TFunction;

const errorPaths = async (tags: { name: string; value: string }[]) => {
  try {
    await Yup.object({ instanceTags: userTagsSchema(t) }).validate(
      { instanceTags: tags },
      { abortEarly: false }
    );
    return [];
  } catch (e) {
    return (e as Yup.ValidationError).inner.map((err) => `${err.path}:${err.message}`);
  }
};

describe('userTagsSchema', () => {
  it('accepts no tags and valid tags', async () => {
    expect(await errorPaths([])).toEqual([]);
    expect(
      await errorPaths([
        { name: 'a', value: '1' },
        { name: 'A', value: '1' }
      ])
    ).toEqual([]);
  });

  it('rejects a blank row', async () => {
    expect(await errorPaths([{ name: '', value: '' }])).toEqual([
      'instanceTags.0.name:userTags.tagNameRequired',
      'instanceTags.0.value:userTags.tagValueRequired'
    ]);
  });

  it('rejects a row with only a name or only a value', async () => {
    expect(await errorPaths([{ name: 'a', value: ' ' }])).toEqual([
      'instanceTags.0.value:userTags.tagValueRequired'
    ]);
    expect(await errorPaths([{ name: '', value: 'x' }])).toEqual([
      'instanceTags.0.name:userTags.tagNameRequired'
    ]);
  });

  it('flags the later duplicate name, comparing trimmed and case-sensitive', async () => {
    expect(
      await errorPaths([
        { name: 'a', value: '1' },
        { name: ' a ', value: '2' }
      ])
    ).toEqual(['instanceTags.1.name:userTags.tagNameDuplicate']);
  });
});
