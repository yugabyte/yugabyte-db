import { Typography } from '@material-ui/core';
import clsx from 'clsx';
import { Link } from 'react-router';
import { Trans, useTranslation } from 'react-i18next';

import { assertUnreachableCase } from '@app/utils/errorHandlingUtils';
import { useOperationBannerStyles } from '../useOperationBannerStyles';
import { AccordionCardState } from './AccordionCard';

import InfoIcon from '@app/redesign/assets/approved/info.svg';

interface PreCheckStageBannerProps {
  state: AccordionCardState;
  universeUuid: string;
  taskUuid: string;
}

const TRANSLATION_KEY_PREFIX =
  'universeActions.dbUpgrade.dbUpgradeManagementSidePanel.progressPanel.operationBanner';
export const PreCheckStageBanner = ({
  state,
  universeUuid,
  taskUuid
}: PreCheckStageBannerProps) => {
  const bannerClasses = useOperationBannerStyles();
  const { t } = useTranslation('translation', {
    keyPrefix: TRANSLATION_KEY_PREFIX
  });
  switch (state) {
    case AccordionCardState.NEUTRAL:
    case AccordionCardState.FAILED:
      return null;
    case AccordionCardState.IN_PROGRESS:
      return (
        <div className={clsx(bannerClasses.banner, bannerClasses.inProgress)}>
          <InfoIcon className={bannerClasses.icon} />
          <Typography variant="subtitle1">{t('preCheckInProgressBanner')}</Typography>
        </div>
      );
    case AccordionCardState.SUCCESS:
      return (
        <div className={clsx(bannerClasses.banner, bannerClasses.success)}>
          <InfoIcon className={bannerClasses.icon} />
          <Typography variant="subtitle1">{t('preCheckSuccessBanner')}</Typography>
        </div>
      );
    case AccordionCardState.WARNING:
      return (
        <div className={clsx(bannerClasses.banner, bannerClasses.warning)}>
          <InfoIcon className={bannerClasses.icon} />
          <Typography variant="subtitle1">
            <Trans
              t={t}
              i18nKey="preCheckFailedBanner"
              components={{
                viewDetailsLink: (
                  <Link
                    to={`/universes/${universeUuid}/tasks/${taskUuid}`}
                    target="_blank"
                    rel="noopener noreferrer"
                    className={bannerClasses.link}
                  />
                )
              }}
            />
          </Typography>
        </div>
      );
    default:
      return assertUnreachableCase(state);
  }
};
