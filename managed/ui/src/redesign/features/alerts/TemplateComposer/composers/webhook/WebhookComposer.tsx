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
import { Descendant, Transforms } from 'slate';
import { HistoryEditor } from 'slate-history';
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
  getAlertChannelTemplates
} from '../../CustomVariablesAPI';
import { AlertPopover, GetInsertVariableButton, useComposerStyles } from '../ComposerStyles';
import { IComposer, IComposerRef } from '../IComposer';
import { JSONDeserializer } from '../../../../../components/YBEditor/serializers/JSON/JSONDeSerializer';
import { TextSerializer } from '../../../../../components/YBEditor/serializers/Text/TextSerializer';
import RollbackToTemplateModal from './RollbackToTemplateModal';

import { ReactComponent as ClearTemplate } from '../icons/clearTemplate.svg';
import WebhookPreviewModal from './WebhookPreviewModal';
import { Info } from '@material-ui/icons';

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

    const { data: channelTemplates, isLoading: isTemplateLoading } = useQuery(
      ALERT_TEMPLATES_QUERY_KEY.getAlertChannelTemplates,
      getAlertChannelTemplates
    );

    const createTemplate = useMutation(
      ({ textTemplate }: { textTemplate: string }) => {
        return createAlertChannelTemplates({
          type: 'WebHook',
          textTemplate
        });
      },
      {
        onSuccess: () => {
          toast.success(t('alertCustomTemplates.composer.templateSavedSuccess'));
        }
      }
    );

    useEffect(() => {
      if (isTemplateLoading) return;

      const webhookTemplate = find(channelTemplates?.data, { type: 'WebHook' });

      if (webhookTemplate && bodyEditorRef.current) {
        try {
          const bodyVal = new JSONDeserializer(
            bodyEditorRef.current,
            webhookTemplate.textTemplate ?? webhookTemplate.defaultTextTemplate ?? ''
          ).deserialize();

          // Don't aleter the history while loading the template
          HistoryEditor.withoutSaving(bodyEditorRef.current, () => {
            clearEditor(bodyEditorRef.current as IYBEditor);
            Transforms.insertNodes(bodyEditorRef.current as IYBEditor, bodyVal);
          });
        } catch (e) {
          console.log(e);
        }
      }
    }, [isTemplateLoading, channelTemplates]);

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
      if (webhookTemplate && bodyEditorRef.current) {
        const bodyVal = new JSONDeserializer(
          bodyEditorRef.current,
          webhookTemplate.defaultTextTemplate ?? ''
        ).deserialize();
        clearEditor(bodyEditorRef.current as IYBEditor);
        Transforms.insertNodes(bodyEditorRef.current as IYBEditor, bodyVal);
      }
      setShowRollbackTemplateModal(false);
      resetEditorHistory(bodyEditorRef.current!);
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
                    onVariableSelect={(variable) => {
                      bodyEditorRef.current!['insertJSONVariable'](variable);
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
                loadPlugins={{ jsonPlugin: true }}
                ref={bodyEditorRef}
                initialValue={[DefaultJSONElement]}
                showLineNumbers
                editorProps={{ style: { height: '480px' } }}
              />
            </Grid>
          </Grid>
          <Grid item container alignItems="center" className={composerStyles.helpText}>
            <Info />
            <Typography variant="body2">{t('alertCustomTemplates.composer.helpText')}</Typography>
          </Grid>
        </Grid>
        <Grid item className={commonStyles.noPadding}>
          <Grid
            container
            className={composerStyles.actions}
            alignItems="center"
            justifyContent="space-between"
          >
            <YBButton
              variant="secondary"
              onClick={() => {
                setShowPreviewModal(true);
              }}
            >
              {t('alertCustomTemplates.composer.previewTemplateButton')}
            </YBButton>
            <div>
              <YBButton
                variant="secondary"
                onClick={() => {
                  onClose();
                }}
              >
                {t('common.cancel')}
              </YBButton>
              <YBButton
                variant="primary"
                type="submit"
                disabled={!isEditorDirty(bodyEditorRef.current)}
                autoFocus
                className={composerStyles.submitButton}
                onClick={() => {
                  if (bodyEditorRef.current) {
                    const bodyText = new TextSerializer(bodyEditorRef.current).serialize();

                    createTemplate.mutate({
                      textTemplate: bodyText
                    });
                  }
                }}
              >
                {t('common.save')}
              </YBButton>
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
