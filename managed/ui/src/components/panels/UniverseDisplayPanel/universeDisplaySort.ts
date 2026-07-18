// Copyright (c) YugabyteDB, Inc.

import type { Universe } from '../../../redesign/features/universe/universe-form/utils/dto';

/**
 * Sort key for dashboard universe cards: ascending creation time, then name.
 * Valid parseable creation dates sort before invalid ones; invalid dates tie-break on name only.
 */
export function compareUniversesForDashboardDisplay(
  universeA: Pick<Universe, 'creationDate' | 'name'>,
  universeB: Pick<Universe, 'creationDate' | 'name'>
): number {
  const timestampA = Date.parse(universeA.creationDate);
  const timestampB = Date.parse(universeB.creationDate);
  const isCreationDateAValid = !Number.isNaN(timestampA);
  const isCreationDateBValid = !Number.isNaN(timestampB);
  if (isCreationDateAValid && isCreationDateBValid) {
    const timeDiff = timestampA - timestampB;
    if (timeDiff !== 0) {
      return timeDiff;
    }
  } else if (isCreationDateAValid !== isCreationDateBValid) {
    return isCreationDateAValid ? -1 : 1;
  }
  return (universeA.name ?? '').localeCompare(universeB.name ?? '');
}
