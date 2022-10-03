import React, { FC, ReactNode, useState } from 'react';
import { Box, CircularProgress, makeStyles, Typography, MenuItem } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { GenericFailure, YBDropdown, YBModal } from '@app/components';
import RefreshIcon from '@app/assets/refresh.svg';
import Fullscreen from '@app/assets/fullscreen.svg';
import MoreIcon from '@app/assets/more-horizontal.svg';

const useStyles = makeStyles((theme) => ({
  root: {
    display: 'flex',
    flexDirection: 'column',
    height: theme.spacing(40),
    padding: theme.spacing(2, 3, 2, 2),
    border: `1px solid ${theme.palette.grey[200]}`,
    borderRadius: theme.shape.borderRadius,
    backgroundColor: theme.palette.background.paper
  },
  icon: {
    margin: theme.spacing(0, 1, 0, 0)
  },
  pointer: {
    cursor: 'pointer'
  }
}));

interface ChartContainerProps {
  title: ReactNode;
  isLoading: boolean;
  isError: boolean;
  onRefresh: () => void;
}

export const YBChartContainer: FC<ChartContainerProps> = ({ title, isLoading, isError, onRefresh, children }) => {
  const { t } = useTranslation();
  const classes = useStyles();
  const [showChartModal, setShowChartModal] = useState<boolean>(false);
  const refresh = () => {
    onRefresh();
  };

  const closeChartModal = () => {
    setShowChartModal(false);
  };

  return (
    <>
      <div className={classes.root}>
        <Box display="flex" justifyContent="space-between">
          <Typography variant="h5">{title}</Typography>
          <YBDropdown origin={<MoreIcon />} className={classes.pointer}>
            <MenuItem onClick={refresh}>
              <RefreshIcon className={classes.icon} />
              {t('clusterDetail.charts.refresh')}
            </MenuItem>
            <MenuItem
              onClick={() => {
                setShowChartModal(true);
              }}
            >
              <Fullscreen className={classes.icon} />
              {t('clusterDetail.charts.viewFullScreen')}
            </MenuItem>
          </YBDropdown>
        </Box>
        <Box height="100%" display="flex" justifyContent="center" alignItems="center" marginTop={2}>
          {isError && <GenericFailure />}
          {isLoading && <CircularProgress size={32} color="primary" />}
          {!isLoading && !isError && <>{children}</>}
        </Box>
      </div>
      <YBModal
        open={showChartModal}
        title={title as string}
        onClose={closeChartModal}
        enableBackdropDismiss
        titleSeparator
        fullScreen
      >
        {children}
      </YBModal>
    </>
  );
};
