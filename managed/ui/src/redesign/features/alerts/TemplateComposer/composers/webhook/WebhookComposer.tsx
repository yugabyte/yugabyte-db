/*
 * Created on Wed Apr 05 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { useEffect, useImperativeHandle, useRef, useState } from 'react';
import { Grid, makeStyles, Typography } from '@material-ui/core';
import clsx from 'clsx';
import { find } from 'lodash';
import { useTranslation } from 'react-i18next';
import { useMutation, useQuery } from 'react-query';
import { toast } from 'react-toastify';
import { Descendant } from 'slate';
import { YBLoadingCircleIcon } from '../../../../../../components/common/indicators';
import { YBButton } from '../../../../../components';
import { YBEditor } from '../../../../../components/YBEditor';
import {
  clearEditor,
  DefaultJSONElement,
  isEditorDirty,
  IYBEditor,
  resetEditorHistory
} from '../../../../../components/YBEditor/plugins';
import { useCommonStyles } from '../../CommonStyles';
import {
  ALERT_TEMPLATES_QUERY_KEY,
  createAlertChannelTemplates,
  fetchAlertTemplateVariables,
  getAlertChannelTemplates
} from '../../CustomVariablesAPI';
import { AlertPopover, GetInsertVariableButton, useComposerStyles } from '../ComposerStyles';
import { IComposer, IComposerRef } from '../IComposer';
import RollbackToTemplateModal from './RollbackToTemplateModal';

import { ReactComponent as ClearTemplate } from '../icons/clearTemplate.svg';
import WebhookPreviewModal from './WebhookPreviewModal';
import { findInvalidVariables, loadTemplateIntoEditor } from '../ComposerUtils';
import { HTMLSerializer } from '../../../../../components/YBEditor/serializers';
import { convertHTMLToText } from '../../../../../components/YBEditor/transformers/HTMLToTextTransform';
import { createErrorMessage } from '../../../../universe/universe-form/utils/helpers';
import { Info } from '@material-ui/icons';
import { RbacValidator } from '../../../../rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../../rbac/ApiAndUserPermMapping';

const useStyles = makeStyles((theme) => ({
  composers: {
    padding: `0 ${theme.spacing(3.5)}px !important`
  },
  moreToolbarActions: {
    display: 'flex',
    alignItems: 'center',
    gap: theme.spacing(1.2)
  },
  rollToDefaultTemplateText: {
    textDecoration: 'underline',
    color: '#44518B',
    cursor: 'pointer',
    userSelect: 'none'
  }
}));

const WebhookComposer = React.forwardRef<IComposerRef, React.PropsWithChildren<IComposer>>(
  ({ onClose }, forwardRef) => {
    const { t } = useTranslation();
    const commonStyles = useCommonStyles();
    const composerStyles = useComposerStyles();
    const classes = useStyles();
    const [body, setBody] = useState<Descendant[]>([]);

    const [showBodyAlertPopover, setShowBodyAlertPopover] = useState(false);
    const bodyInsertVariableButRef = useRef(null);

    const bodyEditorRef = useRef<IYBEditor | null>(null);

    const [showPreviewModal, setShowPreviewModal] = useState(false);
    const [showRollbackTemplateModal, setShowRollbackTemplateModal] = useState(false);
    const [isRollbackedToDefaultTemplate, setIsRollbackedToDefaultTemplate] = useState(false);

    const { data: channelTemplates, isLoading: isTemplateLoading } = useQuery(
      ALERT_TEMPLATES_QUERY_KEY.getAlertChannelTemplates,
      getAlertChannelTemplates
    );

    const { data: alertVariables, isLoading: isAlertVariablesLoading } = useQuery(
      ALERT_TEMPLATES_QUERY_KEY.fetchAlertTemplateVariables,
      fetchAlertTemplateVariables
    );

    const createTemplate = useMutation(
      async ({ textTemplate }: { textTemplate: string }) => {
        const invalidVariables = findInvalidVariables(textTemplate, alertVariables!.data);

        if (invalidVariables.length > 0) {
          return Promise.reject({
            message: t('alertCustomTemplates.composer.invalidVariables', {
              variable: invalidVariables.join(', ')
            })
          });
        }

        return createAlertChannelTemplates({
          type: 'WebHook',
          textTemplate
        });
      },
      {
        onSuccess: () => {
          toast.success(t('alertCustomTemplates.composer.templateSavedSuccess'));
        },
        onError(error) {
          toast.error(createErrorMessage(error));
        }
      }
    );

    useEffect(() => {
      if (isTemplateLoading || isAlertVariablesLoading || isEditorDirty(bodyEditorRef.current))
        return;

      const webhookTemplate = find(channelTemplates?.data, { type: 'WebHook' });
      if (!webhookTemplate || !alertVariables) return;

      loadTemplateIntoEditor(
        (webhookTemplate.textTemplate
          ? webhookTemplate.textTemplate
          : webhookTemplate.defaultTextTemplate) ?? '',
        alertVariables.data,
        bodyEditorRef.current
      );
    }, [isTemplateLoading, channelTemplates, isAlertVariablesLoading, alertVariables]);

    useImperativeHandle(
      forwardRef,
      () => ({
        editors: {
          bodyEditor: bodyEditorRef,
          subjectEditor: { current: null }
        }
      }),
      []
    );

    // rollback to default template
    const rollbackTemplate = () => {
      const webhookTemplate = find(channelTemplates?.data, { type: 'WebHook' });
      if (webhookTemplate && alertVariables?.data) {
        loadTemplateIntoEditor(
          webhookTemplate.defaultTextTemplate,
          alertVariables.data,
          bodyEditorRef.current
        );
      }
      setShowRollbackTemplateModal(false);
      resetEditorHistory(bodyEditorRef.current!);
      setIsRollbackedToDefaultTemplate(true);
    };

    if (isTemplateLoading) {
      return <YBLoadingCircleIcon />;
    }

    return (
      <>
        <Grid className={classes.composers}>
          <Grid item className={composerStyles.content}>
            <Grid item className={clsx(commonStyles.editorBorder, composerStyles.editorArea)}>
              <Grid className={composerStyles.toolbarRoot} container alignItems="center">
                <Grid item className={composerStyles.formatIcons}></Grid>
                <Grid item className={classes.moreToolbarActions}>
                  {isEditorDirty(bodyEditorRef.current) && (
                    <Typography
                      variant="body2"
                      className={classes.rollToDefaultTemplateText}
                      onClick={() => {
                        setShowRollbackTemplateModal(true);
                      }}
                      data-testid="webhook-rollback-template"
                    >
                      {t('alertCustomTemplates.composer.webhookComposer.rollback')}
                    </Typography>
                  )}

                  <YBButton
                    className={composerStyles.insertVariableButton}
                    variant="secondary"
                    startIcon={<ClearTemplate />}
                    onClick={() => {
                      clearEditor(bodyEditorRef.current!);
                      bodyEditorRef.current!.insertNode(DefaultJSONElement);
                    }}
                    data-testid="webhook-clear-template"
                  >
                    {t('alertCustomTemplates.composer.webhookComposer.clearTemplate')}
                  </YBButton>
                  <GetInsertVariableButton
                    onClick={() => setShowBodyAlertPopover(true)}
                    ref={bodyInsertVariableButRef}
                  />
                  <AlertPopover
                    anchorEl={bodyInsertVariableButRef.current}
                    editor={bodyEditorRef.current as IYBEditor}
                    onVariableSelect={(variable, type) => {
                      if (type === 'SYSTEM') {
                        bodyEditorRef.current!['addSystemVariable'](variable);
                      } else {
                        bodyEditorRef.current!['addCustomVariable'](variable);
                      }
                    }}
                    handleClose={() => {
                      setShowBodyAlertPopover(false);
                    }}
                    open={showBodyAlertPopover}
                  />
                </Grid>
              </Grid>
              <YBEditor
                setVal={setBody}
                loadPlugins={{ jsonPlugin: true, alertVariablesPlugin: true }}
                ref={bodyEditorRef}
                initialValue={[DefaultJSONElement]}
                showLineNumbers
                editorProps={{ style: { height: '480px' }, 'data-testid': 'webhook-body-editor' }}
                onEditorKeyDown={() => {
                  isRollbackedToDefaultTemplate && setIsRollbackedToDefaultTemplate(false);
                }}
              />
            </Grid>
          </Grid>
          <Grid item container alignItems="center" className={composerStyles.helpText}>
            <Info />
            <Typography variant="body2">
              {t('alertCustomTemplates.composer.helpText', { type: 'webhook' })}
            </Typography>
          </Grid>
        </Grid>
        <Grid item className={commonStyles.noPadding}>
          <Grid
            container
            className={composerStyles.actions}
            alignItems="center"
            justifyContent="space-between"
          >
            <RbacValidator
              accessRequiredOn={ApiPermissionMap.PREVIEW_ALERT_NOTIFICATION}
              isControl
            >
              <YBButton
                variant="secondary"
                onClick={() => {
                  setShowPreviewModal(true);
                }}
                data-testid="preview-webhook-button"
              >
                {t('alertCustomTemplates.composer.previewTemplateButton')}
              </YBButton>
            </RbacValidator>
            <div>
              <YBButton
                variant="secondary"
                onClick={() => {
                  onClose();
                }}
                data-testid="cancel-webhook-button"
              >
                {t('common.cancel')}
              </YBButton>
              <RbacValidator
                accessRequiredOn={ApiPermissionMap.CREATE_ALERT_CHANNEL_TEMPLATE}
                isControl
              >
                <YBButton
                  variant="primary"
                  type="submit"
                  disabled={!isEditorDirty(bodyEditorRef.current) && !isRollbackedToDefaultTemplate}
                  autoFocus
                  className={composerStyles.submitButton}
                  onClick={() => {
                    if (bodyEditorRef.current) {
                      const bodyText = new HTMLSerializer(bodyEditorRef.current).serialize();

                      createTemplate.mutate({
                        textTemplate: convertHTMLToText(bodyText)
                      });
                    }
                  }}
                  data-testid="save-webhook-button"
                >
                  {t('common.save')}
                </YBButton>
              </RbacValidator>
            </div>
          </Grid>
        </Grid>
        <WebhookPreviewModal
          bodyValue={body}
          visible={showPreviewModal}
          onHide={() => setShowPreviewModal(false)}
        />
        <RollbackToTemplateModal
          visible={showRollbackTemplateModal}
          onHide={() => {
            setShowRollbackTemplateModal(false);
          }}
          onSubmit={() => {
            rollbackTemplate();
          }}
        />
      </>
    );
  }
);

export default WebhookComposer;
