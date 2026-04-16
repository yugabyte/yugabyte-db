import { YBModal } from '@yugabytedb/ui-components';
import { MetadataFields } from '../../helpers/dtos';
import { Box, makeStyles, Theme, Typography } from '@material-ui/core';
import { useMutation } from 'react-query';
import { TroubleshootAPI } from '../../api';
import { toast } from 'react-toastify';

interface RemoveUniverseMetadataProps {
  data: MetadataFields;
  open: boolean;
  onClose: () => void;
  onActionPerformed: () => void;
}

const useStyles = makeStyles((theme: Theme) => ({
  root: {
    padding: `${theme.spacing(3)}px ${theme.spacing(4.5)}px`
  },
  modalTitle: {
    marginLeft: theme.spacing(2.25)
  }
}));

export const RemoveUniverseMetadata = ({
  data,
  open,
  onClose,
  onActionPerformed
}: RemoveUniverseMetadataProps) => {
  const helperClasses = useStyles();

  // DELETE API call to delete the universe from TS database
  const deleteUniverseMetadata = useMutation(
    () => TroubleshootAPI.deleteUniverseMetadata(data.id),
    {
      onSuccess: (response: any) => {
        toast.success(
          'Universe removed successfully, no further troubleshooting on this universe willbe performed'
        );
        onActionPerformed();
        onClose();
      },
      onError: () => {
        toast.error('Not able to remove universe, please try again');
      }
    }
  );

  const handleSubmit = async () => {
    deleteUniverseMetadata.mutateAsync();
  };

  return (
    <YBModal
      open={open}
      onClose={onClose}
      title={'Remove Universe'}
      onSubmit={handleSubmit}
      cancelLabel={'Cancel'}
      submitLabel={'Remove'}
      overrideHeight="250px"
      size="sm"
      titleSeparator
      enableBackdropDismiss
      dialogContentProps={{
        className: helperClasses.root,
        dividers: true
      }}
    >
      <Box mt={2}>
        <Typography variant="body2">
          {`Are you sure you want to remove the universe ${data.id} for further troubleshooting ?`}
        </Typography>
      </Box>
    </YBModal>
  );
};
