import React, { FC, useMemo } from "react";
import type { RefactoringCount } from "@app/api/src";
import { useTranslation } from "react-i18next";
import { YBTable } from "@app/components";
import { BadgeVariant, YBBadge } from "@app/components/YBBadge/YBBadge";

interface RefactoringGraphProps {
  sqlObjects: RefactoringCount[] | undefined;
}

export const RefactoringGraph: FC<RefactoringGraphProps> = ({ sqlObjects }) => {
  const { t } = useTranslation();
  const graphData = useMemo(() => {
    if (!sqlObjects) {
      return [];
    }

    return sqlObjects
      .filter(({ automatic, manual }) => (automatic ?? 0) + (manual ?? 0) > 0)
      .map(({ sql_object_type, automatic, manual }) => {
        return {
          objectType:
            sql_object_type
              ?.trim()
              .toLowerCase(),
          automaticDDLImport: automatic ?? 0,
          manualRefactoring: manual ?? 0,
        };
      });
  }, [sqlObjects]);

  if (!graphData.length) {
    return null;
  }

  const columns = [
    {
      name: "objectType",
      label: t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.objectType"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px", textTransform: "capitalize" } }),
      },
    },
    {
      name: "automaticDDLImport",
      label:
        t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.automaticDDLImport"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
        customBodyRender: (count: number) =>
          <YBBadge
            text={count}
            variant={BadgeVariant.Success}
          />
      },
    },
    {
      name: "manualRefactoring",
      label:
        t("clusterDetail.voyager.planAndAssess.recommendation.schemaChanges.manualRefactoring"),
      options: {
        setCellHeaderProps: () => ({ style: { padding: "8px 16px" } }),
        setCellProps: () => ({ style: { padding: "8px 16px" } }),
        customBodyRender: (count: number) =>
          <YBBadge
            text={count}
            variant={BadgeVariant.Warning}
          />
      },
    },
  ];

  return (
    <YBTable
      data={graphData}
      columns={columns}
      withBorder={false}
      options={{
        pagination: false,
      }}
    />
  );
};
