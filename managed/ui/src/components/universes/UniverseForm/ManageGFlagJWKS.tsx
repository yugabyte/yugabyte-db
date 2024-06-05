import { FC } from 'react';
import { useTranslation } from 'react-i18next';
import { Box, makeStyles, Theme } from '@material-ui/core';
import { Field } from 'formik';
import { YBModalForm } from '../../common/forms';
import { YBFormInput } from '../../common/forms/fields';
import { YBLabel } from '../../../redesign/components';

interface ManageGFlagJWKSProps {
  open: boolean;
  token: string;
  rowIndex: number;
  onHide: () => void;
  onUpdate: (index: number, key: string) => void;
}

const useStyles = makeStyles((theme: Theme) => ({
  JWKSTextArea: {
    minWidth: '560px',
    maxWidth: '870px',
    minHeight: '320px',
    padding: theme.spacing(2)
  }
}));

export const ManageGFlagJWKS: FC<ManageGFlagJWKSProps> = ({
  open,
  rowIndex,
  token,
  onHide,
  onUpdate
}) => {
  const { t } = useTranslation();
  const classes = useStyles();

  const initialValues = {
    JWKSToken: token ?? ''
  };

  const handleFormSubmit = (values: any) => {
    if (values.JWKSToken) {
      onUpdate(rowIndex, values.JWKSToken);
    }
    onHide();
  };

  /* 
    Used the same Modal component as the one in GFlagField.tsx
    since using a different modal had overlay issues between two
    modal dialog
  */
  return (
    <YBModalForm
      size="large"
      title={t('universeForm.gFlags.JWKSModalTitle')}
      visible={open}
      formName="ManageJWKSForm"
      onHide={onHide}
      onFormSubmit={handleFormSubmit}
      initialValues={initialValues}
      submitLabel={t('common.save')}
      showCancelButton
      render={() => {
        return (
          <>
            <Box mb={1}>
              <YBLabel width="fit-content">{t('universeForm.gFlags.JWKSModalNote')}</YBLabel>
            </Box>
            <Field
              name="JWKSToken"
              component={YBFormInput}
              componentClass="textarea"
              className={classes.JWKSTextArea}
              placeholder={t('universeForm.gFlags.JWKSModalPlaceholder')}
            />
          </>
        );
      }}
    />
  );
};
