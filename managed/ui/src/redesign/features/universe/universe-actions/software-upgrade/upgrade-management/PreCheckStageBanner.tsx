import { Link as MUILink, Typography } from '@material-ui/core';
import clsx from 'clsx';
import { Trans, useTranslation } from 'react-i18next';
import { useDispatch } from 'react-redux';

import { showTaskInDrawer } from '@app/actions/tasks';
import { assertUnreachableCase } from '@app/utils/errorHandlingUtils';
import { useOperationBannerStyles } from '../useOperationBannerStyles';
import { AccordionCardState } from './AccordionCard';

import InfoIcon from '@app/redesign/assets/approved/info.svg';

interface PreCheckStageBannerProps {
  state: AccordionCardState;
  taskUuid: string;
  onCloseSidePanel: () => void;
}

const TRANSLATION_KEY_PREFIX =
  'universeActions.dbUpgrade.dbUpgradeManagementSidePanel.progressPanel.operationBanner';
export const PreCheckStageBanner = ({
  state,
  taskUuid,
  onCloseSidePanel
}: PreCheckStageBannerProps) => {
  const dispatch = useDispatch();
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
                  <MUILink
                    component="button"
                    type="button"
                    className={bannerClasses.link}
                    underline="always"
                    onClick={(event) => {
                      event.preventDefault();
                      event.stopPropagation();
                      dispatch(showTaskInDrawer(taskUuid));
                      onCloseSidePanel();
                    }}
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
