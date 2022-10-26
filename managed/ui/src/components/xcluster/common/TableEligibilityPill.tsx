import React from 'react';
import clsx from 'clsx';

import { EligibilityDetails } from '../createConfig/SelectTablesStep';

import styles from './TableEligibilityPill.module.scss';

export enum XClusterTableEligibility {
  ELIGIBLE = 'Eligible',
  // Ineligible states
  INELIGIBLE_IN_USE = 'In Use',
  INELIGIBLE_NO_MATCH = 'No Match'
}

interface TableEligibilityPillProps {
  eligibilityDetails: EligibilityDetails;
}

export const TableEligibilityPill = ({ eligibilityDetails }: TableEligibilityPillProps) => {
  const pillText =
    eligibilityDetails.state === XClusterTableEligibility.INELIGIBLE_IN_USE
      ? `${eligibilityDetails.state}: ${eligibilityDetails.xClusterConfigName}`
      : eligibilityDetails.state;
  return (
    <div
      className={clsx(
        styles.pill,
        eligibilityDetails.state !== XClusterTableEligibility.ELIGIBLE && styles.ineligible
      )}
    >
      {pillText}
    </div>
  );
};
