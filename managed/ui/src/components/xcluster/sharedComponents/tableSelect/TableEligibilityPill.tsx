import clsx from 'clsx';
import { useTranslation } from 'react-i18next';

import { XClusterTableEligibility } from '../../constants';
import { assertUnreachableCase } from '../../../../utils/errorHandlingUtils';
import { EligibilityDetails } from '../..';

import { usePillStyles } from '../../../../redesign/styles/styles';

interface TableEligibilityPillProps {
  eligibilityDetails: EligibilityDetails;
}

const TRANSLATION_KEY_PREFIX = 'clusterDetail.xCluster.selectTable.tablePill';

export const TableEligibilityPill = ({ eligibilityDetails }: TableEligibilityPillProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const pillClasses = usePillStyles();
  switch (eligibilityDetails.status) {
    case XClusterTableEligibility.ELIGIBLE_UNUSED:
      return <div className={clsx(pillClasses.pill)}>{t('eligible')}</div>;
    case XClusterTableEligibility.ELIGIBLE_IN_CURRENT_CONFIG:
      return (
        <div className={clsx(pillClasses.pill, pillClasses.ready)}>{t('inCurrentConfig')}</div>
      );
    case XClusterTableEligibility.INELIGIBLE_IN_USE:
      return <div className={clsx(pillClasses.pill, pillClasses.danger)}>{t('inUse')}</div>;
    default:
      return assertUnreachableCase(eligibilityDetails);
  }
};
