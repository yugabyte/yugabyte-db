import { Box, makeStyles, Typography, useTheme } from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';
import { YBModal, YBModalProps } from '../../../redesign/components';
import {
  I18N_KEY_PREFIX_XCLUSTER_SCHEMA_CHANGE_MODE,
  I18N_KEY_PREFIX_XCLUSTER_TERMS,
  XCLUSTER_DR_DDL_STEPS_DOCUMENTATION_URL,
  XCLUSTER_REPLICATION_DDL_STEPS_DOCUMENTATION_URL
} from '../constants';
import { getSchemaChangeMode } from '../ReplicationUtils';
import { ReactComponent as DocumentationIcon } from '../../../redesign/assets/documentation.svg';
import { ReactComponent as UpArrow } from '../../../redesign/assets/upgrade-arrow.svg';

import { XClusterConfig } from '../dtos';

interface UpgradeXClusterModalProps {
  xClusterConfig: XClusterConfig;
  isDrInterface: boolean;
  modalProps: YBModalProps;
}

const useStyles = makeStyles((theme) => ({
  learnMoreLink: {
    display: 'flex',
    gap: theme.spacing(0.5),
    alignItems: 'center',

    color: theme.palette.orange[500],
    cursor: 'pointer',
    textDecoration: 'underline'
  }
}));

const MODAL_NAME = 'UpgradeXClusterModal';
const TRANSLATION_KEY_PREFIX = 'clusterDetail.xCluster.shared.upgradeModal';

export const UpgradeXClusterModal = ({
  xClusterConfig,
  isDrInterface,
  modalProps
}: UpgradeXClusterModalProps) => {
  const { t } = useTranslation('translation', { keyPrefix: TRANSLATION_KEY_PREFIX });
  const theme = useTheme();
  const classes = useStyles();

  const modalTitle = t('title');
  const cancelLabel = t('close', { keyPrefix: 'common' });
  const schemaChangeMode = getSchemaChangeMode(xClusterConfig);

  return (
    <YBModal
      title={modalTitle}
      titleIcon={<UpArrow />}
      cancelLabel={cancelLabel}
      size="md"
      cancelTestId={`${MODAL_NAME}_CancelButton`}
      {...modalProps}
    >
      <Box display="flex" flexDirection="column" gridGap={theme.spacing(2)} paddingTop={2}>
        <Typography variant="body2">
          <Trans
            i18nKey={`${TRANSLATION_KEY_PREFIX}.upgradeInfo`}
            components={{ bold: <b />, paragraph: <p /> }}
            values={{
              schemaChangeMode: t(`mode.${schemaChangeMode}`, {
                keyPrefix: I18N_KEY_PREFIX_XCLUSTER_SCHEMA_CHANGE_MODE
              }),
              xClusterOffering: t(`offering.${isDrInterface ? 'dr' : 'xClusterReplication'}`, {
                keyPrefix: I18N_KEY_PREFIX_XCLUSTER_TERMS
              })
            }}
          />
        </Typography>
        <Typography variant="body2" className={classes.learnMoreLink}>
          <DocumentationIcon />
          <a
            href={
              isDrInterface
                ? XCLUSTER_DR_DDL_STEPS_DOCUMENTATION_URL
                : XCLUSTER_REPLICATION_DDL_STEPS_DOCUMENTATION_URL
            }
            target="_blank"
            rel="noopener noreferrer"
          >
            {t('learnMore')}
          </a>
        </Typography>
      </Box>
    </YBModal>
  );
};
