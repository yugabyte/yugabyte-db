import React, { FC, ReactNode } from 'react';
import { Box, makeStyles, Theme } from '@material-ui/core';
import { Namespace, TFunction, useTranslation } from 'react-i18next';
import { colors } from '@app/theme/variables';
import { YBTooltip } from '@app/components';
import CheckIcon from '@app/assets/check.svg';
import FailedIcon from '@app/assets/failed-solid.svg';
// import WarningIcon from '@app/assets/alert-solid.svg';
// import LoadingIcon from '@app/assets/Default-Loading-Circles.svg';
// import ClockIcon from '@app/assets/clock.svg';

export enum StatusEntity {
  Tserver,
  Master
}

export enum StateEnum {
    Succeeded = 'SUCCEEDED',
    Failed = 'FAILED'
}

interface YBSmartStatusProps {
  entity: StatusEntity;
  status: string;
}

enum Category {
  Success,
  InProgress,
  Pending,
  Warning,
  Failed,
  Info,
  Disabled
}

interface StatusDetails {
  category: Category;
  label: string;
  icon: ReactNode;
  tooltip?: ReactNode;
  dataTestId?: string;
}

const successIcon = (
  <Box display="flex" mr={-0.75}>
    <CheckIcon color={colors.success[700]} />
  </Box>
);
// const progressIcon = (
//   <Box ml={0.75}>
//     <LoadingIcon width={16} height={16} />
//   </Box>
// );
// const pendingIcon = (
//   <Box display="flex" ml={0.75}>
//     <ClockIcon />
//   </Box>
// );
// const warningIcon = (
//   <Box display="flex" ml={0.5} color={colors.warning[100]}>
//     <WarningIcon />
//   </Box>
// );
const failedIcon = (
  <Box display="flex" ml={0.5}>
    <FailedIcon width={16} height={16} />
  </Box>
);

const getStatusDetails =
    (entity: StatusEntity, status: string, t: TFunction<Namespace<'en'>>): StatusDetails =>
{
  const mapping: Record<StatusEntity, Record<string, StatusDetails>> = {
    [StatusEntity.Tserver]: {
      [StateEnum.Succeeded]: {
        category: Category.Success,
        label: t('clusterDetail.nodes.tserver'),
        icon: successIcon
      },
      [StateEnum.Failed]: {
        category: Category.Failed,
        label: t('clusterDetail.nodes.tserver'),
        icon: failedIcon
      }
    },
    [StatusEntity.Master]: {
      [StateEnum.Succeeded]: {
        category: Category.Success,
        label: t('clusterDetail.nodes.master'),
        icon: successIcon
      },
      [StateEnum.Failed]: {
        category: Category.Failed,
        label: t('clusterDetail.nodes.master'),
        icon: failedIcon
      }
    }
  };

  const defaultResult: StatusDetails = {
    category: Category.Info,
    label: status,
    icon: null
  };

  return mapping?.[entity]?.[status] ?? defaultResult;
};

const useStyles = makeStyles<Theme, StatusDetails>((theme) => ({
  root: {
    height: 24,
    borderRadius: theme.spacing(0.75),
    fontSize: 11.5,
    display: 'inline-flex',
    alignItems: 'center',
    padding: theme.spacing(0, 1),
    color: (statusDetails) => {
      switch (statusDetails.category) {
        case Category.Success:
          return theme.palette.success[700];
        case Category.InProgress:
          return theme.palette.primary[700];
        case Category.Pending:
          return theme.palette.warning[900];
        case Category.Warning:
          return theme.palette.warning[900];
        case Category.Failed:
          return theme.palette.error[700];
        case Category.Info:
          return theme.palette.secondary[600];
        case Category.Disabled:
          return theme.palette.grey[700];
        default:
          return theme.palette.text.primary;
      }
    },
    backgroundColor: (statusDetails) => {
      switch (statusDetails.category) {
        case Category.Success:
          return theme.palette.success[100];
        case Category.InProgress:
          return theme.palette.primary[300];
        case Category.Pending:
          return theme.palette.warning[100];
        case Category.Warning:
          return theme.palette.warning[100];
        case Category.Failed:
          return theme.palette.error[100];
        case Category.Info:
          return theme.palette.secondary[200];
        case Category.Disabled:
          return theme.palette.grey[200];
        default:
          return 'inherit';
      }
    }
  }
}));

export const YBSmartStatus: FC<YBSmartStatusProps> = ({ status, entity }) => {
  const { t } = useTranslation();
  const statusDetails = getStatusDetails(entity, status, t);
  const classes = useStyles(statusDetails);

  return (
    <YBTooltip title={statusDetails.tooltip ?? ''} interactive>
      <div className={classes.root} data-testid={statusDetails.dataTestId}>
        {statusDetails.label}
        {statusDetails.icon}
      </div>
    </YBTooltip>
  );
};
