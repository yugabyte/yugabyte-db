import { hasSubstringMatch } from '../../../components/queries/helpers/queriesHelper';
import { assertUnreachableCase } from '../../../utils/errorHandlingUtils';
import { SearchToken } from './YBSmartSearchBar';

export const FieldType = {
  STRING: 'string',
  STRING_ARRAY: 'string_array',
  BOOLEAN: 'boolean',
  NUMBER: 'number'
} as const;
export type FieldType = typeof FieldType[keyof typeof FieldType];

interface StringField {
  value: string;
  type: typeof FieldType.STRING;
}
interface StringArrayField {
  value: string[];
  type: typeof FieldType.STRING_ARRAY;
}
interface BooleanField {
  value: boolean;
  type: typeof FieldType.BOOLEAN;
}
interface NumberField {
  value: number;
  type: typeof FieldType.NUMBER;
}

export type SearchCandidate = Record<
  string,
  StringField | StringArrayField | BooleanField | NumberField
>;

const hasStringArraySubstringMatch = (fieldValue: string[], searchTokenValue: string) =>
  fieldValue.some((element) => hasSubstringMatch(element, searchTokenValue));

/**
 * Regex for numeric comparison
 *
 * Group 1 - Comparison Operator
 * Group 2 - Comparison Threshold
 */
const NUMERIC_COMPARISON_REGEX = /^(>|>=|<|<=|=|!=)(\d+)$/;
type ComparisonOperator = '>' | '>=' | '<' | '<=' | '=' | '!=';

export const isMatchedBySearchToken = (
  candidate: SearchCandidate,
  searchToken: SearchToken,
  substringSearchFields: string[]
) => {
  if (!searchToken.modifier) {
    return substringSearchFields.some((substringSearchField) => {
      const { type: fieldType, value: fieldValue } = candidate[substringSearchField] ?? {};
      if (fieldType === FieldType.STRING) {
        return fieldValue && hasSubstringMatch(fieldValue, searchToken.value);
      }
      if (fieldType === FieldType.STRING_ARRAY) {
        return (
          Array.isArray(fieldValue) &&
          fieldValue.length > 0 &&
          hasStringArraySubstringMatch(fieldValue, searchToken.value)
        );
      }
      return false;
    });
  }
  const { type: fieldType, value: fieldValue } = candidate[searchToken.modifier] ?? {};

  // If the field value is undefined or null, we consider it not a match for all candidates.
  if (fieldValue === undefined || fieldValue === null) {
    return false;
  }

  switch (fieldType) {
    case FieldType.STRING:
      return fieldValue && hasSubstringMatch(fieldValue, searchToken.value);
    case FieldType.STRING_ARRAY:
      return (
        Array.isArray(fieldValue) &&
        fieldValue.length > 0 &&
        hasStringArraySubstringMatch(fieldValue, searchToken.value)
      );
    case FieldType.BOOLEAN:
      return (
        (fieldValue && searchToken.value === 'true') ||
        (!fieldValue && searchToken.value === 'false')
      );
    case FieldType.NUMBER: {
      const match = NUMERIC_COMPARISON_REGEX.exec(searchToken.value);
      if (match) {
        const comparisonOperator = match[1] as ComparisonOperator;
        const comparisonThreshold = parseFloat(match[2]);

        switch (comparisonOperator) {
          case '>':
            return fieldValue > comparisonThreshold;
          case '>=':
            return fieldValue >= comparisonThreshold;
          case '<':
            return fieldValue < comparisonThreshold;
          case '<=':
            return fieldValue <= comparisonThreshold;
          case '=':
            return fieldValue === comparisonThreshold;
          case '!=':
            return fieldValue !== comparisonThreshold;
          default:
            // This should be an unreachable case because we handled all possible
            // strings for the comparison operator.
            assertUnreachableCase(comparisonOperator);
            return false;
        }
      }

      // If the provided search token value does not match the comparison regex,
      // we consider it not a match for all candidates.
      return false;
    }
    default:
      return assertUnreachableCase(fieldType);
  }
};
