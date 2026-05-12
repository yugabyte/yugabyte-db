import { makeStyles, Typography } from '@material-ui/core';
import { YBTag, YBTooltip } from '@yugabyte-ui-library/core';
import { Trans, useTranslation } from 'react-i18next';
import { useQuery } from 'react-query';

import { usePillStyles } from '@app/redesign/styles/styles';
import { precheckSoftwareUpgrade } from '@app/v2/api/universe/universe';

import DocumentationIcon from '@app/redesign/assets/documentation.svg';
import InfoIcon from '@app/redesign/assets/info-message.svg';
import UnavailableIcon from '@app/redesign/assets/approved/revoke-key-5.svg';
import UpgradeIcon from '@app/redesign/assets/upgrade.svg';

const TRANSLATION_KEY_PREFIX =
  'universeActions.dbUpgrade.upgradeModal.dbVersionStep.upgradeInfoCard';

const useStyles = makeStyles((theme) => ({
  upgradeInfoCard: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(2),

    width: '100%',
    minWidth: 550,
    height: 'fit-content',
    minHeight: 200,
    padding: theme.spacing(2),

    backgroundColor: theme.palette.common.white,
    border: `1px solid ${theme.palette.grey[200]}`,
    borderRadius: theme.shape.borderRadius
  },
  upgradeInfoHeader: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(1)
  },
  ysqlMajorUpgradeTag: {
    ...theme.typography.subtitle1,

    display: 'flex',
    gap: theme.spacing(0.5),
    alignItems: 'center',

    height: 24,
    width: 'fit-content',
    padding: theme.spacing(0.5, 0.75),

    borderRadius: 6,
    backgroundColor: theme.palette.secondary[600],
    color: theme.palette.common.white,
    fontWeight: 500,
    lineHeight: '16px',
    letterSpacing: '0.2px'
  },
  ysqlMajorUpgradeIcon: {
    display: 'inline-flex',
    flexShrink: 0,

    width: 16,
    height: 16,
    '& svg': {
      width: '100%',
      height: '100%'
    }
  },
  upgradeInfoDescription: {
    color: theme.palette.grey[700],
    fontSize: '11.5px',
    lineHeight: '18px'
  },
  versionString: {
    color: theme.palette.grey[900],
    fontSize: '11.5px',
    fontWeight: 600,
    lineHeight: '18px'
  },
  upgradeInfoMetadata: {
    display: 'flex',
    flexDirection: 'column',
    gap: 2,

    '& ul': {
      paddingInlineStart: theme.spacing(2),
      listStyleType: 'disc'
    },

    '& li': {
      color: theme.palette.grey[800],
      fontSize: '11.5px',
      lineHeight: '18px'
    }
  },
  upgradeInfoMetadataItem: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(0.5)
  },
  upgradeRestrictionsBox: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(1.5),

    padding: theme.spacing(2),

    backgroundColor: theme.palette.grey[50],
    border: `1px solid ${theme.palette.grey[200]}`,
    borderRadius: theme.shape.borderRadius,

    '& ul': {
      paddingInlineStart: theme.spacing(2),
      listStyleType: 'disc'
    },

    '& li': {
      color: theme.palette.grey[700],
      fontSize: '11.5px',
      lineHeight: '18px'
    }
  },
  underlineLink: {
    cursor: 'pointer',
    textDecoration: 'underline'
  },
  learnMoreLink: {
    alignSelf: 'flex-end',

    display: 'inline-flex',
    alignItems: 'center',
    gap: theme.spacing(0.5),

    color: theme.palette.primary[600],
    cursor: 'pointer',
    lineHeight: '16px'
  }
}));

export interface DbUpgradeInfoCardProps {
  currentUniverseUuid: string;
  currentVersion: string;
  targetVersion: string;
}

export const DbUpgradeInfoCard = ({
  currentUniverseUuid,
  currentVersion,
  targetVersion
}: DbUpgradeInfoCardProps) => {
  const classes = useStyles();
  const pillClasses = usePillStyles();
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });

  const dbUpgradeMetaQuery = useQuery(
    ['softwareUpgradePrecheck', currentUniverseUuid, targetVersion],
    () =>
      precheckSoftwareUpgrade(currentUniverseUuid, {
        yb_software_version: targetVersion
      })
  );

  const precheckData = dbUpgradeMetaQuery.data;
  const isYsqlMajorUpgrade = precheckData?.ysql_major_version_upgrade ?? false;
  const isFinalizationRequired = precheckData?.finalize_required ?? false;

  const getRestrictionsKey = () => {
    if (isYsqlMajorUpgrade) return 'ddlAndSomeOperationsBlocked';
    if (isFinalizationRequired) return 'someOperationsBlockedFinalization';
    return 'someOperationsBlocked';
  };

  return (
    <div className={classes.upgradeInfoCard}>
      <div className={classes.upgradeInfoHeader}>
        {isYsqlMajorUpgrade && (
          <div className={classes.ysqlMajorUpgradeTag}>
            <UpgradeIcon width={16} height={16} />
            <span>{t('ysqlMajorVersionUpgrade')}</span>
          </div>
        )}

        <Typography variant="body2" className={classes.upgradeInfoDescription}>
          <Trans
            t={t}
            i18nKey={isYsqlMajorUpgrade ? 'ysqlMajorVersionUpgradeInfo' : 'upgradeInfo'}
            components={{
              versionString: (
                <Typography variant="body1" component="span" className={classes.versionString} />
              )
            }}
            values={{ targetVersion }}
          />
        </Typography>
      </div>

      <div className={classes.upgradeInfoMetadata}>
        <ul>
          {isFinalizationRequired && (
            <li>
              <div className={classes.upgradeInfoMetadataItem}>
                <b>{t('finalizationRequired')}</b>
                <YBTooltip title={t('finalizationRequiredTooltip')}>
                  <InfoIcon />
                </YBTooltip>
              </div>
            </li>
          )}
          <li>
            <div className={classes.upgradeInfoMetadataItem}>
              <Trans
                t={t}
                i18nKey={isFinalizationRequired ? 'rollbackAvailable' : 'rollbackAvailableAnytime'}
                components={{ bold: <b /> }}
                values={{ currentVersion }}
              />
            </div>
          </li>
        </ul>
      </div>

      <div className={classes.upgradeRestrictionsBox}>
        <YBTag size="medium" variant="light" startIcon={<UnavailableIcon width={16} height={16} />}>
          {t('temporaryRestrictions')}
        </YBTag>
        <ul>
          <li>
            <Trans
              t={t}
              i18nKey={getRestrictionsKey()}
              components={{ underline: <span className={classes.underlineLink} /> }}
            />
          </li>
          <li>{t('clusterMayHaveSlowerPerformance')}</li>
        </ul>
      </div>

      {isYsqlMajorUpgrade && (
        <Typography variant="subtitle1" className={classes.learnMoreLink}>
          <DocumentationIcon width={16} height={16} />
          <span>{t('learnMoreAboutUpgrade')}</span>
        </Typography>
      )}
    </div>
  );
};
