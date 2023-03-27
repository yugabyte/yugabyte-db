/*
 * Created on Tue Feb 14 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { FC, useEffect, useRef, useState } from 'react';
import clsx from 'clsx';
import { Grid, makeStyles, Popover } from '@material-ui/core';
import { Descendant, Transforms } from 'slate';
import { useMutation, useQuery } from 'react-query';
import { useTranslation } from 'react-i18next';
import { find } from 'lodash';
import { toast } from 'react-toastify';

import CustomVariablesEditor from './CustomVariables';
import { YBEditor } from '../../../components/YBEditor';
import { YBButton } from '../../../components';
import {
  ALERT_TEMPLATES_QUERY_KEY,
  createAlertChannelTemplates,
  getAlertChannelTemplates
} from './CustomVariablesAPI';
import { AddAlertVariablesPopup } from './AlertVariablesPopup';
import {
  ALERT_VARIABLE_START_TAG,
  clearEditor,
  IYBEditor
} from '../../../components/YBEditor/plugins';
import AlertVariablesPreviewModal from './AlertVariablesPreviewModal';
import { HTMLDeSerializer, HTMLSerializer } from '../../../components/YBEditor/serializers';
import { YBLoadingCircleIcon } from '../../../../components/common/indicators';
import { useCommonStyles } from './CommonStyles';
import { Add } from '@material-ui/icons';

type ComposerProps = {
  onHide: () => void;
};

const useStyles = makeStyles((theme) => ({
  root: {
    height: '600px'
  },
  content: {
    marginTop: theme.spacing(2.5)
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
  insertVariableButton: {
    border: '1px solid #E5E5E9',
    borderRadius: theme.spacing(0.9),
    height: '30px',
    float: 'right',
    fontWeight: 500,
    fontSize: '13px',
    color: '#67666C'
  },
  startTag: {
    width: '22px',
    height: '22px',
    border: `1px solid ${theme.palette.ybacolors.ybGrayHover}`,
    borderRadius: theme.spacing(0.5),
    marginLeft: theme.spacing(1)
  }
}));

const Composer: FC<ComposerProps> = ({ onHide }) => {
  const classes = useStyles();
  const commonStyles = useCommonStyles();
  const [subject, setSubject] = useState<Descendant[]>([]);
  const [body, setBody] = useState<Descendant[]>([]);

  const [showBodyAlertPopover, setShowBodyAlertPopover] = useState(false);
  const [showSubjectAlertPopover, setShowSubjectAlertPopover] = useState(false);
  const bodyInsertVariableButRef = useRef(null);

  const subjectEditorRef = useRef<IYBEditor | null>(null);
  const bodyEditorRef = useRef<IYBEditor | null>(null);

  const subjectInsertVariableButRef = useRef(null);

  const [showPreviewModal, setShowPreviewModal] = useState(false);

  const { data: channelTemplates, isLoading: isTemplateLoading } = useQuery(
    ALERT_TEMPLATES_QUERY_KEY.getAlertChannelTemplates,
    getAlertChannelTemplates
  );

  const createTemplate = useMutation(
    ({ textTemplate, titleTemplate }: { textTemplate: string; titleTemplate: string }) => {
      return createAlertChannelTemplates({
        type: 'Email',
        textTemplate,
        titleTemplate
      });
    },
    {
      onSuccess: () => {
        toast.success(t('alertCustomTemplates.composer.templateSavedSuccess'));
      }
    }
  );

  const getButton = (onClick: () => void, ref: React.MutableRefObject<any>) => {
    return (
      <YBButton
        onClick={onClick}
        innerRef={ref}
        variant="secondary"
        className={classes.insertVariableButton}
        startIcon={<Add />}
      >
        {t('alertCustomTemplates.composer.insertVariableButton')}
        <span className={classes.startTag}>{ALERT_VARIABLE_START_TAG}</span>
      </YBButton>
    );
  };

  const { t } = useTranslation();

  // if the template is already available , load it in the editor
  useEffect(() => {
    if (isTemplateLoading) return;

    const emailTemplate = find(channelTemplates?.data, { type: 'Email' });

    if (emailTemplate && bodyEditorRef.current && subjectEditorRef.current) {
      try {
        const bodyVal = new HTMLDeSerializer(
          bodyEditorRef.current,
          emailTemplate.textTemplate
        ).deserialize();
        clearEditor(bodyEditorRef.current);
        Transforms.insertNodes(bodyEditorRef.current, bodyVal);

        const subjectVal = new HTMLDeSerializer(
          subjectEditorRef.current,
          emailTemplate.titleTemplate
        ).deserialize();
        clearEditor(subjectEditorRef.current);
        Transforms.insertNodes(subjectEditorRef.current, subjectVal);
      } catch (e) {
        console.log(e);
      }
    }
  }, [isTemplateLoading, channelTemplates]);

  if (isTemplateLoading) {
    return <YBLoadingCircleIcon />;
  }

  return (
    <Grid container spacing={2} className={classes.root}>
      <Grid item xs={9} sm={9} lg={9} md={9}>
        <Grid>
          <Grid item alignItems="center" container>
            {t('alertCustomTemplates.composer.subject')}
            <Grid
              container
              item
              alignItems="center"
              className={clsx(commonStyles.editorBorder, commonStyles.subjectEditor)}
            >
              <Grid item style={{ width: '80%' }}>
                <YBEditor
                  showToolbar={false}
                  setVal={setSubject}
                  loadPlugins={{ singleLine: true, alertVariablesPlugin: true }}
                  ref={subjectEditorRef}
                />
              </Grid>
              <Grid item style={{ width: '20%' }}>
                {getButton(() => setShowSubjectAlertPopover(true), subjectInsertVariableButRef)}
                <AlertPopover
                  anchorEl={subjectInsertVariableButRef.current}
                  editor={subjectEditorRef.current as any}
                  handleClose={() => {
                    setShowSubjectAlertPopover(false);
                  }}
                  open={showSubjectAlertPopover}
                />
              </Grid>
            </Grid>
          </Grid>
          <Grid item className={classes.content}>
            {t('alertCustomTemplates.composer.content')}
            <Grid item className={commonStyles.editorBorder}>
              <YBEditor
                showToolbar={true}
                setVal={setBody}
                loadPlugins={{ alertVariablesPlugin: true }}
                ref={bodyEditorRef}
                moreToolbar={(editor) => {
                  return (
                    <>
                      {getButton(() => setShowBodyAlertPopover(true), bodyInsertVariableButRef)}
                      <AlertPopover
                        anchorEl={bodyInsertVariableButRef.current}
                        editor={editor}
                        handleClose={() => {
                          setShowBodyAlertPopover(false);
                        }}
                        open={showBodyAlertPopover}
                      />
                    </>
                  );
                }}
              />
            </Grid>
          </Grid>
        </Grid>
        <Grid
          container
          className={classes.actions}
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
                onHide();
              }}
            >
              {t('common.cancel')}
            </YBButton>
            <YBButton
              variant="primary"
              type="submit"
              autoFocus
              className={classes.submitButton}
              onClick={() => {
                if (bodyEditorRef.current && subjectEditorRef.current) {
                  const subjectHtml = new HTMLSerializer(subjectEditorRef.current).serialize();
                  const bodyHtml = new HTMLSerializer(bodyEditorRef.current).serialize();

                  createTemplate.mutate({
                    textTemplate: bodyHtml,
                    titleTemplate: subjectHtml
                  });
                }
              }}
            >
              {t('common.save')}
            </YBButton>
          </div>
        </Grid>
      </Grid>
      <Grid item xs={3} sm={3} lg={3} md={3} container justifyContent="center">
        <CustomVariablesEditor />
      </Grid>
      <AlertVariablesPreviewModal
        bodyValue={body}
        subjectValue={subject}
        visible={showPreviewModal}
        onHide={() => setShowPreviewModal(false)}
      />
    </Grid>
  );
};

export default Composer;

interface AlertPopoverProps {
  open: boolean;
  editor: IYBEditor;
  anchorEl: null | Element | ((element: Element) => Element);
  handleClose: () => void;
}

const AlertPopover: FC<AlertPopoverProps> = ({ open, anchorEl, editor, handleClose }) => {
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
          editor['addCustomVariable'](v);
          handleClose();
        }}
        onSystemVariableSelect={(v) => {
          editor['addSystemVariable'](v);
          handleClose();
        }}
      />
    </Popover>
  );
};
