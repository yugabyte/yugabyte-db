import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Box, makeStyles } from '@material-ui/core';
import { YBButton, YBInput, YBLabel } from '../../components';
import { YBPanelItem } from '../../../components/panels';
import { EditTPConfigDialog } from './TroubleshootingDialogs/EditTPConfigDialog';
import { DeleteTPConfigDialog } from './TroubleshootingDialogs/DeleteTPConfigDialog';
import { ROOT_URL } from '../../../config';

const useStyles = makeStyles((theme) => ({
  infoBox: {
    marginTop: theme.spacing(2),
    display: 'flex',
    flexDirection: 'row'
  },
  textBox: {
    width: '400px'
  },
  buttonBox: {
    marginTop: theme.spacing(6)
  },
  button: {
    marginLeft: theme.spacing(2)
  }
}));

interface TroubleshootingConfigInfoProps {
  tpUrl: string;
  ybaUrl: string;
  metricsUrl: string;
  inUseStatus: boolean;
  tpUuid: string;
  customerUUID: string;
  onRefetchConfig: () => void;
}

export const TroubleshootingConfigInfo = ({
  tpUrl,
  ybaUrl,
  metricsUrl,
  tpUuid,
  customerUUID,
  inUseStatus,
  onRefetchConfig
}: TroubleshootingConfigInfoProps) => {
  const { t } = useTranslation();
  const helperClasses = useStyles();
  const [showEditTPConfigDialog, setShowEditTPConfigDialog] = useState<boolean>(false);
  const [showDeleteTPConfigDialog, setShowDeleteTPConfigDialog] = useState<boolean>(false);

  // TODO: Checkin code and remove all logs after we verify from internal dev portal
  console.warn('ROOT_URL', ROOT_URL);
  const splitUrl = ROOT_URL.split('/api/');
  console.warn('splitUrl', splitUrl);
  const baseUrl = window.location.origin;
  console.warn('baseUrl', baseUrl);

  const onEditTPConfigButtonClick = () => {
    setShowEditTPConfigDialog(true);
  };

  const onEditTPConfigDialogClose = () => {
    setShowEditTPConfigDialog(false);
  };

  const onDeleteTPConfigButtonClick = () => {
    setShowDeleteTPConfigDialog(true);
  };

  const onDeleteTPConfigDialogClose = () => {
    setShowDeleteTPConfigDialog(false);
  };

  const configData = {
    customerUUID,
    tpUrl,
    ybaUrl,
    metricsUrl,
    tpUuid,
    inUseStatus
  };

  return (
    <YBPanelItem
      body={
        <Box>
          <Box className={helperClasses.infoBox}>
            <YBLabel dataTestId="TroubleshootConfigInfo-TpUrlLabel" width="300px">
              {t('clusterDetail.troubleshoot.tpServiceUrlLabel')}
            </YBLabel>
            <YBInput
              name="id"
              type="text"
              value={tpUrl}
              disabled
              className={helperClasses.textBox}
            />
          </Box>
          <Box className={helperClasses.infoBox}>
            <YBLabel dataTestId="TroubleshootConfigInfo-ybaUrlLabel" width="300px">
              {t('clusterDetail.troubleshoot.ybPlatformServiceUrlLabel')}
            </YBLabel>
            <YBInput type="text" disabled value={ybaUrl} className={helperClasses.textBox} />
          </Box>
          <Box className={helperClasses.infoBox}>
            <YBLabel dataTestId="TroubleshootConfigInfo-metricsUrlLabel" width="300px">
              {t('clusterDetail.troubleshoot.ybPlatformMetricsUrlLabel')}
            </YBLabel>
            <YBInput type="text" disabled value={metricsUrl} className={helperClasses.textBox} />
          </Box>
          <Box className={helperClasses.buttonBox}>
            <YBButton variant="primary" size="large" onClick={onEditTPConfigButtonClick}>
              {t('common.edit')}
            </YBButton>
            <YBButton
              variant="primary"
              size="large"
              className={helperClasses.button}
              onClick={onDeleteTPConfigButtonClick}
            >
              {t('common.delete')}
            </YBButton>
          </Box>
          {showEditTPConfigDialog && (
            <EditTPConfigDialog
              open={showEditTPConfigDialog}
              onRefetchConfig={onRefetchConfig}
              onClose={onEditTPConfigDialogClose}
              data={configData}
            />
          )}
          {showDeleteTPConfigDialog && (
            <DeleteTPConfigDialog
              open={showDeleteTPConfigDialog}
              onRefetchConfig={onRefetchConfig}
              onClose={onDeleteTPConfigDialogClose}
              data={configData}
            />
          )}
        </Box>
      }
      noBackground
    />
  );
};
