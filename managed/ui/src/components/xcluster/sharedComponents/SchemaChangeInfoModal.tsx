import { Box, makeStyles, Typography, useTheme } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';
import { useState } from 'react';
import clsx from 'clsx';

import { YBModal, YBModalProps, YBTooltip } from '../../../redesign/components';
import { formatYbSoftwareVersionString } from '../../../utils/Formatters';
import {
  I18N_KEY_PREFIX_XCLUSTER_SCHEMA_CHANGE_MODE,
  I18N_KEY_PREFIX_XCLUSTER_TERMS,
  XCLUSTER_DR_DDL_STEPS_DOCUMENTATION_URL,
  XCLUSTER_REPLICATION_DDL_STEPS_DOCUMENTATION_URL
} from '../constants';
import { getLatestSchemaChangeModeSupported, getSchemaChangeMode } from '../ReplicationUtils';
import { ReactComponent as DocumentationIcon } from '../../../redesign/assets/documentation.svg';
import { ReactComponent as InfoIcon } from '../../../redesign/assets/info-message.svg';
import { ReactComponent as UpArrow } from '../../../redesign/assets/upgrade-arrow.svg';
import { UpgradeXClusterModal } from './UpgradeXClusterModal';

import { XClusterConfig } from '../dtos';

import { usePillStyles } from '../../../redesign/styles/styles';

interface SchemaChangeModeInfoModalCommonProps {
  modalProps: YBModalProps;
}

type SchemaChangeModeInfoModalProps =
  | (SchemaChangeModeInfoModalCommonProps & {
      xClusterConfig: XClusterConfig;
      sourceUniverseVersion: string;
      targetUniverseVersion: string;
      isDrInterface: boolean;
      isConfigInterface: true;
    })
  | (SchemaChangeModeInfoModalCommonProps & {
      currentUniverseVersion: string;
      isConfigInterface: false;
    });

const useStyles = makeStyles((theme) => ({
  titleDetailsPill: {
    width: 'fit-content',
    padding: theme.spacing(0.25, 0.75),

    fontSize: '10px',
    lineHeight: '16px',
    borderRadius: '4px',
    border: `1px solid ${theme.palette.grey[300]}`
  },
  upArrowIcon: {
    width: '14px',
    height: '14px'
  },
  label: {
    display: 'flex',
    gap: theme.spacing(1),
    alignItems: 'center'
  },
  metadataContainer: {
    display: 'flex',
    flex: 1,
    flexDirection: 'column',
    gap: theme.spacing(1)
  },
  universeLevelLearnMoreInstructions: {
    display: 'flex',
    gap: theme.spacing(0.5),
    alignItems: 'center'
  },
  universeLevelLearnMoreContainer: {
    marginTop: 'auto',

    '& li': {
      listStyleType: 'disc'
    },
    '& a': {
      textDecoration: 'underline',
      color: theme.palette.ybacolors.linkBlue
    }
  },
  learnMoreLink: {
    display: 'flex',
    gap: theme.spacing(0.5),
    alignItems: 'center',

    cursor: 'pointer',

    '& a': {
      textDecoration: 'underline',
      color: theme.palette.ybacolors.linkBlue
    }
  },
  upgradeAvailableLink: {
    display: 'flex',
    gap: theme.spacing(0.5),
    alignItems: 'center',

    color: theme.palette.orange[500],
    cursor: 'pointer',
    textDecoration: 'underline'
  },
  dbVersionUpgradeTooltip: {
    marginTop: theme.spacing(1)
  }
}));

const MODAL_NAME = 'SchemaChangesModal';
const TRANSLATION_KEY_PREFIX = 'clusterDetail.xCluster.shared.schemaChangeModal';

