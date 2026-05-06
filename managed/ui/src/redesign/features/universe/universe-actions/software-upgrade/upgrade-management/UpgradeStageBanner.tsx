import { Link as MUILink, Typography } from '@material-ui/core';
import clsx from 'clsx';
import { Link } from 'react-router';
import { Trans, useTranslation } from 'react-i18next';

import { assertUnreachableCase } from '@app/utils/errorHandlingUtils';
import { useOperationBannerStyles } from '../useOperationBannerStyles';
import { AccordionCardState } from './AccordionCard';
import { YBA_UNIVERSE_UPGRADE_DOCUMENTATION_URL } from '../constants';

import InfoIcon from '@app/redesign/assets/approved/info.svg';

interface UpgradeStageBannerProps {
  state: AccordionCardState;
  universeUuid: string;
  taskUuid: string;
}

const TRANSLATION_KEY_PREFIX =
  'universeActions.dbUpgrade.dbUpgradeManagementSidePanel.progressPanel.operationBanner';
export const UpgradeStageBanner = ({ state, universeUuid, taskUuid }: UpgradeStageBannerProps) => {
  const bannerClasses = useOperationBannerStyles();
  const { t } = useTranslation('translation', {
    keyPrefix: TRANSLATION_KEY_PREFIX
  });
  switch (state) {
    case AccordionCardState.NEUTRAL:
    case AccordionCardState.WARNING:
      return null;
    case AccordionCardState.IN_PROGRESS:
      return (
        <div className={clsx(bannerClasses.banner, bannerClasses.inProgress)}>
          <InfoIcon className={bannerClasses.icon} />
          <Typography variant="subtitle1">
            <Trans
              t={t}
              i18nKey="upgradeInProgressBanner"
              components={{
                learnMoreLink: (
                  <MUILink
                    href={YBA_UNIVERSE_UPGRADE_DOCUMENTATION_URL}
                    target="_blank"
                    rel="noopener noreferrer"
                    underline="always"
                    className={bannerClasses.link}
                    data-testid="upgrade-in-progress-learn-more-link"
                  />
                )
              }}
            />
          </Typography>
        </div>
      );
    case AccordionCardState.SUCCESS:
      return (
        <div className={clsx(bannerClasses.banner, bannerClasses.success)}>
          <InfoIcon className={bannerClasses.icon} />
          <Typography variant="subtitle1">{t('upgradeSuccessBanner')}</Typography>
        </div>
      );
    case AccordionCardState.FAILED:
      return (
        <div className={clsx(bannerClasses.banner, bannerClasses.error)}>
          <InfoIcon className={bannerClasses.icon} />
          <Typography variant="subtitle1">
            <Trans
              t={t}
              i18nKey="upgradeFailedBanner"
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
