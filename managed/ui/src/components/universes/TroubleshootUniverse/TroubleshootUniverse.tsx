import { useState } from 'react';
import { useQuery } from 'react-query';
import { useSelector } from 'react-redux';
import { Box, makeStyles } from '@material-ui/core';
import {
  TroubleshootAdvisor,
  TroubleshootAPI,
  QUERY_KEY,
  AttachUniverse
} from '@yugabytedb/troubleshoot-ui';
import { YBErrorIndicator, YBLoading } from '../../common/indicators';
import { YBPanelItem } from '../../panels';
import { AppName } from '../../../redesign/features/Troubleshooting/TroubleshootingDashboard';
import { IN_DEVELOPMENT_MODE } from '../../../config';
import { toast } from 'react-toastify';
import { isNonEmptyObject } from '../../../utils/ObjectUtils';

const STATUS = {
  SUCCESS: 'success',
  ERROR: 'error'
};

interface TroubleshootUniverseProps {
  universeUuid: string;
  appName: AppName;
  timezone: string;
  apiUrl: string;
  platformUrl: string;
  metricsUrl: string;
}

const useStyles = makeStyles((theme) => ({
  register: {
    cursor: 'pointer'
  }
}));

export const TroubleshootUniverse = ({
  universeUuid,
  appName,
  timezone,
  apiUrl,
  platformUrl,
  metricsUrl
}: TroubleshootUniverseProps) => {
  const helperClasses = useStyles();
  const { currentCustomer } = useSelector((state: any) => state.customer);
  const [showAttachUniverseDialog, setShowAttachUniverseDialog] = useState<boolean>(false);

  const troubleshootingUniverseMetadata = useQuery(QUERY_KEY.fetchUniverseMetadataList, () =>
    TroubleshootAPI.fetchUniverseMetadataList(apiUrl, currentCustomer.data.uuid)
  );

  if (troubleshootingUniverseMetadata.isError) {
    return <YBErrorIndicator />;
  }
  if (
    troubleshootingUniverseMetadata.isLoading ||
    (troubleshootingUniverseMetadata.isIdle && troubleshootingUniverseMetadata.data === undefined)
  ) {
    return <YBLoading />;
  }

  const currentUniverseMetadata = troubleshootingUniverseMetadata?.data?.find(
    (metadata) => metadata.id === universeUuid
  );

  const onAttachUniverseDialogClose = () => {
    troubleshootingUniverseMetadata.refetch();
    setShowAttachUniverseDialog(false);
  };

  const onAttachUniverse = () => {
    troubleshootingUniverseMetadata.refetch();
    setShowAttachUniverseDialog(true);
  };

  const onUpdateMetadata = (status: string) => {
    if (status === STATUS.SUCCESS) {
      toast.success('Universe is successfully added to troubleshooting service');
    } else {
      toast.error('Unable to add universe to troubleshooting service');
    }
  };

  return isNonEmptyObject(currentUniverseMetadata) ? (
    <TroubleshootAdvisor
      universeUuid={universeUuid}
      appName={appName}
      timezone={timezone}
      apiUrl={apiUrl}
    />
  ) : (
    <YBPanelItem
      body={
        <Box>
          {'Universe is currently not registered to the troubleshooting service, '}
          <a onClick={onAttachUniverse} className={helperClasses.register}>
            {' please register here'}
          </a>
          {showAttachUniverseDialog && (
            <AttachUniverse
              universeUuid={universeUuid}
              customerUuid={currentCustomer.data.uuid}
              platformUrl={platformUrl}
              apiUrl={apiUrl}
              metricsUrl={metricsUrl}
              open={showAttachUniverseDialog}
              onUpdateMetadata={onUpdateMetadata}
              onClose={onAttachUniverseDialogClose}
              isDevMode={IN_DEVELOPMENT_MODE}
            />
          )}
        </Box>
      }
    />
  );
};