export const SchemaChangeModeInfoModal = (props: SchemaChangeModeInfoModalProps) => {
  const [isUpgradeXClusterModalOpen, setIsUpgradeXClusterModalOpen] = useState<boolean>(false);

  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const classes = useStyles();
  const pillClasses = usePillStyles();

  const theme = useTheme();

  const openUpgradeXClusterModal = () => setIsUpgradeXClusterModalOpen(true);
  const closeUpgradeXClusterModal = () => setIsUpgradeXClusterModalOpen(false);

  const modalTitle = (
    <Box display="flex" alignContent="center" gridGap={theme.spacing(1)}>
      <Typography variant="h4" data-testid="YBModal-Title">
        {t('title')}
      </Typography>
      {!props.isConfigInterface && (
        <div className={classes.titleDetailsPill}>{t('titleDetails')}</div>
      )}
    </Box>
  );
  const cancelLabel = t('close', { keyPrefix: 'common' });

  // -------------
  // The following are only used if we this modal is shown for a specific replication config.
  const schemaChangeMode = props.isConfigInterface
    ? getSchemaChangeMode(props.xClusterConfig)
    : null;
  const latestSchemaChangeModeSupported = props.isConfigInterface
    ? getLatestSchemaChangeModeSupported(props.sourceUniverseVersion, props.targetUniverseVersion)
    : null;
  const isLatestSchemaChangeModeUsed = schemaChangeMode === latestSchemaChangeModeSupported;
  const dbVersionTooltipText = props.isConfigInterface
    ? t('universeVersion.tooltip', {
        xClusterOffering: t(`offering.${props.isDrInterface ? 'dr' : 'xClusterReplication'}`, {
          keyPrefix: I18N_KEY_PREFIX_XCLUSTER_TERMS
        })
      })
    : '';
  const dbVersionUpgradeTooltipText = props.isConfigInterface
    ? t('universeVersion.tooltipUpgradePrompt', {
        xClusterOffering: t(`offering.${props.isDrInterface ? 'dr' : 'xClusterReplication'}`, {
          keyPrefix: I18N_KEY_PREFIX_XCLUSTER_TERMS
        }),
        sourceUniverseTerm: t(`source.${props.isDrInterface ? 'dr' : 'xClusterReplication'}`, {
          keyPrefix: I18N_KEY_PREFIX_XCLUSTER_TERMS
        }),
        targetUniverseTerm: t(`target.${props.isDrInterface ? 'dr' : 'xClusterReplication'}`, {
          keyPrefix: I18N_KEY_PREFIX_XCLUSTER_TERMS
        })
      })
    : '';
  const dbVersionTooltipComponent = (
    <>
      <Typography variant="body2">{dbVersionTooltipText}</Typography>
      {!isLatestSchemaChangeModeUsed && (
        <Typography variant="body2" className={classes.dbVersionUpgradeTooltip}>
          {dbVersionUpgradeTooltipText}
        </Typography>
      )}
    </>
  );

  const participantDatabaseVersionMetadata = props.isConfigInterface ? (
    <Box
      display="flex"
      borderBottom={`1px solid ${theme.palette.ybacolors.ybBorderGray}`}
      paddingBottom={2}
    >
      <div className={classes.metadataContainer}>
        <Typography variant="body1" component="span" className={classes.label}>
          {t('universeVersion.label', {
            universeTerm: t(
              `source.${props.isDrInterface ? 'dr_titleCase' : 'xClusterReplication_titleCase'}`,
              {
                keyPrefix: I18N_KEY_PREFIX_XCLUSTER_TERMS
              }
            )
          })}
          <YBTooltip title={dbVersionTooltipComponent}>
            <InfoIcon />
          </YBTooltip>
        </Typography>
        <Typography variant="body2">
          {formatYbSoftwareVersionString(props.sourceUniverseVersion)}
        </Typography>
      </div>
      <div className={classes.metadataContainer}>
        <Typography variant="body1" component="span" className={classes.label}>
          {t('universeVersion.label', {
            universeTerm: t(
              `target.${props.isDrInterface ? 'dr_titleCase' : 'xClusterReplication_titleCase'}`,
              {
                keyPrefix: I18N_KEY_PREFIX_XCLUSTER_TERMS
              }
            )
          })}
          <YBTooltip title={dbVersionTooltipComponent}>
            <InfoIcon />
          </YBTooltip>
        </Typography>
        <Typography variant="body2">
          {formatYbSoftwareVersionString(props.targetUniverseVersion)}
        </Typography>
      </div>
    </Box>
  ) : null;
  // -------------

  return (
    <YBModal
      customTitle={modalTitle}
      cancelLabel={cancelLabel}
      size="md"
      cancelTestId={`${MODAL_NAME}_CancelButton`}
      {...props.modalProps}
    >
      <Box display="flex" flexDirection="column" height="100%">
        {props.isConfigInterface ? (
          participantDatabaseVersionMetadata
        ) : (
          <Box
            display="flex"
            borderBottom={`1px solid ${theme.palette.ybacolors.ybBorderGray}`}
            paddingBottom={2}
          >
            <div className={classes.metadataContainer}>
              <Typography variant="body1" component="span" className={classes.label}>
                {t('universeVersion.universeLevelLabel')}
                <YBTooltip
                  title={
                    <Typography variant="body2">
                      {t('universeVersion.universeLevelTooltip')}
                    </Typography>
                  }
                >
                  <InfoIcon />
                </YBTooltip>
              </Typography>
              <Typography variant="body2">
                {formatYbSoftwareVersionString(props.currentUniverseVersion)}
              </Typography>
            </div>
          </Box>
        )}
        {props.isConfigInterface && (
          <Box
            borderBottom={`1px solid ${theme.palette.ybacolors.ybBorderGray}`}
            paddingTop={2}
            paddingBottom={2}
          >
            <div className={classes.metadataContainer}>
              <Typography variant="body1">{t('schemaChanges')}</Typography>
              <Box display="flex" gridGap={theme.spacing(1)} alignItems="center">
                <Typography variant="body2">
                  {t(`mode.${schemaChangeMode}_titleCase`, {
                    keyPrefix: I18N_KEY_PREFIX_XCLUSTER_SCHEMA_CHANGE_MODE
                  })}
                </Typography>
                <div className={clsx(pillClasses.pill, pillClasses.productOrange)}>
                  {t(`mode.${schemaChangeMode}_pill`, {
                    keyPrefix: I18N_KEY_PREFIX_XCLUSTER_SCHEMA_CHANGE_MODE
                  })}
                </div>
                {!isLatestSchemaChangeModeUsed && (
                  <Typography
                    variant="body2"
                    className={classes.upgradeAvailableLink}
                    onClick={openUpgradeXClusterModal}
                  >
                    <UpArrow className={classes.upArrowIcon} />
                    {t('upgradeAvailable', {
                      keyPrefix: I18N_KEY_PREFIX_XCLUSTER_SCHEMA_CHANGE_MODE
                    })}
                  </Typography>
                )}
              </Box>
            </div>
          </Box>
        )}
        <Box
          display="flex"
          flexDirection="column"
          gridGap={theme.spacing(2)}
          paddingTop={2}
          flex={1}
        >
          <Typography variant="body2">
            <Trans
              i18nKey={`${TRANSLATION_KEY_PREFIX}.${
                props.isConfigInterface
                  ? 'schemaChangesInstructions'
                  : 'universeLevelSchemaChangesInstructions'
              }`}
              components={{ bold: <b /> }}
            />
          </Typography>
          {props.isConfigInterface ? (
            <Typography variant="body2" className={classes.learnMoreLink}>
              <DocumentationIcon />
              <a
                href={
                  props.isDrInterface
                    ? XCLUSTER_DR_DDL_STEPS_DOCUMENTATION_URL
                    : XCLUSTER_REPLICATION_DDL_STEPS_DOCUMENTATION_URL
                }
                target="_blank"
                rel="noopener noreferrer"
              >
                {t('learnMore')}
              </a>
            </Typography>
          ) : (
            <div className={classes.universeLevelLearnMoreContainer}>
              <Typography variant="body2" className={classes.universeLevelLearnMoreInstructions}>
                <DocumentationIcon />
                {t('universeLevelLearnMore')}
              </Typography>
              <ul>
                <li>
                  <a
                    href={XCLUSTER_DR_DDL_STEPS_DOCUMENTATION_URL}
                    target="_blank"
                    rel="noopener noreferrer"
                  >
                    {t('universeLevelLearnMoreDrDocsLink')}
                  </a>
                </li>
                <li>
                  <a
                    href={XCLUSTER_REPLICATION_DDL_STEPS_DOCUMENTATION_URL}
                    target="_blank"
                    rel="noopener noreferrer"
                  >
                    {t('universeLevelLearnMoreXClusterReplicationDocsLink')}
                  </a>
                </li>
              </ul>
            </div>
          )}
        </Box>
      </Box>
      {isUpgradeXClusterModalOpen && props.isConfigInterface && (
        <UpgradeXClusterModal
          xClusterConfig={props.xClusterConfig}
          isDrInterface={true}
          modalProps={{ open: isUpgradeXClusterModalOpen, onClose: closeUpgradeXClusterModal }}
        />
      )}
    </YBModal>
  );
};
