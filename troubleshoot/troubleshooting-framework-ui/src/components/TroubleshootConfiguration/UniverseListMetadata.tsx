import { YBButton } from '@yugabytedb/ui-components';
import { MetadataFields } from '../../helpers/dtos';

import {
  Box,
  Paper,
  TableCell,
  TableHead,
  Table,
  TableRow,
  TableBody,
  TableContainer,
  makeStyles,
  Tooltip
} from '@material-ui/core';
import { useEffect, useState } from 'react';
import { UpdateUniverseMetadata } from './UpdateUniverseMetadata';
import { RemoveUniverseMetadata } from './RemoveUniverseMetadata';
import clsx from 'clsx';

import InfoMessageIcon from '../../assets/info-message.svg';

export enum AppStatus {
  ACTIVE = 'Active',
  INACTIVE = 'Inactive'
}

interface UniverseListMetadataProps {
  metadata: MetadataFields[];
  universeList: any[];
  customerUuid: string;
  onActionPerformed: () => void;
}

const useStyles = makeStyles((theme) => ({
  overrideMuiTableContainer: {
    '& .MuiTableContainer-root': {
      width: '100% !important',
      marginTop: '3px'
    }
  },
  tagGreen: {
    backgroundColor: '#CDEFE1'
  },
  tagRed: {
    backgroundColor: '#FDE2E2'
  },
  tagTextGreen: {
    color: '#097245'
  },
  tagTextRed: {
    color: '#8F0000'
  },
  statusText: {
    fontFamily: 'Inter',
    fontStyle: 'normal',
    fontSize: '13px'
  },
  statusBox: {
    display: 'flex',
    flexDirection: 'row',
    alignItems: 'center',
    height: '24px',
    borderRadius: '6px',
    padding: '10px 6px',
    width: 'fit-content'
  },
  flexColumn: {
    display: 'flex',
    flexDirection: 'row'
  },
  tooltip: {
    marginLeft: theme.spacing(0.5)
  }
}));

