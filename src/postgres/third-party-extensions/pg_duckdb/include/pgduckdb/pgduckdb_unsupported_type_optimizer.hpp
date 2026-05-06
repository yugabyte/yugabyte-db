#pragma once

#include "duckdb/optimizer/optimizer_extension.hpp"
#include "duckdb/planner/logical_operator.hpp"
#include "duckdb/planner/logical_operator_visitor.hpp"
#include "duckdb/planner/expression.hpp"

namespace pgduckdb {

class UnsupportedTypeOptimizer : public duckdb::LogicalOperatorVisitor {
public:
	//! Override VisitExpression to check for unsupported types
	void VisitExpression(duckdb::unique_ptr<duckdb::Expression> *expression) override;

	//! Get the optimizer extension to register with DuckDB
	static duckdb::OptimizerExtension GetOptimizerExtension();

private:
	//! Check if an expression contains UnsupportedPostgresType
	void CheckExpressionForUnsupportedTypes(duckdb::Expression &expr);

	//! The main optimize function called by DuckDB
	static void OptimizeFunction(duckdb::OptimizerExtensionInput &input,
	                             duckdb::unique_ptr<duckdb::LogicalOperator> &plan);
};

} // namespace pgduckdb
