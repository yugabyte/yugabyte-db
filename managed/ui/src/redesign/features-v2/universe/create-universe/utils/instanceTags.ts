import { InstanceTag } from '../steps/advanced-settings/dtos';

// Trimmed to match what userTagsSchema checks for uniqueness. Kept free of heavy imports so the
// payload mapper and the edit modal can share it.
export const instanceTagsToObject = (tags: InstanceTag[]): Record<string, string> =>
  tags.reduce(
    (acc, tag) => {
      if (tag?.name && tag?.value) acc[tag.name.trim()] = `${tag.value}`.trim();
      return acc;
    },
    {} as Record<string, string>
  );