export const UniverseListMetadata = ({
  metadata,
  customerUuid,
  universeList,
  onActionPerformed
}: UniverseListMetadataProps) => {
  const helperClasses = useStyles();
  const [showUpdateMetadataDialog, setShowUpdateMetadataDialog] = useState<boolean>(false);
  const [showDeleteMetadataDialog, setShowDeleteMetadataDialog] = useState<boolean>(false);
  const [selectedMetadata, setSelectedMetadata] = useState<MetadataFields | null>(null);
  const [metadataWithUniverseDetails, setMetadataWithUniverseDetails] = useState<any>(metadata);

  const onUpdateMetadataButtonClick = (data: MetadataFields) => {
    setSelectedMetadata(data);
    setShowUpdateMetadataDialog(true);
  };

  const onUpdateMetadataDialogClose = () => {
    setShowUpdateMetadataDialog(false);
  };

  const onDeleteMetadataDialogClose = () => {
    setShowDeleteMetadataDialog(false);
  };

  const onDeleteMetadataButtonClick = (data: MetadataFields) => {
    setSelectedMetadata(data);
    setShowDeleteMetadataDialog(true);
  };

  useEffect(() => {
    const mergeData = metadata.map((metadata: MetadataFields) => {
      const selectedUniverse = universeList.find(
        // TODO: Request Aleks to send customer UUID in universe_details API inside TS service
        (universe) => metadata.id === universe.universeUUID
      );
      metadata.name = selectedUniverse?.name ?? '';
      metadata.lastSyncError = selectedUniverse?.lastSyncError;
      return metadata;
    });
    setMetadataWithUniverseDetails(mergeData);
  }, [universeList, metadata]);

  return (
    <Box className={helperClasses.overrideMuiTableContainer}>
      <TableContainer component={Paper}>
        <Table aria-label="simple table">
          <TableHead>
            <TableRow>
              <TableCell align="center">{'Universe UUID'}</TableCell>
              <TableCell align="center">{'Universe Name'}</TableCell>
              <TableCell align="center">{'Customer UUID'}&nbsp;</TableCell>
              <TableCell align="center">{'Platform URL'}&nbsp;</TableCell>
              <TableCell align="center">{'Metrics URL'}&nbsp;</TableCell>
              <TableCell align="center">{'Metrics Scrape Period(secs)'}&nbsp;</TableCell>
              <TableCell align="center">{'Data Mount Points'}&nbsp;</TableCell>
              <TableCell align="center">{'Other Mount Points'}&nbsp;</TableCell>
              <TableCell align="center">{'Status'}&nbsp;</TableCell>
              <TableCell align="center">{'Actions'}&nbsp;</TableCell>
            </TableRow>
          </TableHead>
          <TableBody>
            {metadataWithUniverseDetails.map((data: MetadataFields) => (
              <TableRow key={data.id}>
                <TableCell align="center" component="th" scope="row">
                  {data.id}
                </TableCell>
                <TableCell align="center" component="th" scope="row">
                  <a href={`/universes/${data.id}/troubleshoot`}>{data.name}</a>
                </TableCell>
                <TableCell align="center">{data.customerId}</TableCell>
                <TableCell align="center">
                  <a href={data.platformUrl} target="_blank" rel="noreferrer">
                    {data.platformUrl}
                  </a>
                </TableCell>
                <TableCell align="center">
                  <a href={data.metricsUrl} target="_blank" rel="noreferrer">
                    {data.metricsUrl}
                  </a>
                </TableCell>
                <TableCell align="center">{data.metricsScrapePeriodSec}</TableCell>
                <TableCell align="center">{data.dataMountPoints}</TableCell>
                <TableCell align="center">{data.otherMountPoints}</TableCell>
                <TableCell align="center">
                  {data.lastSyncError ? (
                    <Box className={helperClasses.flexColumn}>
                      <Box className={clsx(helperClasses.tagRed, helperClasses.statusBox)}>
                        <span className={clsx(helperClasses.tagTextRed, helperClasses.statusText)}>
                          {AppStatus.INACTIVE}
                        </span>
                      </Box>
                      <Tooltip
                        title={data.lastSyncError}
                        arrow
                        placement="top"
                        className={helperClasses.tooltip}
                      >
                        <img src={InfoMessageIcon} alt="info" />
                      </Tooltip>
                    </Box>
                  ) : (
                    <Box className={clsx(helperClasses.tagGreen, helperClasses.statusBox)}>
                      <span className={clsx(helperClasses.tagTextGreen, helperClasses.statusText)}>
                        {AppStatus.ACTIVE}
                      </span>
                    </Box>
                  )}
                </TableCell>
                <TableCell align="center">
                  <Box>
                    <YBButton size="medium" onClick={() => onUpdateMetadataButtonClick(data)}>
                      <span
                        style={{
                          color: '#303A78',
                          textDecoration: 'underline'
                        }}
                      >
                        {'Update'}
                      </span>
                    </YBButton>
                    <YBButton size="medium" onClick={() => onDeleteMetadataButtonClick(data)}>
                      <span
                        style={{
                          color: '#303A78',
                          textDecoration: 'underline'
                        }}
                      >
                        {'Delete'}
                      </span>
                    </YBButton>
                  </Box>
                </TableCell>
              </TableRow>
            ))}
          </TableBody>
        </Table>
      </TableContainer>
      {showUpdateMetadataDialog && selectedMetadata && (
        <UpdateUniverseMetadata
          onActionPerformed={onActionPerformed}
          open={showUpdateMetadataDialog}
          onClose={onUpdateMetadataDialogClose}
          data={selectedMetadata}
        />
      )}
      {showDeleteMetadataDialog && selectedMetadata && (
        <RemoveUniverseMetadata
          onActionPerformed={onActionPerformed}
          open={showDeleteMetadataDialog}
          onClose={onDeleteMetadataDialogClose}
          data={selectedMetadata}
        />
      )}
    </Box>
  );
};
