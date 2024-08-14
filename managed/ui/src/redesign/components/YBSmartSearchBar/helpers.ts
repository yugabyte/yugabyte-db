import { hasSubstringMatch } from '../../../components/queries/helpers/queriesHelper';
import { assertUnreachableCase } from '../../../utils/errorHandlingUtils';
import { SearchToken } from './YBSmartSearchBar';

export const FieldType = {
  STRING: 'string',
  BOOLEAN: 'boolean',
  NUMBER: 'number'
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
interface NumberField {
  value: number;
  type: typeof FieldType.NUMBER;
}

export type SearchCandidate = Record<string, StringField | BooleanField | NumberField>;

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
    case FieldType.NUMBER:
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
            return fieldValue != comparisonThreshold;
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
    default:
      return assertUnreachableCase(fieldType);
  }
};
