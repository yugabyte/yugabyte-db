import clsx from 'clsx';

import { XClusterTableEligibility } from '../../constants';
import { assertUnreachableCase } from '../../../../utils/errorHandlingUtils';

import { EligibilityDetails } from '../..';

import styles from './TableEligibilityPill.module.scss';

interface TableEligibilityPillProps {
  eligibilityDetails: EligibilityDetails;
}

export const TableEligibilityPill = ({ eligibilityDetails }: TableEligibilityPillProps) => {
  switch (eligibilityDetails.status) {
    case XClusterTableEligibility.ELIGIBLE_UNUSED:
      return <div className={clsx(styles.pill)}>Eligible</div>;
    case XClusterTableEligibility.ELIGIBLE_IN_CURRENT_CONFIG:
      return (
        <div className={clsx(styles.pill, styles.good)}>
          {`In use: ${eligibilityDetails.xClusterConfigName} (current config)`}
        </div>
      );
    case XClusterTableEligibility.INELIGIBLE_IN_USE:
      return (
        <div className={clsx(styles.pill, styles.bad)}>
          {`In use: ${eligibilityDetails.xClusterConfigName}`}
        </div>
      );
    case XClusterTableEligibility.INELIGIBLE_NO_MATCH:
      return <div className={clsx(styles.pill, styles.bad)}>No Match</div>;
    default:
      return assertUnreachableCase(eligibilityDetails);
  }
};
