import React, { ComponentType, FC } from "react";
import { Box, makeStyles, Tab, Tabs } from "@material-ui/core";
import { useTranslation } from "react-i18next";
import { SummaryTab } from "./SummaryTab";
import { ReviewRecommendedTab } from "./ReviewRecommendedTab";
import clsx from "clsx";
import type { SchemaAnalysisData } from "./SchemaAnalysis";

const useStyles = makeStyles((theme) => ({
  fullWidth: {
    width: "100%",
  },
  nmt: {
    marginTop: theme.spacing(-1),
  },
}));

export interface ITabListItem {
  name: string;
  component: ComponentType<{ analysis: SchemaAnalysisData }>;
  testId: string;
}

const tabList: ITabListItem[] = [
  {
    name: "tabSummary",
    component: SummaryTab,
    testId: "MigrationSchemaTabList-Summary",
  },
  {
    name: "tabSuggestedRefactoring",
    component: ReviewRecommendedTab,
    testId: "MigrationSchemaTabList-SuggestedRefactoring",
  },
];

type SchemaAnalysisTabsProps = {
  analysis: SchemaAnalysisData;
};

export const SchemaAnalysisTabs: FC<SchemaAnalysisTabsProps> = ({ analysis }) => {
  const classes = useStyles();
  const { t } = useTranslation();

  const [currentTab, setCurrentTab] = React.useState<string>(tabList[0].name);
  const TabComponent = tabList.find((tab) => tab.name === currentTab)?.component;

  const handleChange = (_: React.ChangeEvent<{}>, newValue: string) => {
    setCurrentTab(newValue);
  };

  return (
    <Box className={clsx(classes.fullWidth, classes.nmt)}>
      <Tabs
        indicatorColor="primary"
        textColor="primary"
        data-testid="MigrationSchemaTabList"
        value={currentTab}
        onChange={handleChange}
      >
        {tabList.map((tab) => (
          <Tab
            key={tab.name}
            value={tab.name}
            label={t(`clusterDetail.voyager.migrateSchema.${tab.name}`)}
            data-testid={tab.testId}
          />
        ))}
      </Tabs>

      <Box mt={3}>{TabComponent && <TabComponent analysis={analysis} />}</Box>
    </Box>
  );
};
