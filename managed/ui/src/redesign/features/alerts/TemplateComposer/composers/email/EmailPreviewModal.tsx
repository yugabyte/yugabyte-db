/*
 * Created on Fri Mar 17 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import { FC, useRef } from 'react';
import clsx from 'clsx';
import { useTranslation } from 'react-i18next';
import { useMutation, useQuery } from 'react-query';
import { Descendant } from 'slate';
import { Box, makeStyles, MenuItem, Typography } from '@material-ui/core';
import { toast } from 'react-toastify';
import { YBLoadingCircleIcon } from '../../../../../../components/common/indicators';
import { YBModal, YBSelect } from '../../../../../components';
import { YBEditor } from '../../../../../components/YBEditor';
import { IYBEditor } from '../../../../../components/YBEditor/plugins';
import {
  ALERT_TEMPLATES_QUERY_KEY,
  fetchAlertConfigList,
  previewAlertNotification
} from '../../CustomVariablesAPI';
import { useCommonStyles } from '../../CommonStyles';
import { HTMLSerializer } from '../../../../../components/YBEditor/serializers';
import { createErrorMessage } from '../../../../universe/universe-form/utils/helpers';
import { fillAlertVariablesWithValue } from '../ComposerUtils';
import { convertNodesToText } from '../../../../../components/YBEditor/serializers/Text/TextSerializer';

type EmailPreviewModalProps = {
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
 * values are filled from the api
 */
const EmailPreviewModal: FC<EmailPreviewModalProps> = ({
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

  const fillTemplateWithValue = useMutation(
    ({
      subjectAsText,
      bodyAsText,
      subjectAsHTML,
      bodyAsHTML,
      alertConfigUUID
    }: {
      subjectAsText: string;
      bodyAsText: string;
      subjectAsHTML: string;
      bodyAsHTML: string;
      alertConfigUUID: string;
    }) =>
      previewAlertNotification(
        {
          type: 'Email',
          textTemplate: bodyAsText,
          titleTemplate: subjectAsText,
          highlightedTextTemplate: bodyAsHTML,
          highlightedTitleTemplate: subjectAsHTML
        },
        alertConfigUUID
      ),
    {
      onSuccess(data) {
        fillAlertVariablesWithValue(bodyEditorRef.current!, data.data.highlightedText);
        fillAlertVariablesWithValue(subjectEditorRef.current!, data.data.highlightedTitle);
      },
      onError(err) {
        toast.error(createErrorMessage(err));
      }
    }
  );

  if (!visible) return null;

  if (isLoading || data?.data === undefined) {
    return <YBLoadingCircleIcon />;
  }

  const alertConfigurations = data.data.sort((a, b) => a.name.localeCompare(b.name));

  alertConfigurations.forEach((alertConfig) => {
    alertConfigurationsMap[alertConfig.uuid] = alertConfig.name;
  });

  const previewTemplate = (alertConfigUUID: string) => {
    const bodyAsHTML = new HTMLSerializer(bodyEditorRef.current!).serializeElement(bodyValue);
    const subjectAsHTML = new HTMLSerializer(subjectEditorRef.current!).serializeElement(
      subjectValue
    );

    const bodyAsText = convertNodesToText(bodyValue);
    const subjectAsText = convertNodesToText(subjectValue);

    fillTemplateWithValue.mutate({
      bodyAsHTML,
      subjectAsHTML,
      bodyAsText,
      subjectAsText,
      alertConfigUUID
    });
  };

  return (
    <YBModal
      open={visible}
      title={t('alertCustomTemplates.alertVariablesPreviewModal.modalTitle')}
      dialogContentProps={{ className: clsx(classes.noPadding, commonStyles.noOverflow) }}
      onClose={() => onHide()}
      overrideWidth="740px"
      overrideHeight="540px"
      size="lg"
      titleSeparator
      enableBackdropDismiss
    >
      <Box className={classes.defaultPadding}>
        <Typography variant="body2">
          {t('alertCustomTemplates.alertVariablesPreviewModal.info')}
        </Typography>
        <YBSelect
          className={classes.select}
          data-testid="email-preview-select-config"
          onChange={(e) => {
            previewTemplate(e.target.value);
          }}
          renderValue={(selectedAlertConfig) => {
            if (!selectedAlertConfig) {
              return (
                <em>{t('alertCustomTemplates.alertVariablesPreviewModal.selectPlaceholder')}</em>
              );
            }
            return alertConfigurationsMap[selectedAlertConfig as string];
          }}
        >
          <MenuItem disabled value="">
            <em>{t('alertCustomTemplates.alertVariablesPreviewModal.selectPlaceholder')}</em>
          </MenuItem>
          {Object.keys(alertConfigurationsMap).map((alertConfigUuid) => (
            <MenuItem
              data-testid={`alert-config-${alertConfigurationsMap[alertConfigUuid]}`}
              key={alertConfigurationsMap[alertConfigUuid]}
              value={alertConfigUuid}
            >
              {alertConfigurationsMap[alertConfigUuid]}
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
            editorProps={{ readOnly: true, 'data-testid': 'preview-email-subject-editor' }}
            loadPlugins={{
              alertVariablesPlugin: true,
              singleLine: true
            }}
            initialValue={subjectValue}
            ref={subjectEditorRef}
          />
        </Box>
        <Typography variant="body1">{t('alertCustomTemplates.composer.content')}</Typography>
        <Box className={clsx(commonStyles.editorBorder, classes.bodyEditor)}>
          <YBEditor
            editorProps={{
              readOnly: true,
              style: { height: '180px' },
              'data-testid': 'preview-email-body-editor'
            }}
            loadPlugins={{ alertVariablesPlugin: true }}
            initialValue={bodyValue}
            ref={bodyEditorRef}
          />
        </Box>
      </Box>
    </YBModal>
  );
};

export default EmailPreviewModal;
