/*
 * Created on Wed Apr 05 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { FC } from 'react';
import { makeStyles, Popover } from '@material-ui/core';
import { useTranslation } from 'react-i18next';
import { YBButton } from '../../../../components';
import { ALERT_VARIABLE_START_TAG, IYBEditor } from '../../../../components/YBEditor/plugins';
import { Add } from '@material-ui/icons';
import { AddAlertVariablesPopup } from '../AlertVariablesPopup';
import { AlertVariableType, CustomVariable, SystemVariables } from '../ICustomVariables';

export const useComposerStyles = makeStyles((theme) => ({
  subjectArea: {},
  editorArea: {
    marginTop: theme.spacing(0.8)
  },
  content: {
    marginTop: theme.spacing(2.5)
  },
  helpText: {
    marginTop: theme.spacing(1.75),
    marginBottom: theme.spacing(7),
    '& svg': {
      width: '14px',
      height: '14px',
      color: theme.palette.orange[500],
      marginRight: theme.spacing(1.2)
    }
  },
  insertVariableButton: {
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    borderRadius: theme.spacing(0.9),
    height: '30px',
    float: 'right',
    '& .MuiButton-label': {
      fontWeight: 500,
      fontSize: '13px',
      color: '#67666C'
    }
  },
  startTag: {
    width: '22px',
    height: '22px',
    border: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    borderRadius: theme.spacing(0.5),
    marginLeft: theme.spacing(1)
  },
  actions: {
    background: '#F5F4F0',
    padding: theme.spacing(2),
    boxShadow: `0px -1px 0px rgba(0, 0, 0, 0.1)`,
    '& button': {
      height: '40px'
    }
  },
  submitButton: {
    width: '65px !important',
    marginLeft: theme.spacing(1.9)
  },
  toolbarRoot: {
    height: theme.spacing(5.25),
    display: 'flex',
    justifyContent: 'space-between',
    background: '#FAFBFC',
    borderBottom: `1px solid ${theme.palette.ybacolors.ybBorderGray}`,
    padding: `0 ${theme.spacing(2)}px`
  },
  formatIcons: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(0.5),
    '& > svg': {
      width: theme.spacing(3),
      height: theme.spacing(1.75),
      opacity: 0.5,
      cursor: 'pointer',
      '&.big': {
        height: theme.spacing(3)
      },
      '&.medium': {
        height: theme.spacing(2.25)
      },
      '&.active': {
        opacity: '1 !important'
      }
    }
  }
}));

type GetInsertVariableButtonProps = {
  onClick: () => void;
};

export const GetInsertVariableButton = React.forwardRef<
  HTMLAnchorElement,
  React.PropsWithChildren<GetInsertVariableButtonProps>
>(({ onClick }, forwardRef) => {
  const classes = useComposerStyles();
  const { t } = useTranslation();
  return (
    <YBButton
      onClick={onClick}
      innerRef={forwardRef}
      variant="secondary"
      className={classes.insertVariableButton}
      startIcon={<Add />}
      data-testid={'insert-alert-variable-button'}
    >
      {t('alertCustomTemplates.composer.insertVariableButton')}
      <span className={classes.startTag}>{ALERT_VARIABLE_START_TAG}</span>
    </YBButton>
  );
});

interface AlertPopoverProps {
  open: boolean;
  editor: IYBEditor;
  anchorEl: null | Element | ((element: Element) => Element);
  onVariableSelect: (variable: CustomVariable | SystemVariables, type: AlertVariableType) => void;
  handleClose: () => void;
}

export const AlertPopover: FC<AlertPopoverProps> = ({
  open,
  anchorEl,
  editor,
  handleClose,
  onVariableSelect
}) => {
  return (
    <Popover
      id={'alertVariablesPopup'}
      open={open}
      anchorEl={anchorEl}
      onClose={handleClose}
      anchorOrigin={{
        vertical: 'bottom',
        horizontal: 'right'
      }}
      transformOrigin={{
        vertical: 'top',
        horizontal: 'right'
      }}
    >
      <AddAlertVariablesPopup
        show={open}
        onCustomVariableSelect={(v) => {
          onVariableSelect(v, 'CUSTOM');
          handleClose();
        }}
        onSystemVariableSelect={(v) => {
          onVariableSelect(v, 'SYSTEM');
          handleClose();
        }}
      />
    </Popover>
  );
};
