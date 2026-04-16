import { YBPanelItem, YBErrorIndicator, YBButton } from '@yugabytedb/ui-components';
import { Box, makeStyles, Typography } from '@material-ui/core';
import { useState } from 'react';
import { MetadataFields, Universe } from '../../helpers/dtos';
import { useQuery, useQueryClient } from 'react-query';
import { TroubleshootAPI, QUERY_KEY } from '../../api';
import { UniverseListMetadata } from './UniverseListMetadata';

import { ReactComponent as LoadingIcon } from '../../assets/loading.svg';
import clsx from 'clsx';

interface TroubleshootConfigurationProps {
  customerUuid: string;
}

const useStyles = makeStyles((theme) => ({
  loadingBox: {
    position: 'fixed',
    left: '50%',
    top: '50%',
    width: '100%',
    height: '100%'
  },
  refreshButton: {
    width: '120px',
    marginTop: theme.spacing(2),
    alignSelf: 'end'
  },
  flexColumn: {
    display: 'flex',
    flexDirection: 'column'
  },
  inProgressIcon: {
    color: '#1A44A5'
  },
  icon: {
    height: '40px',
    width: '40px'
  },
  noUniverses: {
    alignSelf: 'center'
  }
}));

export const TroubleshootConfiguration = ({ customerUuid }: TroubleshootConfigurationProps) => {
  const helperClasses = useStyles();
  const [metadata, setMetadata] = useState<MetadataFields[]>([]);
  const [universeList, setUniverseList] = useState<Universe[]>([]);
  const queryClient = useQueryClient();

  const {
    isIdle: isFetchMetadataIdle,
    isLoading: isFetchMetadataLoading,
    isError: isFetchMetadataError,
    refetch: metadataRefetch
  } = useQuery(
    QUERY_KEY.fetchUniverseMetadataList,
    () => TroubleshootAPI.fetchUniverseMetadataList(),
    {
      onSuccess: (data: MetadataFields[]) => {
        setMetadata(data);
      }
    }
  );

  const {
    isIdle: isFetchUniverseIdle,
    isLoading: isFetchUniverseLoading,
    isError: isFetchUniverseError,
    refetch: universeListRefetch
  } = useQuery(
    QUERY_KEY.fetchUniverseListDetails,
    () => TroubleshootAPI.fetchUniverseListDetails(),
    {
      onSuccess: (data: Universe[]) => {
        setUniverseList(data);
      }
    }
  );

  if (isFetchMetadataError || isFetchUniverseError) {
    <YBErrorIndicator customErrorMessage={'Unable to fetch universe details'} />;
  }

  if (
    isFetchMetadataLoading ||
    isFetchUniverseLoading ||
    (isFetchMetadataIdle && metadata === undefined) ||
    (isFetchUniverseIdle && universeList === undefined)
  ) {
    return (
      <Box className={helperClasses.loadingBox}>
        <LoadingIcon className={clsx(helperClasses.icon, helperClasses.inProgressIcon)} />
      </Box>
    );
  }

  const onActionPerformed = () => {
    const getLatestMetadata = async () => {
      queryClient.invalidateQueries(QUERY_KEY.fetchUniverseMetadataList);
      queryClient.invalidateQueries(QUERY_KEY.fetchUniverseListDetails);
      await metadataRefetch();
      await universeListRefetch();
    };
    getLatestMetadata();
  };

  return (
    <YBPanelItem
      body={
        <Box ml={1} mr={1} mt={1} className={helperClasses.flexColumn}>
          <YBButton
            variant="secondary"
            size="medium"
            onClick={() => {
              onActionPerformed();
            }}
            className={helperClasses.refreshButton}
          >
            {'Refresh'}
          </YBButton>

          {metadata.length > 0 ? (
            <>
              <Box mt={2}>
                <UniverseListMetadata
                  metadata={metadata}
                  universeList={universeList}
                  customerUuid={customerUuid}
                  onActionPerformed={onActionPerformed}
                />
              </Box>
            </>
          ) : (
            <Box className={helperClasses.noUniverses}>
              {'Currently, there are no universes attached to troubleshooting service'}
            </Box>
          )}
        </Box>
      }
    />
  );
};
