/*
 * Created on Mon Feb 27 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { FC, useState } from 'react';
import clsx from 'clsx';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import {
  Box,
  Grid,
  makeStyles,
  Menu,
  MenuItem,
  Paper,
  TableBody,
  Typography
} from '@material-ui/core';
import { sortBy } from 'lodash';
import { useTranslation } from 'react-i18next';
import { toast } from 'react-toastify';
import { YBLoadingCircleIcon } from '../../../../components/common/indicators';
import { YBInput, YBModal } from '../../../components';
import { createErrorMessage } from '../../universe/universe-form/utils/helpers';
import {
  ALERT_TEMPLATES_QUERY_KEY,
  fetchAlertConfigList,
  fetchAlertTemplateVariables,
  setVariableValueForAlertconfig
} from './CustomVariablesAPI';
import { CustomVariable, IAlertConfiguration } from './ICustomVariables';
import { YBTable, YBTableCell, YBTableHead, YBTableRow } from './AssignVariablesTable';
import { KeyboardArrowDown, KeyboardArrowUp } from '@material-ui/icons';
import { useCommonStyles } from './CommonStyles';

type AssignCustomVariableValueModalProps = {
  visible: boolean;
  onHide: Function;
};

const useStyles = makeStyles((theme) => ({
  modalBody: {
    padding: theme.spacing(2.3, 3.2)
  },
  searchInput: {
    marginTop: theme.spacing(1.2),
    marginBottom: theme.spacing(2.2)
  },
  alertPoliciesName: {
    color: theme.palette.grey[900]
  },
  tableContainer: {
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    overflow: 'auto',
    maxHeight: '500px'
  },
  defaultSpan: {
    marginLeft: theme.spacing(0.4)
  },
  arrowIcon: {
    width: theme.spacing(2.5),
    height: theme.spacing(2.5)
  },
  variableName: {
    marginTop: theme.spacing(0.6),
    fontWeight: 700
  },
  searchHelpText: {
    fontWeight: 500
  }
}));

type AlertConfigValueMatrix = [IAlertConfiguration, CustomVariable[]][];

const AssignCustomVariableValueModal: FC<AssignCustomVariableValueModalProps> = ({
  visible,
  onHide
}) => {
  const { t } = useTranslation();
  const classes = useStyles();
  const commonStyles = useCommonStyles();
  const [searchText, setSearchText] = useState('');
  const queryClient = useQueryClient();
  const { data, isLoading } = useQuery(ALERT_TEMPLATES_QUERY_KEY.fetchAlertConfigurationList, () =>
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
      onSuccess: () => {
        queryClient.invalidateQueries(ALERT_TEMPLATES_QUERY_KEY.fetchAlertConfigurationList);
      },
      onError: (error) => {
        toast.error(createErrorMessage(error));
      }
    }
  );

  if (isLoading || customVariablesLoading || data === undefined || alertVariables === undefined)
    return <YBLoadingCircleIcon />;

  let alertConfigurations = data.data;

  if (searchText) {
    alertConfigurations = alertConfigurations.filter(
      (a) => a.name.toLowerCase().indexOf(searchText.toLowerCase()) > -1
    );
  }

  const customVariablesRow = alertVariables.data.customVariables;

  const alertConfigValueMatrix: AlertConfigValueMatrix = sortBy(
    alertConfigurations,
    (a) => a.name
  ).map((alertConfig) => {
    return [alertConfig, [...customVariablesRow]];
  });

  const generateTableHeader = () => {
    return (
      <YBTableHead>
        <YBTableRow>
          <YBTableCell>
            <Typography variant="body1">
              {t(
                'alertCustomTemplates.customVariables.assignCustomVariableValueModal.tablePolicyHeader'
              )}
            </Typography>
          </YBTableCell>
          {customVariablesRow.map((variable: CustomVariable) => {
            return (
              <YBTableCell key={variable.uuid}>
                <Typography variant="body1">
                  <div className={clsx(commonStyles.subText, commonStyles.upperCase)}>
                    {t('alertCustomTemplates.customVariables.variableText')}
                  </div>
                </Typography>
                <Typography variant="body2" className={classes.variableName}>
                  {variable.name}
                </Typography>
              </YBTableCell>
            );
          })}
        </YBTableRow>
      </YBTableHead>
    );
  };

  const generateTableBody = () => {
    return (
      <TableBody>
        {alertConfigValueMatrix.map(([alertConfig, customVariable]) => {
          return (
            <YBTableRow key={alertConfig.name}>
              <YBTableCell key={alertConfig.name}>
                <Typography variant="body2" className={classes.alertPoliciesName}>
                  {alertConfig.name}
                </Typography>
                <Typography variant="body2" className={commonStyles.subText}>
                  {alertConfig.description}
                </Typography>
              </YBTableCell>
              {customVariable.map((cv) => {
                return (
                  <YBTableCell className={commonStyles.clickable} key={cv.uuid}>
                    <CustomVariableHoverMenu
                      variable={cv}
                      labels={alertConfig.labels}
                      onSelect={(val: string) => {
                        const labels = alertConfig.labels ?? {};
                        labels[cv.name] = val;
                        setVariableValue.mutate({
                          ...alertConfig,
                          labels
                        });
                      }}
                    />
                  </YBTableCell>
                );
              })}
            </YBTableRow>
          );
        })}
      </TableBody>
    );
  };

  return (
    <YBModal
      open={visible}
      title={t('alertCustomTemplates.customVariables.assignCustomVariableValueModal.title')}
      size="xl"
      dialogContentProps={{
        dividers: true
      }}
      isSubmitting={setVariableValue.isLoading}
      submitLabel={setVariableValue.isLoading ? t('common.saving') : t('common.close')}
      onSubmit={() => onHide()}
      onClose={() => onHide()}
      enableBackdropDismiss
    >
      <Box>
        <Typography variant="body2" className={classes.searchHelpText}>
          {t('alertCustomTemplates.customVariables.assignCustomVariableValueModal.searchHelpText')}
        </Typography>
        <Grid item container xs={10} md={10} lg={10}>
          <YBInput
            onChange={(e) => setSearchText(e.target.value)}
            className={classes.searchInput}
            fullWidth
            placeholder={t(
              'alertCustomTemplates.customVariables.assignCustomVariableValueModal.searchInputPlaceHolder'
            )}
          />
        </Grid>
      </Box>
      <Box>
        <Paper className={classes.tableContainer}>
          <YBTable stickyHeader>
            {generateTableHeader()}
            {generateTableBody()}
          </YBTable>
        </Paper>
      </Box>
    </YBModal>
  );
};

export default AssignCustomVariableValueModal;

type CustomVariableHoverMenuProps = {
  variable: CustomVariable;
  onSelect: (val: string) => void;
  labels?: Record<string, string>;
};

const CustomVariableHoverMenu: FC<CustomVariableHoverMenuProps> = ({
  onSelect,
  variable,
  labels = {}
}) => {
  const [anchorEl, setAnchorEl] = React.useState<null | HTMLElement>(null);
  const open = Boolean(anchorEl);
  const handleClick = (event: React.MouseEvent<HTMLDivElement>) => {
    setAnchorEl(event.currentTarget);
  };
  const handleClose = () => {
    setAnchorEl(null);
  };
  const commonStyles = useCommonStyles();
  const classes = useStyles();
  const { t } = useTranslation();

  const defaultValueSpan = () => (
    <Typography className={clsx(commonStyles.subText, classes.defaultSpan)} component={'span'}>
      {t('alertCustomTemplates.customVariables.assignCustomVariableValueModal.menuDefault')}
    </Typography>
  );
  return (
    <>
      <Grid container onClick={handleClick} data-testid={`alert-Variable-${variable.name}`}>
        <Grid item>
          {labels[variable.name] ?? (
            <>
              {variable.defaultValue} {defaultValueSpan()}{' '}
            </>
          )}
        </Grid>
        <Grid item>{variable.defaultValue === labels[variable.name] && defaultValueSpan()}</Grid>
        <Grid item>
          {open ? (
            <KeyboardArrowUp className={classes.arrowIcon} />
          ) : (
            <KeyboardArrowDown className={classes.arrowIcon} />
          )}
        </Grid>
      </Grid>
      <Menu
        id="custom_alert_more_options_menu"
        anchorEl={anchorEl}
        open={open}
        onClose={handleClose}
        getContentAnchorEl={null}
        className={clsx(commonStyles.menuStyles, commonStyles.menuNoBorder)}
        anchorOrigin={{
          vertical: 'bottom',
          horizontal: 'left',
        }}
        transformOrigin={{
          vertical: 'top',
          horizontal: 'left',
        }}
      >
        {variable.possibleValues.map((v) => (
          <MenuItem
            key={v}
            onClick={() => {
              handleClose();
              onSelect(v);
            }}
            data-testid={`menu-${v}`}
          >
            {v}
            {v === variable.defaultValue && defaultValueSpan()}
          </MenuItem>
        ))}
      </Menu>
    </>
  );
};
