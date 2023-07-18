/*
 * Created on Fri Mar 17 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC } from 'react';
import { Box, Divider, Grid, Grow, makeStyles, Typography } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { useQuery } from 'react-query';
import { YBLoadingCircleIcon } from '../../../../components/common/indicators';
import { ALERT_TEMPLATES_QUERY_KEY, fetchAlertTemplateVariables } from './CustomVariablesAPI';
import { CustomVariable, SystemVariables } from './ICustomVariables';

interface AddAlertVariablesPopupProps {
  show: boolean;
  onCustomVariableSelect: (variable: CustomVariable) => void;
  onSystemVariableSelect: (variable: SystemVariables) => void;
}

const alertStyles = makeStyles((theme) => ({
  root: {
    boxShadow: '0px 3px 15px rgba(102, 102, 102, 0.15)',
    borderRadius: theme.spacing(0.5),
    background: theme.palette.common.white,
    padding: theme.spacing(1.9),
    width: '260px',
    overflowY: 'auto',
    maxHeight: '380px'
  },
  variable: {
    marginBottom: theme.spacing(3),
    color: theme.palette.ybacolors.labelBackground,
    cursor: 'pointer',
    userSelect: 'none'
  },
  header: {
    marginBottom: theme.spacing(3)
  },
  systemVariableHeader: {
    marginTop: theme.spacing(2.5)
  }
}));

export const AddAlertVariablesPopup: FC<AddAlertVariablesPopupProps> = ({
  show,
  onCustomVariableSelect,
  onSystemVariableSelect
}) => {
  const classes = alertStyles();

  const { t } = useTranslation();

  const { data: alertVariables, isLoading } = useQuery(
    ALERT_TEMPLATES_QUERY_KEY.fetchAlertTemplateVariables,
    fetchAlertTemplateVariables,
    {
      enabled: show,
      refetchOnMount: false //we don't need to fetch it everytime when the popover is opened
    }
  );

  if (!show) return null;

  return isLoading ? (
    <YBLoadingCircleIcon />
  ) : (
    <Grow in={show}>
      <Box className={classes.root}>
        <Grid>
          <Typography variant="subtitle2" className={classes.header}>
            {t('alertCustomTemplates.customVariables.customVariables')}
          </Typography>
          {alertVariables?.data.customVariables.map((variable) => (
            <Grid
              item
              key={variable.uuid}
              className={classes.variable}
              onClick={() => {
                onCustomVariableSelect(variable);
              }}
              data-testid={`alert-custom-variable-${variable.name}`}
            >
              <Typography variant="body2">{variable.name}</Typography>
            </Grid>
          ))}
        </Grid>
        <Divider />
        <Grid className={classes.systemVariableHeader}>
          <Typography variant="subtitle2" className={classes.header}>
            {t('alertCustomTemplates.customVariables.systemVariables')}
          </Typography>
          {alertVariables?.data.systemVariables.map((variable) => (
            <Grid
              item
              key={variable.name}
              className={classes.variable}
              data-testid={`alert-system-variable-${variable.name}`}
              onClick={() => {
                onSystemVariableSelect(variable);
              }}
            >
              <Typography variant="body2">{variable.name}</Typography>
            </Grid>
          ))}
        </Grid>
      </Box>
    </Grow>
  );
};
