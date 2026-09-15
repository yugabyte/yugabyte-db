import { useEffect, useMemo } from 'react';
import { FormProvider, useForm } from 'react-hook-form';
import { useTranslation } from 'react-i18next';
import { toast } from 'react-toastify';
import { yupResolver } from '@hookform/resolvers/yup';
import { mui, yba } from '@yugabyte-ui-library/core';
import { InstanceARNField } from '../../create-universe/fields';
import { awsArnFormSchema } from '../../create-universe/fields/arn-field/awsArnValidation';
import { useEditUniverse } from '../../../../../v2/api/universe/universe';
import { useEditUniverseTaskHandler } from '../hooks/useEditUniverseTaskHandler';
import { getClusterByType, useEditUniverseContext } from '../EditUniverseUtils';
import { createErrorMessage } from '../../../../../utils/ObjectUtils';
import { ClusterSpecClusterType } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { FullMoveWarning } from '../components';

const { YBModal } = yba;
const { styled, Box, boxClasses } = mui;

const ModalContent = styled(Box)(({ theme }) => ({
  [`.yb-${boxClasses.root}`]: {
    width: '100%'
  }
}));

interface EditNodeAcessModalProps {
  open: boolean;
  onClose: () => void;
}

interface NodeAcessFormProps {
  awsArnString: string;
}

export const EditNodeAcessModal = ({ open, onClose }: EditNodeAcessModalProps) => {
  const { t } = useTranslation('translation', { keyPrefix: 'editUniverse.advanced' });
  const { universeData } = useEditUniverseContext();
  const editUniverse = useEditUniverse();
  const universeUUID = universeData?.info?.universe_uuid;
  const handleEditUniverseSuccess = useEditUniverseTaskHandler(universeUUID);
  const primaryCluster = getClusterByType(universeData!, ClusterSpecClusterType.PRIMARY);
  const providerSpec = primaryCluster?.provider_spec;
  const awsArnString = primaryCluster?.provider_spec?.aws_instance_profile;

  const defaultValues = {
    awsArnString
  };
  const validationSchema = useMemo(() => awsArnFormSchema(t), [t]);
  const methods = useForm<NodeAcessFormProps>({
    defaultValues,
    resolver: yupResolver(validationSchema),
    mode: 'onSubmit',
    reValidateMode: 'onChange'
  });
  const {
    handleSubmit,
    reset,
    formState: { isDirty }
  } = methods;

  useEffect(() => {
    if (open) reset(defaultValues);
  }, [open]);

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
              provider_spec: {
                region_list: providerSpec?.region_list ?? [],
                aws_instance_profile: values.awsArnString,
                image_bundle_uuid: providerSpec?.image_bundle_uuid
              }
            }
          ]
        }
      },
      {
        onSuccess: (response) => {
          reset(values);
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
      title={t('nodeAccess')}
      submitLabel={t('apply', { keyPrefix: 'common' })}
      cancelLabel={t('cancel', { keyPrefix: 'common' })}
      titleSeparator
      size="md"
      dialogContentProps={{ sx: { padding: '16px !important', gap: '16px' } }}
      overrideHeight={'fit-content'}
      onSubmit={handleFormSubmit}
    >
      <FormProvider {...methods}>
        <ModalContent>
          <InstanceARNField disabled={false} />
          {isDirty && <FullMoveWarning setting="instanceProfileArn" />}
        </ModalContent>
      </FormProvider>
    </YBModal>
  );
};
