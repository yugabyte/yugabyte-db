import React, { FC } from "react";
import { YBTooltip } from "@app/components";
import { Box, makeStyles } from "@material-ui/core";
import clsx from "clsx";
import { useTranslation, TFunction } from "react-i18next";

const useStyles = makeStyles((theme) => ({
  complexity: {
    width: 12,
    height: 12,
    borderRadius: "100%",
    border: `1px solid ${theme.palette.grey[300]}`,
  },
  complexityActive: {
    backgroundColor: theme.palette.warning[300],
    borderColor: theme.palette.warning[500],
  },
}));

interface ComplexityComponentProps {
  complexity?: string;
};

export const getComplexityString = (complexity: string, t: TFunction<"translation", undefined>) => {

  const complexityTitle: {[key: string]: string} = {
    "high": t("clusterDetail.voyager.planAndAssess.complexity.hard"),
    "medium": t("clusterDetail.voyager.planAndAssess.complexity.medium"),
    "low": t("clusterDetail.voyager.planAndAssess.complexity.easy"),
  };
  return complexityTitle[complexity];
};

export const ComplexityComponent: FC<ComplexityComponentProps> = ({ complexity }) => {
  const classes = useStyles();
  const complexityL = complexity?.toLowerCase();
  const { t } = useTranslation();

  const totalComplexityCount = 3;
  const activeComplexityCount = complexityL === "high" ? 3 : complexityL === "medium" ? 2 : 1;


  const complexityString = getComplexityString(complexityL ?? "n/a", t);

  if (complexityString === undefined) {
    return <>{t("clusterDetail.voyager.planAndAssess.complexity.notAvailable")}</>
  }

  return (
    <YBTooltip title={complexityString}>
      <Box display="flex" alignItems="center" gridGap={6} width="fit-content">
        {Array.from({ length: totalComplexityCount }).map((_, index) => (
          <Box
            key={index}
            className={clsx(
              classes.complexity,
              index < activeComplexityCount && classes.complexityActive
            )}
          />
        ))}
      </Box>
    </YBTooltip>
  );
};
