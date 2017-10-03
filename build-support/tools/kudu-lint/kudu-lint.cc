// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTTypeTraits.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/AST/Stmt.h"
#include "clang/Driver/Options.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/TextDiagnostic.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Support/CommandLine.h"

// Using clang without importing namespaces is damn near impossible.
using namespace llvm; // NOLINT
using namespace clang::ast_matchers; // NOLINT

using llvm::opt::OptTable;
using clang::ast_type_traits::DynTypedNode;
using clang::driver::createDriverOptTable;
using clang::driver::options::OPT_ast_dump;
using clang::tooling::CommonOptionsParser;
using clang::tooling::CommandLineArguments;
using clang::tooling::ClangTool;
using clang::tooling::newFrontendActionFactory;
using clang::ASTContext;
using clang::CharSourceRange;
using clang::ClassTemplateSpecializationDecl;
using clang::Decl;
using clang::DiagnosticsEngine;
using clang::Stmt;
using clang::FixItHint;
using clang::SourceLocation;
using clang::SourceRange;
using clang::Stmt;
using clang::TextDiagnostic;
using std::string;

static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);
static cl::extrahelp MoreHelp(
    "\tFor example, to run kudu-lint on all files in a subtree of the\n"
    "\tsource tree, use:\n"
    "\n"
    "\t  find path/in/subtree -name '*.cc'|xargs kudu-lint\n"
    "\n"
    "\tor using a specific build path:\n"
    "\n"
    "\t  find path/in/subtree -name '*.cc'|xargs kudu-lint -p build/path\n"
    "\n"
    "\tNote, that path/in/subtree and current directory should follow the\n"
    "\trules described above.\n"
    "\n"
    "\t Sometimes kudu-lint can't figure out the proper line number for an\n"
    "\t error, and reports it inside some standard library. the -ast-dump\n"
    "\t option can be useful for these circumstances.\n"
    "\n"
);

// Command line flags.
static OwningPtr<OptTable> gOptions(createDriverOptTable());
static cl::list<std::string> gArgsAfter(
  "extra-arg",
  cl::desc("Additional argument to append to the compiler command line"));
static cl::list<std::string> gArgsBefore(
  "extra-arg-before",
  cl::desc("Additional argument to prepend to the compiler command line"));
static cl::opt<bool> gASTDump("ast-dump",
                              cl::desc(gOptions->getOptionHelpText(OPT_ast_dump)));

namespace {

// Callback for unused statuses. Simply reports the error at the point where the
// expression was found.
template<class NODETYPE>
class ErrorPrinter : public MatchFinder::MatchCallback {
 public:
  ErrorPrinter(const std::string& error_msg,
               const std::string& bound_name,
               bool skip_system_headers)
    : error_msg_(error_msg),
      bound_name_(bound_name),
      skip_system_headers_(skip_system_headers) {
  }

  virtual void run(const MatchFinder::MatchResult& result) {
    const NODETYPE* node;
    if ((node = result.Nodes.getNodeAs<NODETYPE>(bound_name_))) {
      SourceRange r = node->getSourceRange();

      if (skip_system_headers_ && result.SourceManager->isInSystemHeader(r.getBegin())) {
        return;
      }

      if (gASTDump) {
        node->dump();
      }

      if (r.isValid()) {
        TextDiagnostic td(llvm::outs(), result.Context->getLangOpts(),
                          &result.Context->getDiagnostics().getDiagnosticOptions());
        td.emitDiagnostic(r.getBegin(), DiagnosticsEngine::Error,
                          error_msg_,
                          ArrayRef<CharSourceRange>(CharSourceRange::getTokenRange(r)),
                          ArrayRef<FixItHint>(), result.SourceManager);
      }

      SourceLocation instantiation_point;
      if (FindInstantiationPoint(result, node, &instantiation_point)) {
        TextDiagnostic td(llvm::outs(), result.Context->getLangOpts(),
                          &result.Context->getDiagnostics().getDiagnosticOptions());
        td.emitDiagnostic(instantiation_point, DiagnosticsEngine::Note,
                          "previous error instantiated at",
                          ArrayRef<CharSourceRange>(),
                          ArrayRef<FixItHint>(), result.SourceManager);
      }
    } else {
      llvm_unreachable("bound node missing");
    }
  }

 private:
  bool GetParent(ASTContext* ctx, const DynTypedNode& node, DynTypedNode* parent) {
    ASTContext::ParentVector parents = ctx->getParents(node);
    if (parents.empty()) return false;
    assert(parents.size() == 1);
    *parent = parents[0];
    return true;
  }

