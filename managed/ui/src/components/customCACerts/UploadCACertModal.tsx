/*
 * Created on Mon Jun 05 2023
 *
 * Copyright 2021 YugaByte, Inc. and Contributors
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License")
 * You may not use this file except in compliance with the License. You may obtain a copy of the License at
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

import React, { FC, useEffect } from 'react';
import * as Yup from 'yup';
import { useTranslation } from 'react-i18next';
import { useForm } from 'react-hook-form';
import { Typography, makeStyles } from '@material-ui/core';
import { yupResolver } from '@hookform/resolvers/yup';
import { useMutation, useQuery, useQueryClient } from 'react-query';
import { toast } from 'react-toastify';
import { YBInputField, YBModal } from '../../redesign/components';
import { CACert, CACertUploadParams } from './ICerts';
import { getCertDetails, updateCA, uploadCA } from './CACertsApi';
import { readFileAsText } from '../configRedesign/providerRedesign/forms/utils';
import { YBLoadingCircleIcon } from '../common/indicators';
import { createErrorMessage } from '../../utils/ObjectUtils';
import { YBDropZone } from '../configRedesign/providerRedesign/components/YBDropZone/YBDropZone';

type UploadCACertModalProps = {
  visible: boolean;
  onHide: () => void;
  editCADetails: CACert | null;
};

type UploadCACertModalForm = {
  certName: string;
  certFile: File | undefined;
};

const INITIAL_FORM_VALUE: UploadCACertModalForm = {
  certName: '',
  certFile: undefined
};

const useStyles = makeStyles((theme) => ({
  root: {
    padding: `${theme.spacing(3)}px ${theme.spacing(4.5)}px`
  },
  nameInput: {
    width: '380px',
    marginTop: theme.spacing(0.8)
  },
  uploadCertPanel: {
    background: `rgba(229, 229, 233, 0.1)`,
    borderRadius: theme.spacing(1),
    marginTop: theme.spacing(4),
    color: '#818182',
    whiteSpace: 'pre-wrap',
    textAlign: 'center',

    '& button': {
      height: theme.spacing(5),
      padding: theme.spacing(1.2),
      minWidth: '90px'
    },
    '& .updateFilesContainer': {
      justifyContent: 'space-between',
      '& >div': {
        maxWidth: 'unset'
      }
    }
  },
  uploadCertCtrl: {
    height: '210px'
  },
  modalTitle: {
    marginLeft: theme.spacing(2.25)
  }
}));

const UploadCACertModal: FC<UploadCACertModalProps> = ({ visible, onHide, editCADetails }) => {
  const { t } = useTranslation();
  const classes = useStyles();
  const queryClient = useQueryClient();

  const validationSchema = Yup.object().shape({
    certName: Yup.string().required(t('common.requiredField')),
    certFile: Yup.mixed().required(t('common.requiredField'))
  });

  const {
    handleSubmit,
    control,
    formState: { isValid },
    setValue,
    watch,
    reset
  } = useForm<UploadCACertModalForm>({
    mode: 'onChange',
    defaultValues: INITIAL_FORM_VALUE,
    resolver: yupResolver(validationSchema)
  });

  const hideModal = () => {
    reset();
    onHide();
  };

  const uploadCert = useMutation(
    (cert: CACertUploadParams) => {
      if (editCADetails === null) {
        return uploadCA(cert);
      } else {
        return updateCA(editCADetails.id, cert);
      }
    },

    {
      onError(error) {
        toast.error(createErrorMessage(error));
      },
      onSuccess: () => {
        toast.success(t('customCACerts.uploadCACertModal.successMsg'));
        queryClient.invalidateQueries('ca_certs_list');
        hideModal();
      }
    }
  );

  const { isLoading } = useQuery([editCADetails?.id], () => getCertDetails(editCADetails!.id), {
    enabled: editCADetails !== null,
    onSuccess: (data) => {
      setValue('certName', data.data.name);
      const blob = new Blob([data.data.contents], {
        type: 'text/plain'
      });
      setValue('certFile', new File([blob], data.data.name));
    }
  });

  const handleFormSubmit = handleSubmit((formValues) => {
    if (formValues.certFile) {
      readFileAsText(formValues.certFile).then((fileContents) => {
        uploadCert.mutate({ name: formValues.certName, contents: fileContents ?? '' });
      });
    }
  });

  useEffect(() => {
    reset();
  }, [visible, reset]);

  if (editCADetails !== null && isLoading) return <YBLoadingCircleIcon />;

  return (
    <YBModal
      open={visible}
      onClose={hideModal}
      title={t('customCACerts.uploadCACertModal.title')}
      onSubmit={handleFormSubmit}
      cancelLabel={t('common.cancel')}
      submitLabel={t('customCACerts.uploadCACertModal.save')}
      overrideWidth="960px"
      overrideHeight="560px"
      size="lg"
      titleSeparator
      enableBackdropDismiss
      dialogContentProps={{
        className: classes.root,
        dividers: true
      }}
      titleContentProps={classes.modalTitle}
      buttonProps={{
        primary: {
          disabled: !isValid
        }
      }}
    >
      <Typography variant="subtitle1">
        {t('customCACerts.uploadCACertModal.certNameTitle')}
      </Typography>
      <YBInputField
        className={classes.nameInput}
        name="certName"
        control={control}
        data-testid="certName"
        disabled={editCADetails !== null}
      />
      <div className={classes.uploadCertPanel}>
        <YBDropZone
          name="certFile"
          actionButtonText={t('customCACerts.uploadCACertModal.uploadBtnText')}
          multipleFiles={false}
          descriptionText={t('customCACerts.uploadCACertModal.uploadBtnSubText')}
          className={classes.uploadCertCtrl}
          onChange={(file) => {
            setValue('certFile', file, { shouldValidate: true });
          }}
          value={watch('certFile')}
        />
      </div>
    </YBModal>
  );
};

export default UploadCACertModal;
