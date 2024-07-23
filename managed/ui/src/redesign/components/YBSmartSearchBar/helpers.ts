import { hasSubstringMatch } from '../../../components/queries/helpers/queriesHelper';
import { SearchToken } from './YBSmartSearchBar';

export const FieldType = {
  STRING: 'string',
  BOOLEAN: 'boolean'
} as const;
export type FieldType = typeof FieldType[keyof typeof FieldType];

interface StringField {
  value: string;
  type: typeof FieldType.STRING;
}
interface BooleanField {
  value: boolean;
  type: typeof FieldType.BOOLEAN;
}

export type SearchCandidate = Record<string, StringField | BooleanField>;

export const isMatchedBySearchToken = (
  candidate: SearchCandidate,
  searchToken: SearchToken,
  substringSearchFields: string[]
) => {
  if (!searchToken.modifier) {
    return substringSearchFields.some((substringSearchField) => {
      const fieldValue = candidate[substringSearchField];
      return (
        fieldValue &&
        fieldValue.type === FieldType.STRING &&
        hasSubstringMatch(fieldValue.value, searchToken.value)
      );
    });
  }
  const { type: fieldType, value: fieldValue } = candidate[searchToken.modifier] ?? {};
  switch (fieldType) {
    case FieldType.STRING:
      return fieldValue && hasSubstringMatch(fieldValue, searchToken.value);
    case FieldType.BOOLEAN:
      return (
        (fieldValue && searchToken.value === 'true') ||
        (!fieldValue && searchToken.value === 'false')
      );
    default:
      return false;
  }
};
