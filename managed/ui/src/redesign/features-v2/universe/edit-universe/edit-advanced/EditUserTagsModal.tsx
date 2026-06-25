import { FormProvider, useForm } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { toast } from 'react-toastify';
import { mui, yba } from '@yugabyte-ui-library/core';
import { UserTagsField } from '../../create-universe/fields';
import { useEditUniverse } from '../../../../../v2/api/universe/universe';
import { useEditUniverseTaskHandler } from '../hooks/useEditUniverseTaskHandler';
import { getClusterByType, useEditUniverseContext } from '../EditUniverseUtils';
import { createErrorMessage } from '../../../../../utils/ObjectUtils';
import {
  transformInstanceTags,
  transformTagsArrayToObject
} from '../../../../features/universe/universe-form/utils/helpers';
import { InstanceTag } from '../../create-universe/steps/advanced-settings/dtos';
import { ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';

const { YBModal } = yba;
const { styled, Box, boxClasses } = mui;

const ModalContent = styled(Box)(({ theme }) => ({
  [`.yb-${boxClasses.root}`]: {
    width: '100%'
  }
}));

interface EditUserTagModalProps {
  open: boolean;
  onClose: () => void;
}

interface UserTagsFormProps {
  instanceTags: InstanceTag[];
}

export const EditUserTagsModal = ({ open, onClose }: EditUserTagModalProps) => {
  const { t } = useTranslation('translation', { keyPrefix: 'editUniverse.advanced' });
  const { universeData } = useEditUniverseContext();
  const editUniverse = useEditUniverse();
  const universeUUID = universeData?.info?.universe_uuid;
  const handleEditUniverseSuccess = useEditUniverseTaskHandler(universeUUID);
  const primaryCluster = getClusterByType(universeData!, ClusterSpecClusterType.PRIMARY);
  const userTags = transformInstanceTags(primaryCluster?.instance_tags || {});
  const defaultValues = {
    instanceTags:
      userTags.length >= 0
        ? userTags
        : [
            { name: '', value: '' },
            { name: '', value: '' }
          ]
  };

  const methods = useForm<UserTagsFormProps>({
    defaultValues
  });

  const { handleSubmit } = methods;

  const handleFormSubmit = handleSubmit(async (values) => {
    if (!universeUUID || !primaryCluster?.uuid) {
      toast.error(t('unableToApplyChanges'));
      return;
    }

    editUniverse.mutate(
      {
        uniUUID: universeUUID,
        data: {
          expected_universe_version: -1,
          clusters: [
            {
              uuid: primaryCluster.uuid,
              instance_tags: transformTagsArrayToObject(values?.instanceTags)
            }
          ]
        }
      },
      {
        onSuccess: (response) => {
          handleEditUniverseSuccess(response.task_uuid);
          onClose();
        },
        onError: (error: unknown) => {
          toast.error(createErrorMessage(error));
        }
      }
    );
  });

  return (
    <YBModal
      open={open}
      onClose={onClose}
      title={t('userTagsTitle')}
      submitLabel={t('save', { keyPrefix: 'common' })}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      titleSeparator
      size="md"
      dialogContentProps={{ sx: { padding: '16px !important' } }}
      overrideHeight={'fit-content'}
      onSubmit={handleFormSubmit}
    >
      <FormProvider {...methods}>
        <ModalContent>
          <UserTagsField disabled={false} />
        </ModalContent>
      </FormProvider>
    </YBModal>
  );
};
