/*
 * Created on Tue Feb 14 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { FC, useState } from 'react';
import {
  Grid,
  IconButton,
  makeStyles,
  Menu,
  MenuItem,
  Tooltip,
  Typography
} from '@material-ui/core';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { Trans, useTranslation } from 'react-i18next';
import { Add, Delete, Edit, Info, MoreHoriz } from '@material-ui/icons';
import { toast } from 'react-toastify';
import clsx from 'clsx';
import {
  ALERT_TEMPLATES_QUERY_KEY,
  deleteCustomAlertTemplateVariable,
  fetchAlertTemplateVariables
} from './CustomVariablesAPI';
import { YBButton } from '../../../components';
import { CustomVariableEditorModal } from './CustomVariableEditorModal';
import { useCommonStyles } from './CommonStyles';
import { YBLoadingCircleIcon } from '../../../../components/common/indicators';
import { CustomVariable } from './ICustomVariables';
import { createErrorMessage } from '../../../../utils/ObjectUtils';
import AssignCustomVariableValueModal from './AssignCustomVariableValueModal';
import { RbacValidator } from '../../rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../rbac/ApiAndUserPermMapping';

type CustomVariablesEditorProps = {};

const useStyles = makeStyles((theme) => ({
  root: {
    padding: theme.spacing(2.5)
  },
  variableCards: {
    borderRadius: theme.spacing(1),
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    background: theme.palette.common.white,
    padding: theme.spacing(2),
    width: '360px',
    marginBottom: theme.spacing(2.5),
    maxHeight: '300px',
    overflowY: 'auto'
  },
  emptyCustomVariablesCard: {
    display: 'flex',
    alignItems: 'center',
    padding: theme.spacing(3.6),
    flexDirection: 'column',
    gap: theme.spacing(2)
  },
  emptyCustomVariablesHelpText: {
    textAlign: 'center'
  },
  systemVariablesHeader: {
    fontWeight: 500,
    fontSize: '14px',
    color: theme.palette.common.black,
    marginBottom: theme.spacing(3.1)
  },
  systemVariableItem: {
    margin: 0,
    marginBottom: theme.spacing(2.1),
    cursor: 'pointer',
    '& svg': {
      marginLeft: theme.spacing(1),
      color: '#23232966'
    }
  },
  customVariablesActions: {
    display: 'flex',
    justifyContent: 'flex-end',
    gap: theme.spacing(1.2),
    marginBottom: theme.spacing(2.1)
  },
  customVariableCard: {
    padding: theme.spacing(1.4),
    marginBottom: theme.spacing(2.1),
    borderRadius: theme.spacing(1),
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'space-between',
    width: '100%'
  },
  moreOptionsButton: {
    width: '34px !important',
    height: '30px',
    border: '1px solid #C8C8C8',
    borderRadius: theme.spacing(1)
  },
  createCustomVariableBut: {
    height: '30px',
    "&>span.MuiButton-label": {
      fontWeight: 500,
      fontSize: '12px',
      color: theme.palette.ybacolors.ybDarkGray
    }
  }
}));

const CustomVariablesEditor = (props: CustomVariablesEditorProps) => {
  const classes = useStyles(props);
  const commonStyles = useCommonStyles();
  const { t } = useTranslation();
  const [showCreateCustomVariableModal, setShowCreateCustomVariableModal] = useState(false);
  const [showAssignVariableValueModal, setShowAssignVariableValueModal] = useState(false);
  const [editCustomVariable, setEditCustomVariable] = useState<CustomVariable>();
  const queryClient = useQueryClient();

  const { data: alertVariables, isLoading } = useQuery(
    ALERT_TEMPLATES_QUERY_KEY.fetchAlertTemplateVariables,
    fetchAlertTemplateVariables
  );

  const doDeleteVariable = useMutation(
    (variable: CustomVariable) => {
      return deleteCustomAlertTemplateVariable(variable);
    },
    {
      onSuccess: (_resp, variable) => {
        toast.success(
          <Trans
            i18nKey={`alertCustomTemplates.customVariables.deleteSuccess`}
            values={{ variable_name: variable.name }}
            components={{ u: <u /> }}
          />
        );
        queryClient.invalidateQueries(ALERT_TEMPLATES_QUERY_KEY.fetchAlertTemplateVariables);
      },
      onError(error) {
        toast.error(createErrorMessage(error));
      }
    }
  );

  if (isLoading) {
    return <YBLoadingCircleIcon />;
  }

  const createCustomVariablesButton = (
    <RbacValidator
      accessRequiredOn={ApiPermissionMap.MODIFY_ALERT_TEMPLATE_VARIABLE}
      isControl
    >
      <YBButton
        variant="secondary"
        startIcon={<Add />}
        onClick={() => setShowCreateCustomVariableModal(true)}
        className={classes.createCustomVariableBut}
        data-testid="create_custom_variable"
      >
        {t('alertCustomTemplates.customVariables.createNewVariableButton')}
      </YBButton>
    </RbacValidator>
  );

  const emptyCustomVariables = (
    <div className={classes.emptyCustomVariablesCard}>
      <div className={clsx(commonStyles.helpText, classes.emptyCustomVariablesHelpText)}>
        <Trans
          i18nKey="alertCustomTemplates.customVariables.addNewVariableText"
          components={{ bold: <b /> }}
        />
      </div>
      {createCustomVariablesButton}
    </div>
  );

  const customVariableCard = (variable: CustomVariable, index: number) => (
    <Grid item key={index} className={clsx(commonStyles.defaultBorder, classes.customVariableCard)}>
      <Grid item>
        <Typography variant="body2">
          {t('alertCustomTemplates.customVariables.variableText')}: {variable.name}
        </Typography>
      </Grid>
      <Grid item>
        <MoreOptionsMenu
          menuOptions={[
            {
              icon: <Edit />,
              text: t(
                'alertCustomTemplates.customVariables.customVariableContextMenu.editVariable'
              ),
              callback: () => {
                setEditCustomVariable(variable);
                setShowCreateCustomVariableModal(true);
              }
            },
            // {
            //   icon: <Info />,
            //   text: t(
            //     'alertCustomTemplates.customVariables.customVariableContextMenu.variableDetails'
            //   ),
            //   callback: () => { }
            // },
            {
              icon: <Delete />,
              text: t(
                'alertCustomTemplates.customVariables.customVariableContextMenu.deleteVariable'
              ),
              callback: () => {
                doDeleteVariable.mutate(variable);
              }
            }
          ]}
        >
          <MoreHoriz className={commonStyles.clickable} data-testid={`alert-variable-${variable.name}`} />
        </MoreOptionsMenu>
      </Grid>
    </Grid>
  );

  const customVariablesList = (
    <>
      <Grid container className={classes.customVariablesActions}>
        <Grid item>{createCustomVariablesButton}</Grid>
        <Grid item>
          <MoreOptionsMenu
            menuOptions={[
              {
                text: t('alertCustomTemplates.customVariables.assignVariablesButton'),
                callback: () => {
                  setShowAssignVariableValueModal(true);
                }
              }
            ]}
          >
            <IconButton className={classes.moreOptionsButton} data-testid="assign-variable-button">
              <MoreHoriz />
            </IconButton>
          </MoreOptionsMenu>
        </Grid>
      </Grid>
      <Grid container>
        {alertVariables?.data.customVariables.map((v, i) => customVariableCard(v, i))}
      </Grid>
    </>
  );

  return (
    <div className={classes.root}>
      <Grid container className={classes.variableCards}>
        {alertVariables && alertVariables?.data.customVariables.length > 0
          ? customVariablesList
          : emptyCustomVariables}
      </Grid>
      <Grid container className={classes.variableCards}>
        <Typography component="span" variant="body2" className={classes.systemVariablesHeader}>
          {t('alertCustomTemplates.customVariables.systemVariables')}
        </Typography>
        {alertVariables?.data.systemVariables.map(function systemVariableDom(s) {
          return (
            <Grid
              key={s.name}
              item
              className={classes.systemVariableItem}
              alignItems="center"
              container
            >
              {s.name}
              <Tooltip title={s.description}>
                <Info />
              </Tooltip>
            </Grid>
          );
        })}
      </Grid>
      <CustomVariableEditorModal
        open={showCreateCustomVariableModal}
        onHide={() => {
          setEditCustomVariable(undefined);
          setShowCreateCustomVariableModal(false);
        }}
        editValues={editCustomVariable}
      />
      <AssignCustomVariableValueModal
        visible={showAssignVariableValueModal}
        onHide={() => setShowAssignVariableValueModal(false)}
      />
    </div>
  );
};

type MoreOptionsProps = {
  children: React.ReactElement;
  menuOptions: { icon?: React.ReactNode; text: string; callback: Function }[];
};

const MoreOptionsMenu: FC<MoreOptionsProps> = ({ children, menuOptions }) => {
  const [anchorEl, setAnchorEl] = React.useState<null | HTMLElement>(null);
  const open = Boolean(anchorEl);
  const handleClick = (event: React.MouseEvent<HTMLButtonElement>) => {
    setAnchorEl(event.currentTarget);
  };
  const handleClose = () => {
    setAnchorEl(null);
  };
  const reactChild = React.cloneElement(children, {
    onClick: (e: any) => {
      handleClick(e);
      children.props.onClick?.(e);
    }
  });
  const classes = useCommonStyles();
  return (
    <>
      {reactChild}
      <Menu
        id="custom_alert_more_options_menu"
        anchorEl={anchorEl}
        open={open}
        onClose={handleClose}
        getContentAnchorEl={null}
        className={classes.menuStyles}
        anchorOrigin={{
          vertical: 'bottom',
          horizontal: 'right'
        }}
        transformOrigin={{
          vertical: 'top',
          horizontal: 'right'
        }}
      >
        {menuOptions.map((m) => (
          <MenuItem
            key={m.text}
            data-testid={`variable-${m.text}`}
            onClick={() => {
              handleClose();
              m.callback();
            }}
          >
            {m.icon && m.icon}
            {m.text}
          </MenuItem>
        ))}
      </Menu>
    </>
  );
};

export default CustomVariablesEditor;
