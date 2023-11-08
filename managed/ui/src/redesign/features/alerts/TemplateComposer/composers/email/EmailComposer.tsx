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
import { useTranslation } from 'react-i18next';
import { useMutation, useQuery } from 'react-query';
import { toast } from 'react-toastify';
import { Descendant, Transforms } from 'slate';
import { ReactEditor } from 'slate-react';
import { debounce, find } from 'lodash';
import { YBLoadingCircleIcon } from '../../../../../../components/common/indicators';
import { YBButton } from '../../../../../components';
import { YBEditor } from '../../../../../components/YBEditor';
import {
  clearEditor,
  DefaultElement,
  isBlockActive,
  isEditorDirty, isMarkActive,
  IYBEditor,
  resetEditorHistory,
  TextDecorators,
  toggleBlock,
  toggleMark
} from '../../../../../components/YBEditor/plugins';
import { HTMLSerializer } from '../../../../../components/YBEditor/serializers';
import EmailPreviewModal from './EmailPreviewModal';
import { useCommonStyles } from '../../CommonStyles';
import {
  ALERT_TEMPLATES_QUERY_KEY,
  createAlertChannelTemplates,
  fetchAlertTemplateVariables,
  getAlertChannelTemplates
} from '../../CustomVariablesAPI';
import { AlertPopover, GetInsertVariableButton, useComposerStyles } from '../ComposerStyles';
import { IComposer, IComposerRef } from '../IComposer';
import RollbackToTemplateModal from '../webhook/RollbackToTemplateModal';
import { findInvalidVariables, loadTemplateIntoEditor } from '../ComposerUtils';
import { createErrorMessage } from '../../../../../../utils/ObjectUtils';

//icons
import { Info } from '@material-ui/icons';

import { ReactComponent as Italic } from '../icons/italic.svg';
import { ReactComponent as Bold } from '../icons/bold.svg';
import { ReactComponent as Underline } from '../icons/underline.svg';
import { ReactComponent as Strikethrough } from '../icons/strikethrough.svg';
import { FormatAlignCenter, FormatAlignLeft, FormatAlignRight } from '@material-ui/icons';
import { convertHTMLToText } from '../../../../../components/YBEditor/transformers/HTMLToTextTransform';
import { ReactComponent as ClearTemplate } from '../icons/clearTemplate.svg';
import { RbacValidator } from '../../../../rbac/common/RbacApiPermValidator';
import { ApiPermissionMap } from '../../../../rbac/ApiAndUserPermMapping';

const ToolbarMarkIcons: Partial<Record<TextDecorators, { icon: React.ReactChild }>> = {
  italic: {
    icon: <Italic />
  },
  bold: {
    icon: <Bold />
  },
  underline: {
    icon: <Underline className="big" />
  },
  strikethrough: {
    icon: <Strikethrough className="big" />
  }
};

const ToolbarBlockIcons: Record<
  string,
  { icon: React.ReactElement; fn: Function; align: string }
> = {
  alignLeft: {
    icon: <FormatAlignLeft className="medium" />,
    fn: (editor: IYBEditor) => toggleBlock(editor, 'left'),
    align: 'left'
  },
  alignCenter: {
    icon: <FormatAlignCenter className="medium" />,
    fn: (editor: IYBEditor) => toggleBlock(editor, 'center'),
    align: 'center'
  },
  alignRight: {
    icon: <FormatAlignRight className="medium" />,
    fn: (editor: IYBEditor) => toggleBlock(editor, 'right'),
    align: 'right'
  }
};

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

