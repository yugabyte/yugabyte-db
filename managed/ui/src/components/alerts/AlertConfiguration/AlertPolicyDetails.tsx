/*
 * Created on Thu Mar 30 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useCallback } from 'react';
import {
  Accordion,
  AccordionDetails,
  AccordionSummary,
  Box,
  makeStyles,
  Typography,
  Grid,
  MenuItem
} from '@material-ui/core';
import { Trans, useTranslation } from 'react-i18next';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { toast } from 'react-toastify';
import { isEmpty } from 'lodash';

import { IAlertConfiguration } from '../../../redesign/features/alerts/TemplateComposer/ICustomVariables';

import { YBSelect } from '../../../redesign/components';
import {
  ALERT_TEMPLATES_QUERY_KEY,
  fetchAlertConfigList,
  fetchAlertTemplateVariables,
  setVariableValueForAlertconfig
} from '../../../redesign/features/alerts/TemplateComposer/CustomVariablesAPI';
import { createErrorMessage } from '../../../utils/ObjectUtils';
import { ArrowRight } from '@material-ui/icons';
import { YBLoadingCircleIcon } from '../../common/indicators';

type AlertPolicyDetailsProps = {
  currentMetric: IAlertConfiguration;
};

const useStyles = makeStyles((theme) => ({
  root: {
    '& >.MuiAccordion-root': {
      background: 'rgba(229, 229, 233, 0.2)'
    }
  },
  expandIcon: {
    '&.Mui-expanded': {
      transform: 'rotate(90deg)'
    }
  },
  accordionSummary: {
    flexDirection: 'row-reverse',
    padding: 0,
    '& .MuiSvgIcon-root': {
      width: theme.spacing(3),
      height: theme.spacing(3)
    }
  },
  accordionDetails: {
    margin: `0 ${theme.spacing(2.5)}px`,
    display: 'block'
  },
  emptyVariableList: {
    textAlign: 'center',
    display: 'flex',
    alignItems: 'center',
    background: 'rgba(229, 229, 233, 0.2)',
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    height: '120px',
    flexDirection: 'column',
    justifyContent: 'center',
    color: theme.palette.ybacolors.labelBackground,
    padding: theme.spacing(2.5)
  },
  subText: {
    color: '#67666C',
    marginTop: theme.spacing(1)
  },
  variableContainer: {
    marginTop: theme.spacing(2.5)
  },
  variableValueSelect: {
    width: '530px',
    marginTop: theme.spacing(0.8),
    '& .MuiSvgIcon-root': {
      width: '2em',
      height: '2em'
    }
  },
  customVariable: {
    background: theme.palette.primary[100],
    color: theme.palette.primary[600],
    padding: theme.spacing(0.5),
    borderRadius: theme.spacing(0.5)
  }
}));

const EmptyVariableList = () => {
  const classes = useStyles();
  const { t } = useTranslation();
  return (
    <Box className={classes.emptyVariableList}>
      <div>
        <Typography variant="body1"> {t('alertPolicyDetails.emptyVariableList.title')}</Typography>
      </div>
      <div>
        <Typography variant="body2" className={classes.subText}>
          <Trans
            i18nKey="alertPolicyDetails.emptyVariableList.subText"
            components={{ bold: <b /> }}
          />
        </Typography>
      </div>
    </Box>
  );
};

const AlertPolicyDetails: FC<AlertPolicyDetailsProps> = ({ currentMetric }) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const alertConfigurationsMap = {};
  const queryClient = useQueryClient();

  const { data: configs, isLoading } = useQuery(
    ALERT_TEMPLATES_QUERY_KEY.fetchAlertConfigurationList,
    () =>
      fetchAlertConfigList({
        uuids: []
      })
  );

  const { data: alertVariables, isLoading: customVariablesLoading } = useQuery(
    ALERT_TEMPLATES_QUERY_KEY.fetchAlertTemplateVariables,
    fetchAlertTemplateVariables
  );

  const setVariableValue = useMutation(
    (alertConfig: IAlertConfiguration) => setVariableValueForAlertconfig(alertConfig),
    {
      onSuccess: (_resp, config) => {
        queryClient.invalidateQueries(ALERT_TEMPLATES_QUERY_KEY.fetchAlertConfigurationList);
        toast.success(
          t('alertPolicyDetails.variableValUpdateSuccess', {
            policy_name: config.name
          })
        );
      },
      onError: (error) => {
        toast.error(createErrorMessage(error));
      }
    }
  );

  const variableCard = useCallback(
    (label, defaultValue) => {
      if (!alertVariables || !alertVariables.data) {
        return null;
      }

      const variable = alertVariables.data.customVariables.find((cv) => cv.name === label);

      return (
        <Grid item key={label} className={classes.variableContainer}>
          <div>
            {t('alertPolicyDetails.variableName')}{' '}
            <Typography variant="body1" component="span">
              {label}
            </Typography>
          </div>
          <YBSelect
            className={classes.variableValueSelect}
            defaultValue={defaultValue}
            onChange={(e) => {
              const alertConfigtoUpdate = configs?.data.find((c) => c.name === currentMetric.name);
              if (alertConfigtoUpdate !== undefined) {
                setVariableValue.mutate({
                  ...alertConfigtoUpdate,
                  labels: {
                    ...alertConfigtoUpdate.labels,
                    [label]: e.target.value
                  }
                });
              }
            }}
            renderValue={(selectedValue) => {
              return (
                <div className={classes.customVariable}>
                  {selectedValue}
                  {selectedValue === variable?.defaultValue &&
                    t(
                      'alertCustomTemplates.customVariables.assignCustomVariableValueModal.menuDefault'
                    )}
                </div>
              );
            }}
          >
            {variable?.possibleValues.map(function menuItem(v) {
              return (
                <MenuItem key={v} value={v}>
                  {v}
                  {v === variable.defaultValue &&
                    t(
                      'alertCustomTemplates.customVariables.assignCustomVariableValueModal.menuDefault'
                    )}
                </MenuItem>
              );
            })}
          </YBSelect>
        </Grid>
      );
    },
    [alertVariables, t, classes, configs, currentMetric, setVariableValue]
  );

  const getVariablesList = useCallback(() => {
    if (isEmpty(alertConfigurationsMap[currentMetric.name])) {
      return <EmptyVariableList />;
    }
    return (
      <>
        <Grid container>
          <Typography variant="body2">{t('alertPolicyDetails.variablesList.helpText')}</Typography>
        </Grid>
        <Grid container>
          {Object.keys(alertConfigurationsMap[currentMetric.name]).map((label) =>
            variableCard(label, alertConfigurationsMap[currentMetric.name][label])
          )}
        </Grid>
      </>
    );
  }, [currentMetric, alertConfigurationsMap, t, variableCard]);

  if (
    isLoading ||
    customVariablesLoading ||
    alertVariables?.data === undefined ||
    configs?.data === undefined
  ) {
    return <YBLoadingCircleIcon />;
  }

  const alertConfigurations = configs.data;

  alertConfigurations.forEach((alertConfig) => {
    alertConfigurationsMap[alertConfig.name] = alertConfig.labels ?? {};
  });

  return (
    <div className={classes.root}>
      <Accordion>
        <AccordionSummary
          expandIcon={<ArrowRight />}
          className={classes.accordionSummary}
          classes={{
            expandIcon: classes.expandIcon
          }}
        >
          <Typography variant="body2">{t('alertPolicyDetails.variableTitle')}</Typography>
        </AccordionSummary>
        <AccordionDetails className={classes.accordionDetails}>
          {getVariablesList()}
        </AccordionDetails>
      </Accordion>
    </div>
  );
};

export default AlertPolicyDetails;
