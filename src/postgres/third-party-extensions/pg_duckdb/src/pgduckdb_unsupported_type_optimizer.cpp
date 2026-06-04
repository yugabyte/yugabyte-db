#include "pgduckdb/pgduckdb_unsupported_type_optimizer.hpp"
#include "pgduckdb/pgduckdb_types.hpp"

#include "duckdb/optimizer/optimizer_extension.hpp"
#include "duckdb/planner/logical_operator.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/extension_type_info.hpp"

extern "C" {
#include "postgres.h"
#include "utils/elog.h"
}

namespace pgduckdb {

void
UnsupportedTypeOptimizer::VisitExpression(duckdb::unique_ptr<duckdb::Expression> *expression) {
	if (*expression) {
		CheckExpressionForUnsupportedTypes(**expression);
	}

	// Call parent implementation to continue traversal
	LogicalOperatorVisitor::VisitExpression(expression);
}

void
UnsupportedTypeOptimizer::CheckExpressionForUnsupportedTypes(duckdb::Expression &expr) {
	CheckForUnsupportedPostgresType(expr.return_type);
}

void
UnsupportedTypeOptimizer::OptimizeFunction(duckdb::OptimizerExtensionInput & /* input */,
                                           duckdb::unique_ptr<duckdb::LogicalOperator> &plan) {
	UnsupportedTypeOptimizer visitor;
	visitor.VisitOperator(*plan);
}

duckdb::OptimizerExtension
UnsupportedTypeOptimizer::GetOptimizerExtension() {
	duckdb::OptimizerExtension extension;
	extension.optimize_function = OptimizeFunction;
	return extension;
}

} // namespace pgduckdb
