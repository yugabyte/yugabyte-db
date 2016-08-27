//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Parse Tree Declaration.
//
// This modules includes declarations for parse tree and base class for tree nodes. The parser
// (parser_gram.y) will create these nodes and link them together to from parse tree.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_PARSER_PARSE_TREE_H_
#define YB_SQL_PARSER_PARSE_TREE_H_

#include <stdint.h>

#include <memory>
#include <string>

namespace yb {
namespace sql {

// TreeNode base class.
class TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::shared_ptr<TreeNode> SharedPtr;
  typedef std::shared_ptr<const TreeNode> SharedPtrConst;

  typedef std::unique_ptr<TreeNode> UniPtr;
  typedef std::unique_ptr<const TreeNode> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Public functions.
  TreeNode();
  ~TreeNode();
};

// Parse Tree
class ParseTree {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef std::shared_ptr<ParseTree> SharedPtr;
  typedef std::shared_ptr<const ParseTree> SharedPtrConst;

  typedef std::unique_ptr<ParseTree> UniPtr;
  typedef std::unique_ptr<const ParseTree> UniPtrConst;

  //------------------------------------------------------------------------------------------------
  // Public functions.
  ParseTree();
  ~ParseTree();

 private:
  //------------------------------------------------------------------------------------------------
  // Private data member.
  TreeNode *root;
};

}  // namespace sql.
}  // namespace yb.

#endif  // YB_SQL_PARSER_PARSE_TREE_H_