  // If the AST node 'node' has an ancestor which is a template instantiation,
  // fill the source location of that instantiation into 'loc'. Unfortunately,
  // Clang doesn't retain enough information in the AST nodes to recurse here --
  // so in many cases this is useless, since the instantiation point will simply
  // be inside another instantiated template.
  bool FindInstantiationPoint(const MatchFinder::MatchResult& result,
                              const NODETYPE* node,
                              SourceLocation* loc) {
    DynTypedNode dyn_node = DynTypedNode::create<NODETYPE>(*node);

    // Recurse up the tree.
    while (true) {
      const ClassTemplateSpecializationDecl* D =
          dyn_node.get<ClassTemplateSpecializationDecl>();
      if (D) {
        *loc = D->getPointOfInstantiation();
        return true;
      }
      // TODO: there are probably other types of specializations to handle, but this is the only
      // one seen so far.

      DynTypedNode parent;
      if (!GetParent(result.Context, dyn_node, &parent)) {
        return false;
      }
      dyn_node = parent;
    }
  }

  string error_msg_;
  string bound_name_;
  bool skip_system_headers_;
};

// Inserts arguments before or after the usual command line arguments.
class InsertAdjuster: public clang::tooling::ArgumentsAdjuster {
 public:
  enum Position { BEGIN, END };

  InsertAdjuster(const CommandLineArguments &extra, Position pos)
    : extra_(extra), pos_(pos) {
  }

  InsertAdjuster(const char *extra_, Position pos)
    : extra_(1, std::string(extra_)), pos_(pos) {
  }

  virtual CommandLineArguments Adjust(const CommandLineArguments &Args) LLVM_OVERRIDE {
    CommandLineArguments ret(Args);

    CommandLineArguments::iterator I;
    if (pos_ == END) {
      I = ret.end();
    } else {
      I = ret.begin();
      ++I; // To leave the program name in place
    }

    ret.insert(I, extra_.begin(), extra_.end());
    return ret;
  }

 private:
  const CommandLineArguments extra_;
  const Position pos_;
};

} // anonymous namespace

int main(int argc, const char **argv) {
  CommonOptionsParser options_parser(argc, argv);
  ClangTool Tool(options_parser.getCompilations(),
                 options_parser.getSourcePathList());
  if (gArgsAfter.size() > 0) {
    Tool.appendArgumentsAdjuster(new InsertAdjuster(gArgsAfter,
          InsertAdjuster::END));
  }
  if (gArgsBefore.size() > 0) {
    Tool.appendArgumentsAdjuster(new InsertAdjuster(gArgsBefore,
          InsertAdjuster::BEGIN));
  }

  // Match expressions of type 'Status' which are parented by a compound statement.
  // This implies that the expression is being thrown away, rather than assigned
  // to some variable or function call.
  //
  // For more information on AST matchers, refer to:
  // http://clang.llvm.org/docs/LibASTMatchersReference.html
  StatementMatcher ignored_status_matcher =
    expr(hasType(recordDecl(hasName("Status"))),
         hasParent(compoundStmt())).bind("expr");
  ErrorPrinter<Stmt> ignored_status_printer("Unused status result", "expr", false);

  // Match class members which are reference-typed. This is confusing since they
  // tend to "look like" copied values, but in fact often reference external
  // entities passed in in the constructor.
  DeclarationMatcher ref_member_matcher =
    fieldDecl(hasType(referenceType()))
    .bind("decl");
  ErrorPrinter<Decl> ref_member_printer("Reference-typed member", "decl", true);

  // Disallow calls to sleep, usleep, and nanosleep.
  // SleepFor(MonoDelta) should be used instead, as it is not prone to
  // unit conversion errors, and also ignores EINTR so will safely sleep
  // at least the requested duration.
  StatementMatcher sleep_matcher =
    callExpr(callee(namedDecl(matchesName("(nano|u)?sleep")))).bind("sleep_expr");
  ErrorPrinter<Stmt> sleep_printer("sleep, usleep or nanosleep call", "sleep_expr", true);

  MatchFinder finder;
  finder.addMatcher(ignored_status_matcher, &ignored_status_printer);
  finder.addMatcher(ref_member_matcher, &ref_member_printer);
  finder.addMatcher(sleep_matcher, &sleep_printer);

  return Tool.run(newFrontendActionFactory(&finder));
}
