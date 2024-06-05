import { FC, useState } from 'react';
import { useForm } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { Box, Typography } from '@material-ui/core';
import { YBCheckbox, YBInputField, YBModal } from '../../../../components';
import { api } from '../utils/api';
import { getAsyncCluster, getUniverseName, transitToUniverse } from '../utils/helpers';
import { UniverseDetails } from '../utils/dto';

interface DeleteClusterModalProps {
  open: boolean;
  universeData: UniverseDetails;
  onClose: () => void;
}

type DeleteClusterFormValues = {
  universeName: string | null;
};

const DEFAULT_VALUES: DeleteClusterFormValues = {
  universeName: null
};

export const DeleteClusterModal: FC<DeleteClusterModalProps> = ({
  open,
  universeData,
  onClose
}) => {
  const [forceDelete, setForceDelete] = useState<boolean>(false);
  const { t } = useTranslation();
  const universeName = getUniverseName(universeData);

  //init form
  const {
    control,
    formState: { isValid },
    handleSubmit
  } = useForm<DeleteClusterFormValues>({
    defaultValues: DEFAULT_VALUES,
    mode: 'onChange',
    reValidateMode: 'onChange'
  });

  const handleFormSubmit = handleSubmit(async () => {
    const asyncCluster = getAsyncCluster(universeData);
    const clusterUUID = asyncCluster?.uuid;
    const universeUUID = universeData.universeUUID;

    if (!clusterUUID) return;

    try {
      await api.deleteCluster(clusterUUID, universeUUID, forceDelete);
    } catch (e) {
      console.error(e);
    } finally {
      transitToUniverse(universeUUID);
    }
  });

  const forceDeleteCheckBox = () => {
    return (
      <YBCheckbox
        defaultChecked={forceDelete}
        value={forceDelete}
        label={t('universeForm.deleteClusterModal.forceDeleteCheckbox')}
        size="medium"
        onChange={(e) => setForceDelete(e.target.checked)}
      />
    );
  };

  return (
    <YBModal
      open={open}
      overrideHeight={300}
      titleSeparator
      cancelLabel={t('common.no')}
      submitLabel={t('common.yes')}
      size="sm"
      title={t('universeForm.deleteClusterModal.modalTitle', { universeName })}
      footerAccessory={forceDeleteCheckBox()}
      onClose={onClose}
      onSubmit={handleFormSubmit}
      buttonProps={{
        primary: {
          disabled: !isValid
        }
      }}
      dialogContentProps={{ style: { paddingTop: 20 } }}
      submitTestId="submit-delete-cluster"
      cancelTestId="close-delete-cluster"
    >
      <Box display="flex" width="100%" flexDirection="column" data-testid="delete-cluster-modal">
        <Box>
          <Typography variant="body2">
            {t('universeForm.deleteClusterModal.deleteRRMessage')}
          </Typography>
        </Box>
        <Box mt={2.5}>
          <Typography variant="body1">
            {t('universeForm.deleteClusterModal.enterUniverseName')}
          </Typography>
          <Box mt={1}>
            <YBInputField
              fullWidth
              control={control}
              placeholder={universeName}
              name="universeName"
              inputProps={{
                autoFocus: true,
                'data-testid': 'validate-universename'
              }}
              rules={{
                validate: {
                  universeNameMatch: (value) => value === universeName
                }
              }}
            />
          </Box>
        </Box>
      </Box>
    </YBModal>
  );
};