const EmailComposer = React.forwardRef<IComposerRef, React.PropsWithChildren<IComposer>>(
  ({ onClose }, forwardRef) => {
    const { t } = useTranslation();
    const commonStyles = useCommonStyles();
    const composerStyles = useComposerStyles();
    const classes = useStyles();
    const [subject, setSubject] = useState<Descendant[]>([]);
    const [body, setBody] = useState<Descendant[]>([]);

    const [showBodyAlertPopover, setShowBodyAlertPopover] = useState(false);
    const [showSubjectAlertPopover, setShowSubjectAlertPopover] = useState(false);
    const bodyInsertVariableButRef = useRef(null);

    const subjectEditorRef = useRef<IYBEditor | null>(null);
    const bodyEditorRef = useRef<IYBEditor | null>(null);

    const subjectInsertVariableButRef = useRef(null);

    const [showPreviewModal, setShowPreviewModal] = useState(false);
    const [showRollbackTemplateModal, setShowRollbackTemplateModal] = useState(false);
    const [isRollbackedToDefaultTemplate, setIsRollbackedToDefaultTemplate] = useState(false);

    // counter to force re-render this component, if any operations is performed on the body editor
    const [counter, setCounter] = useState(1);
    const reRender = debounce(function () {
      setCounter(counter + 1);
    }, 200);

    const { data: channelTemplates, isLoading: isTemplateLoading } = useQuery(
      ALERT_TEMPLATES_QUERY_KEY.getAlertChannelTemplates,
      getAlertChannelTemplates
    );

    const { data: alertVariables, isLoading: isAlertVariablesLoading } = useQuery(
      ALERT_TEMPLATES_QUERY_KEY.fetchAlertTemplateVariables,
      fetchAlertTemplateVariables
    );

    const createTemplate = useMutation(
      async ({ textTemplate, titleTemplate }: { textTemplate: string; titleTemplate: string }) => {
        let invalidVariables = findInvalidVariables(titleTemplate, alertVariables!.data);
        invalidVariables = [
          ...invalidVariables,
          ...findInvalidVariables(textTemplate, alertVariables!.data)
        ];

        if (invalidVariables.length > 0) {
          return Promise.reject({
            message: t('alertCustomTemplates.composer.invalidVariables', {
              variable: invalidVariables.join(', ')
            })
          });
        }

        return createAlertChannelTemplates({
          type: 'Email',
          textTemplate,
          titleTemplate
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

      const emailTemplate = find(channelTemplates?.data, { type: 'Email' });
      if (!emailTemplate || !alertVariables?.data) return;

      loadTemplateIntoEditor(
        (emailTemplate.textTemplate
          ? emailTemplate.textTemplate
          : emailTemplate.defaultTextTemplate) ?? '',
        alertVariables.data,
        bodyEditorRef.current
      );

      // subject editor is restricted only to one line
      loadTemplateIntoEditor(
        emailTemplate?.titleTemplate?.replace('\n', '')
          ? emailTemplate.titleTemplate?.replace('\n', '')
          : emailTemplate.defaultTitleTemplate?.replace('\n', ''),
        alertVariables.data,
        subjectEditorRef.current
      );
    }, [isTemplateLoading, channelTemplates, isAlertVariablesLoading, alertVariables]);

    useImperativeHandle(
      forwardRef,
      () => ({
        editors: {
          bodyEditor: bodyEditorRef,
          subjectEditor: subjectEditorRef
        }
      }),
      []
    );

    // rollback to default template
    const rollbackTemplate = () => {
      const emailTemplate = find(channelTemplates?.data, { type: 'Email' });
      if (!emailTemplate || isAlertVariablesLoading) return;

      loadTemplateIntoEditor(
        emailTemplate.defaultTextTemplate ?? '',
        alertVariables!.data,
        bodyEditorRef.current
      );
      loadTemplateIntoEditor(
        emailTemplate.defaultTitleTemplate ?? '',
        alertVariables!.data,
        subjectEditorRef.current
      );

      setIsRollbackedToDefaultTemplate(true);
      setShowRollbackTemplateModal(false);
      resetEditorHistory(bodyEditorRef.current!);
      resetEditorHistory(subjectEditorRef.current!);
    };

    if (isTemplateLoading) {
      return <YBLoadingCircleIcon />;
    }

    return (
      <>
        <Grid className={classes.composers}>
          <Grid item alignItems="center" container className={composerStyles.subjectArea}>
            {t('alertCustomTemplates.composer.subject')}
            <Grid
              container
              item
              alignItems="center"
              className={clsx(
                commonStyles.editorBorder,
                commonStyles.subjectEditor,
                composerStyles.editorArea
              )}
            >
              <Grid item style={{ width: '80%' }}>
                <YBEditor
                  setVal={setSubject}
                  loadPlugins={{
                    singleLine: true,
                    alertVariablesPlugin: true,
                    basic: true,
                    defaultPlugin: false
                  }}
                  ref={subjectEditorRef}
                  editorProps={{
                    'data-testid': 'email-subject-editor'
                  }}
                />
              </Grid>
              <Grid item style={{ width: '20%' }}>
                <GetInsertVariableButton
                  onClick={() => setShowSubjectAlertPopover(true)}
                  ref={subjectInsertVariableButRef}
                />
                <AlertPopover
                  anchorEl={subjectInsertVariableButRef.current}
                  editor={subjectEditorRef.current as any}
                  onVariableSelect={(variable, type) => {
                    if (type === 'SYSTEM') {
                      subjectEditorRef.current!['addSystemVariable'](variable);
                    } else {
                      subjectEditorRef.current!['addCustomVariable'](variable);
                    }
                  }}
                  handleClose={() => {
                    setShowSubjectAlertPopover(false);
                  }}
                  open={showSubjectAlertPopover}
                />
              </Grid>
            </Grid>
          </Grid>
          <Grid item className={composerStyles.content}>
            {t('alertCustomTemplates.composer.content')}
            <Grid item className={clsx(commonStyles.editorBorder, composerStyles.editorArea)}>
              <Grid className={composerStyles.toolbarRoot} container alignItems="center">
                <Grid item className={composerStyles.formatIcons}>
                  {Object.keys(ToolbarMarkIcons).map((ic) =>
                    React.cloneElement(ToolbarMarkIcons[ic].icon, {
                      key: ic,
                      'data-testid': `mark-icon-${ic}`,
                      className: clsx(
                        ToolbarMarkIcons[ic].icon.props.className,
                        isMarkActive(bodyEditorRef.current, ic as TextDecorators) ? 'active' : ''
                      ),
                      onClick: (e: React.MouseEvent) => {
                        e.preventDefault();
                        toggleMark(bodyEditorRef.current!, ic as TextDecorators);
                        if (bodyEditorRef.current?.selection) {
                          ReactEditor.focus(bodyEditorRef.current);
                          Transforms.select(bodyEditorRef.current, bodyEditorRef.current.selection);
                        }
                      }
                    })
                  )}
                  {Object.keys(ToolbarBlockIcons).map((ic) =>
                    React.cloneElement(ToolbarBlockIcons[ic].icon, {
                      key: ic,
                      'data-testid': `block-icon-${ic}`,
                      className: clsx(
                        ToolbarBlockIcons[ic].icon.props.className,
                        isBlockActive(bodyEditorRef.current, ToolbarBlockIcons[ic].align, 'align')
                          ? 'active'
                          : ''
                      ),
                      onClick: (e: React.MouseEvent) => {
                        e.preventDefault();
                        ToolbarBlockIcons[ic].fn(bodyEditorRef.current);
                      }
                    })
                  )}
                </Grid>
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
                      bodyEditorRef.current!.insertNode(DefaultElement);

                      clearEditor(subjectEditorRef.current!);
                      subjectEditorRef.current!.insertNode(DefaultElement);
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
                loadPlugins={{ alertVariablesPlugin: true, basic: true }}
                ref={bodyEditorRef}
                // on onClick and on onKeyDown , update the counter state, re-render this component,
                // such that the marks (bold, italics) are marked
                editorProps={{
                  onClick: () => reRender(),
                  'data-testid': 'email-body-editor'
                }}
                onEditorKeyDown={() => {
                  isRollbackedToDefaultTemplate && setIsRollbackedToDefaultTemplate(false);
                  reRender();
                }}
              />
            </Grid>
          </Grid>
          <Grid item container alignItems="center" className={composerStyles.helpText}>
            <Info />
            <Typography variant="body2">
              {t('alertCustomTemplates.composer.helpText', { type: 'email' })}
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
                data-testid="preview-email-button"
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
                data-testid="cancel-email-button"
              >
                {t('common.cancel')}
              </YBButton>
              <RbacValidator
                accessRequiredOn={ApiPermissionMap.CREATE_ALERT_TEMPLATE}
                isControl
              >
                <YBButton
                  variant="primary"
                  type="submit"
                  disabled={
                    !isEditorDirty(subjectEditorRef.current) &&
                    !isEditorDirty(bodyEditorRef.current) &&
                    !isRollbackedToDefaultTemplate
                  }
                  autoFocus
                  className={composerStyles.submitButton}
                  data-testid="save-email-button"
                  onClick={() => {
                    if (bodyEditorRef.current && subjectEditorRef.current) {
                      const subjectHtml = new HTMLSerializer(subjectEditorRef.current).serialize();
                      const bodyHtml = new HTMLSerializer(bodyEditorRef.current).serialize();
                      createTemplate.mutateAsync({
                        textTemplate: convertHTMLToText(bodyHtml) ?? '',
                        titleTemplate: convertHTMLToText(subjectHtml) ?? ''
                      });
                    }
                  }}
                >
                  {t('common.save')}
                </YBButton>
              </RbacValidator>
            </div>
          </Grid>
        </Grid>
        <EmailPreviewModal
          bodyValue={body}
          subjectValue={subject}
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

export default EmailComposer;
