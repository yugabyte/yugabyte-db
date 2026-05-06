import { Link as MUILink, makeStyles, Typography } from '@material-ui/core';
import clsx from 'clsx';
import { Trans, useTranslation } from 'react-i18next';

import { UpgradeStageCategory } from './constants';
import { YBA_UNIVERSE_UPGRADE_DOCUMENTATION_URL } from '../constants';

import UnavailableIcon from '@app/redesign/assets/approved/revoke-key-5.svg';

import { usePillStyles } from '@app/redesign/styles/styles';

const useStyles = makeStyles((theme) => ({
  temporaryRestrictionsContainer: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(1),

    padding: theme.spacing(2),

    color: theme.palette.grey[700],
    backgroundColor: theme.palette.grey[100],
    border: `1px solid ${theme.palette.grey[200]}`,
    borderRadius: theme.shape.borderRadius,

    '& ul': {
      paddingInlineStart: theme.spacing(2),
      margin: 0
    }
  },
  underlineLink: {
    textDecoration: 'underline',
    color: theme.palette.grey[700]
  }
}));

interface TemporaryRestrictionsNoticeProps {
  upgradeStageCategory: UpgradeStageCategory;
  isYsqlMajorUpgrade: boolean;
}

export const TRANSLATION_KEY_PREFIX =
  'universeActions.dbUpgrade.dbUpgradeManagementSidePanel.progressPanel.temporaryRestrictions';

export const TemporaryRestrictionsNotice = ({
  upgradeStageCategory,
  isYsqlMajorUpgrade
}: TemporaryRestrictionsNoticeProps) => {
  const classes = useStyles();
  const pillClasses = usePillStyles();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  const blockedOperationsMarkup = (
    <Typography variant="subtitle1">
      <Trans
        t={t}
        i18nKey={
          isYsqlMajorUpgrade
            ? 'ddlAndSomeOperationsBlockedForUpgrade'
            : 'someOperationsBlockedForUpgrade'
        }
        components={{
          dbUpgradeImpactDocLink: (
            <MUILink
              href={YBA_UNIVERSE_UPGRADE_DOCUMENTATION_URL}
              target="_blank"
              rel="noopener noreferrer"
              className={classes.underlineLink}
              underline="always"
              data-testid="db-upgrade-impact-doc-link"
            />
          )
        }}
      />
    </Typography>
  );
  return (
    <div className={classes.temporaryRestrictionsContainer}>
      <div className={clsx(pillClasses.pill, pillClasses.metadataWhite)}>
        <UnavailableIcon width={16} height={16} />
        <Typography variant="subtitle1">{t('tag')}</Typography>
      </div>
      {upgradeStageCategory === UpgradeStageCategory.UPGRADE ? (
        <ul>
          <li>{blockedOperationsMarkup}</li>
          <li>
            <Typography variant="subtitle1">{t('universeMayHaveSlowerPerformance')}</Typography>
          </li>
        </ul>
      ) : (
        blockedOperationsMarkup
      )}
    </div>
  );
};
