import React from 'react';
import { Box, Typography } from "@material-ui/core";
import { useTranslation } from 'react-i18next';

interface VoyagerVersionBoxProps {
  voyagerVersion: string;
}

const VoyagerVersionBox: React.FC<VoyagerVersionBoxProps> = ({ voyagerVersion }) => {
  const { t } = useTranslation();

  const greyBox = {
    backgroundColor: '#f5f5f5',
    marginLeft: 20,
    display: 'flex',
    alignItems: 'center',
    borderRadius: 6,
    fontWeight: 500,
    fontSize: 500,
    border: '1px solid #eaeaea',
    borderColor: 'divider',
    padding: '8px 20px',
  };

  return (
    <Box style={greyBox}>
      <Box style={{ maxWidth: "100px", whiteSpace: "normal", wordBreak: "break-word" }}>
        <Typography variant="body2">
          {t("clusterDetail.voyager.planAndAssess.summary.voyagerVersion")}
        </Typography>
      </Box>

      <Box style={{ marginLeft: 12 }}>
        <Typography>
          {voyagerVersion}
        </Typography>
      </Box>
    </Box>
  );
};

export default VoyagerVersionBox;
