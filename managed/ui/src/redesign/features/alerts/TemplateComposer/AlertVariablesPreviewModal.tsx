/*
 * Created on Fri Mar 17 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { FC, useCallback, useRef } from 'react';
import clsx from 'clsx';
import { useTranslation } from 'react-i18next';
import { useQuery } from 'react-query';
import { Descendant, Editor, Element, Path, Transforms } from 'slate';
import { Box, makeStyles, MenuItem, Typography } from '@material-ui/core';
import { YBLoadingCircleIcon } from '../../../../components/common/indicators';
import { YBModal, YBSelect } from '../../../components';
import { YBEditor } from '../../../components/YBEditor';
import { ALERT_VARIABLE_ELEMENT_TYPE, IYBEditor } from '../../../components/YBEditor/plugins';
import { AlertVariableElement } from '../../../components/YBEditor/plugins';
import { ALERT_TEMPLATES_QUERY_KEY, fetchAlertConfigList } from './CustomVariablesAPI';
import { useCommonStyles } from './CommonStyles';

type AlertVariablesPreviewModalProps = {
  visible: boolean;
  bodyValue: Descendant[];
  subjectValue: Descendant[];
  onHide: Function;
};

const useStyles = makeStyles((theme) => ({
  editorRoot: {
    background: 'rgba(217, 217, 217, 0.16)'
  },
  select: {
    width: '488px',
    marginTop: theme.spacing(0.8)
  },
  noPadding: {
    padding: 0
  },
  defaultPadding: {
    padding: `${theme.spacing(2.5)}px ${theme.spacing(3)}px`
  },
  fullHeight: {
    height: '100%'
  },
  subjectEditor: {
    display: 'flex',
    background: theme.palette.common.white,
    alignItems: 'center',
    marginTop: theme.spacing(0.8),
    marginBottom: theme.spacing(3)
  },
  bodyEditor: {
    marginTop: theme.spacing(0.8)
  }
}));

/**
 * preview editor contents with the variable values filled.
 * values are filled from alert configs
 */
const AlertVariablesPreviewModal: FC<AlertVariablesPreviewModalProps> = ({
  bodyValue,
  subjectValue,
  visible,
  onHide
}) => {
  const subjectEditorRef = useRef<IYBEditor | null>(null);
  const bodyEditorRef = useRef<IYBEditor | null>(null);

  const { t } = useTranslation();
  const classes = useStyles();
  const commonStyles = useCommonStyles();
  const alertConfigurationsMap = {};

  const { data, isLoading } = useQuery(ALERT_TEMPLATES_QUERY_KEY.fetchAlertConfigurationList, () =>
    fetchAlertConfigList({
      uuids: []
    })
  );

  /**
   * traverse through all the nodes and fill the alert variable with the value from alert configs
   * if the value is found in alert config, fill the value and mark the view as 'PREVIEW'
   * if the value is not found, mark the variable as 'NO_VALUE
   */
  const transformNodes = useCallback(
    (editor: IYBEditor | null, alertConfig: string) => {
      if (editor) {
        const alertElements = Editor.nodes(editor, {
          at: [],
          match: (node) => Element.isElement(node) && node.type === ALERT_VARIABLE_ELEMENT_TYPE //get only ALERT_VARIABLE_ELEMENT
        });

        let next = null;

        while (!(next = alertElements.next()).done) {
          const [node, path] = next.value as [AlertVariableElement, Path];

          const labels = alertConfigurationsMap[alertConfig]; //get values from alertConfigs

          if (labels && labels[node.variableName]) {
            Transforms.setNodes(
              editor,
              { view: 'PREVIEW', variableValue: labels[node.variableName] },
              { at: path }
            );
          } else {
            Transforms.setNodes(editor, { view: 'NO_VALUE' }, { at: path });
          }
        }
      }
    },
    [alertConfigurationsMap]
  );

  if (!visible) return null;

  if (isLoading || data?.data === undefined) {
    return <YBLoadingCircleIcon />;
  }

  const alertConfigurations = data.data;

  alertConfigurations.forEach((alertConfig) => {
    alertConfigurationsMap[alertConfig.name] = alertConfig.labels ?? {};
  });

  return (
    <YBModal
      open={visible}
      title={t('alertCustomTemplates.alertVariablesPreviewModal.modalTitle')}
      dialogContentProps={{ className: classes.noPadding }}
      onClose={() => onHide()}
      overrideWidth="740px"
      overrideHeight="540px"
    >
      <Box className={classes.defaultPadding}>
        <Typography variant="body2">
          {t('alertCustomTemplates.alertVariablesPreviewModal.info')}
        </Typography>
        <YBSelect
          className={classes.select}
          onChange={(e) => {
            transformNodes(subjectEditorRef.current, e.target.value);
            transformNodes(bodyEditorRef.current, e.target.value);
          }}
          renderValue={(selectedAlertConfig) => {
            if (!selectedAlertConfig) {
              return (
                <em>{t('alertCustomTemplates.alertVariablesPreviewModal.selectPlaceholder')}</em>
              );
            }
            return selectedAlertConfig;
          }}
        >
          <MenuItem disabled value="">
            <em>{t('alertCustomTemplates.alertVariablesPreviewModal.selectPlaceholder')}</em>
          </MenuItem>
          {alertConfigurations.map((alertConfig) => (
            <MenuItem key={alertConfig.name} value={alertConfig.name}>
              {alertConfig.name}
            </MenuItem>
          ))}
        </YBSelect>
      </Box>
      <Box className={clsx(classes.editorRoot, classes.defaultPadding, classes.fullHeight)}>
        <Typography variant="body1">{t('alertCustomTemplates.composer.subject')}</Typography>
        <Box
          className={clsx(
            classes.subjectEditor,
            commonStyles.editorBorder,
            commonStyles.subjectEditor
          )}
        >
          <YBEditor
            editorProps={{ readOnly: true }}
            loadPlugins={{ alertVariablesPlugin: true, singleLine: true }}
            initialValue={subjectValue}
            ref={subjectEditorRef}
          />
        </Box>
        <Typography variant="body1">{t('alertCustomTemplates.composer.content')}</Typography>
        <Box className={clsx(commonStyles.editorBorder, classes.bodyEditor)}>
          <YBEditor
            editorProps={{ readOnly: true, style: { height: '150px' } }}
            loadPlugins={{ alertVariablesPlugin: true }}
            initialValue={bodyValue}
            ref={bodyEditorRef}
          />
        </Box>
      </Box>
    </YBModal>
  );
};

export default AlertVariablesPreviewModal;
