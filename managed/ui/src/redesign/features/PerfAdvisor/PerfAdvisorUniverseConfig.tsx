import { useState } from 'react';
import { useTranslation } from 'react-i18next';
import { Box, makeStyles } from '@material-ui/core';
import { YBButton, YBInput, YBLabel } from '../../components';
import { YBPanelItem } from '../../../components/panels';
import { UnregisterPerfAdvisorDialog } from './PerfAdvisorDialog/UnregisterPerfAdvisorDialog';
import { EditPerfAdvisorConfigDialog } from './PerfAdvisorDialog/EditPerfAdvisorConfigDialog';

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

interface PerfAdvisorUniverseConfigProps {
  tpUrl: string;
  ybaUrl: string;
  metricsUrl: string;
  metricsScrapePeriodSecs: number;
  inUseStatus: boolean;
  tpUuid: string;
  customerUUID: string;
  apiToken: string;
  tpApiToken: string;
  onRefetchConfig: () => void;
}

export const PerfAdvisorUniverseConfig = ({
  tpUrl,
  ybaUrl,
  metricsUrl,
  metricsScrapePeriodSecs,
  tpUuid,
  customerUUID,
  inUseStatus,
  apiToken,
  tpApiToken,
  onRefetchConfig
}: PerfAdvisorUniverseConfigProps) => {
  const { t } = useTranslation();
  const helperClasses = useStyles();
  const [showEditPaConfigDialog, setShowEditPaConfigDialog] = useState<boolean>(false);
  const [showDeletePaConfigDialog, setShowDeletePaConfigDialog] = useState<boolean>(false);

  const onEditPaConfigButtonClick = () => {
    setShowEditPaConfigDialog(true);
  };

  const onEditPaConfigDialogClose = () => {
    setShowEditPaConfigDialog(false);
  };

  const onDeletePaConfigButtonClick = () => {
    setShowDeletePaConfigDialog(true);
  };

  const onDeletePaConfigDialogClose = () => {
    setShowDeletePaConfigDialog(false);
  };

  const configData = {
    customerUUID,
    tpUrl,
    ybaUrl,
    apiToken,
    tpApiToken,
    metricsUrl,
    metricsScrapePeriodSecs,
    tpUuid,
    inUseStatus
  };

  return (
    <YBPanelItem
      body={
        <Box>
          <Box className={helperClasses.infoBox}>
            <YBLabel dataTestId="PerfAdvisorUniverseConfig-TpUrlLabel" width="300px">
              {t('clusterDetail.troubleshoot.paServiceUrlLabel')}
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
            <YBLabel dataTestId="PerfAdvisorUniverseConfig-ybaUrlLabel" width="300px">
              {t('clusterDetail.troubleshoot.ybPlatformServiceUrlLabel')}
            </YBLabel>
            <YBInput type="text" disabled value={ybaUrl} className={helperClasses.textBox} />
          </Box>
          <Box className={helperClasses.infoBox}>
            <YBLabel dataTestId="PerfAdvisorUniverseConfig-metricsUrlLabel" width="300px">
              {t('clusterDetail.troubleshoot.ybPlatformMetricsUrlLabel')}
            </YBLabel>
            <YBInput type="text" disabled value={metricsUrl} className={helperClasses.textBox} />
          </Box>
          <Box className={helperClasses.infoBox}>
            <YBLabel
              dataTestId="PerfAdvisorUniverseConfig-metricsScrapePeriodSecLabel"
              width="300px"
            >
              {t('clusterDetail.troubleshoot.metricsScrapePeriodSecLabel')}
            </YBLabel>
            <YBInput
              type="text"
              disabled
              value={metricsScrapePeriodSecs}
              className={helperClasses.textBox}
            />
          </Box>
          <Box className={helperClasses.buttonBox}>
            <YBButton variant="primary" size="large" onClick={onEditPaConfigButtonClick}>
              {t('common.edit')}
            </YBButton>
            <YBButton
              variant="primary"
              size="large"
              className={helperClasses.button}
              onClick={onDeletePaConfigButtonClick}
            >
              {t('common.delete')}
            </YBButton>
          </Box>
          {showEditPaConfigDialog && (
            <EditPerfAdvisorConfigDialog
              open={showEditPaConfigDialog}
              onRefetchConfig={onRefetchConfig}
              onClose={onEditPaConfigDialogClose}
              data={configData}
            />
          )}
          {showDeletePaConfigDialog && (
            <UnregisterPerfAdvisorDialog
              open={showDeletePaConfigDialog}
              onRefetchConfig={onRefetchConfig}
              onClose={onDeletePaConfigDialogClose}
              data={configData}
            />
          )}
        </Box>
      }
      noBackground
    />
  );
};
