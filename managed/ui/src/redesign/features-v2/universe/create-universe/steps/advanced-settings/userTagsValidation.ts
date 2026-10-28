import * as Yup from 'yup';
import { TFunction } from 'i18next';
import { InstanceTag } from './dtos';

// Errors are attached to the offending input (`<path>.<i>.name|value`) so UserTagsField shows them inline.
export const userTagsSchema = (t: TFunction) =>
  Yup.array<InstanceTag>().test('user-tags', '', function (tags) {
    const seen = new Set<string>();
    const errors: Yup.ValidationError[] = [];
    const fail = (i: number, field: 'name' | 'value', key: string) =>
      errors.push(this.createError({ path: `${this.path}.${i}.${field}`, message: t(key) }));

    (tags ?? []).forEach((tag, i) => {
      const name = tag?.name?.trim() ?? '';
      const value = tag?.value?.trim() ?? '';
      if (!name) fail(i, 'name', 'userTags.tagNameRequired');
      else if (seen.has(name)) fail(i, 'name', 'userTags.tagNameDuplicate');
      seen.add(name);
      if (!value) fail(i, 'value', 'userTags.tagValueRequired');
    });
    // Yup's typings only allow string[], but nested ValidationErrors are supported at runtime.
    return errors.length
      ? new Yup.ValidationError(errors as unknown as string[], tags, this.path)
      : true;
  });
