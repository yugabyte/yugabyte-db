// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#pragma once

#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/program_options.hpp>

#include "yb/util/flags.h"
#include "yb/util/result.h"

// Framework for creating command line tool with multiple subcommands.
// Usage:
// 1) Define actions list for this tool:
// #define MY_TOOL_ACTIONS (Help)(Action1)(Action2)
// 2) Create enum for actions:
// YB_DEFINE_ENUM(MyToolAction, MY_TOOL_ACTIONS);
// 3) For each action define required types and functions:
// 3a) Action arguments:
// struct ActionArguments {
//   int arg1;
//   std::vector<std::string> arg2;
// };
// 3b) Action arguments options:
// const std::string kAction1Description = "Here goes description for action1";
// std::unique_ptr<OptionsDescription> Action1Options() {
//   auto result = std::make_unique<OptionsDescriptionImpl<Action1Arguments>>(
//       kAction1Description);
//   result->desc.add_options()
//       ("arg1", boost::program_options::value(&result->args.arg1)->required(),
//        "Required argument")
//       ("arg2", boost::program_options::value(&result->args.arg2), "Repeatable argument");
//   return result;
// }
// 3c) Action implementation:
// Status ApplyPatchExecute(const ApplyPatchArguments& args) {
//   ApplyPatch apply_patch;
//   return apply_patch.Execute(args);
// }
// 4) Bind actions with framework: YB_TOOL_ARGUMENTS(MyToolAction, MY_TOOL_ACTIONS);
// 5) Run framework from main function:
// return yb::tools::ExecuteTool<yb::tools::MyToolAction>(argc, argv);

namespace yb {
namespace tools {

template <class Action>
std::string OptionName(Action action) {
  auto name = ToString(action);
  std::string result;
  for (char ch : name) {
    if (std::isupper(ch)) {
      if (!result.empty()) {
        result += '-';
      }
      result += std::tolower(ch);
    } else {
      result += ch;
    }
  }
  return result;
}

template <class Action>
Result<Action> ActionByName(const std::string& name) {
  for (auto action : List(static_cast<Action*>(nullptr))) {
    if (OptionName(action) == name) {
      return action;
    }
  }
  return STATUS_FORMAT(InvalidArgument, "Unknown command: $0", name);
}

template <class Action>
void ShowHelp(Action action) {
  auto options = CreateOptionsDescription(action);
  std::cout << options->desc << std::endl;
}

template <class Action>
void ShowCommands() {
  std::cout << "Commands:" << std::endl;
  size_t max_name_len = 0;
  for (auto action : List(static_cast<Action*>(nullptr))) {
    max_name_len = std::max(max_name_len, OptionName(action).size());
  }
  max_name_len += 3;
  for (auto action : List(static_cast<Action*>(nullptr))) {
    auto current_name = OptionName(action);
    std::cout << "  " << current_name << std::string(max_name_len - current_name.length(), ' ')
              << Description(action) << std::endl;
  }
}

template <class Action>
void ShowUsage() {
  if (IsUsageMessageSet()) {
    google::ShowUsageWithFlagsRestrict(google::ProgramInvocationName(), __FILE__);
    std::cout << std::endl;
  }
  ShowCommands<Action>();
}

struct OptionsDescription {
  boost::program_options::positional_options_description positional;
  boost::program_options::options_description desc;
  boost::program_options::options_description hidden;

  explicit OptionsDescription(const std::string& caption) : desc(caption) {}
  virtual ~OptionsDescription() = default;
};

template<class Arguments>
struct OptionsDescriptionImpl : OptionsDescription {
  Arguments args;

  explicit OptionsDescriptionImpl(const std::string& caption) : OptionsDescription(caption) {}
};

#define YB_TOOL_ARGUMENTS_DESCRIPTION(x, enum_name, elem) \
  case enum_name::elem: return BOOST_PP_CAT(k, BOOST_PP_CAT(elem, Description));

#define YB_TOOL_ARGUMENTS_CREATE_OPTIONS(x, enum_name, elem) \
  case enum_name::elem: return BOOST_PP_CAT(elem, Options)();

#define YB_TOOL_ARGUMENTS_EXECUTE(x, enum_name, elem) \
  case enum_name::elem: \
    return BOOST_PP_CAT(elem, Execute)( \
        static_cast<const OptionsDescriptionImpl<BOOST_PP_CAT(elem, Arguments)>&>( \
            description).args);

#define YB_TOOL_ARGUMENTS(enum_name, actions) \
  const std::string& Description(enum_name action) { \
    switch (action) { \
      BOOST_PP_SEQ_FOR_EACH(YB_TOOL_ARGUMENTS_DESCRIPTION, enum_name, actions); \
    } \
    FATAL_INVALID_ENUM_VALUE(enum_name, action); \
  } \
  std::unique_ptr<OptionsDescription> CreateOptionsDescription(enum_name action) { \
    switch (action) { \
      BOOST_PP_SEQ_FOR_EACH(YB_TOOL_ARGUMENTS_CREATE_OPTIONS, enum_name, actions); \
    } \
    FATAL_INVALID_ENUM_VALUE(enum_name, action); \
  } \
  Status Execute(enum_name action, const OptionsDescription& description) { \
    switch (action) { \
      BOOST_PP_SEQ_FOR_EACH(YB_TOOL_ARGUMENTS_EXECUTE, enum_name, actions); \
    } \
    FATAL_INVALID_ENUM_VALUE(enum_name, action); \
  }

inline std::string GetCommand(int argc, char** argv) {
  return argc < 2 ? "" : argv[1];
}

template <class Action>
int ExecuteTool(int argc, char** argv) {
  std::string cmd = GetCommand(argc, argv);
  if (cmd.empty()) {
    ShowUsage<Action>();
    return 0;
  }

  auto action = ActionByName<Action>(cmd);
  if (!action.ok()) {
    std::cerr << action.status().message().ToBuffer() << std::endl;
    return 1;
  }

  try {
    auto options = CreateOptionsDescription(*action);
    boost::program_options::command_line_parser parser(argc - 1, argv + 1);
    boost::program_options::options_description descriptions;
    descriptions.add(options->desc);
    descriptions.add(options->hidden);
    parser.options(descriptions).positional(options->positional);
    auto parsed = parser.run();
    boost::program_options::variables_map variables_map;
    boost::program_options::store(parsed, variables_map);
    boost::program_options::notify(variables_map);
    auto status = Execute(*action, *options);
    if (!status.ok()) {
      std::cerr << status.message().ToBuffer() << std::endl;
      return 1;
    }
    return 0;
  } catch (std::exception& exc) {
    std::cerr << exc.what() << std::endl;
    ShowHelp(*action);
    return 1;
  }

  return 0;
}

template <class Action>
void ParseAndCutGFlagsFromCommandLine(int* argc, char*** argv) {
  std::set<std::string> commands;
  for (auto action : List(static_cast<Action*>(nullptr))) {
    commands.insert(OptionName(action));
  }

  int tail_argc = 0;
  for (int i = 0; i < *argc; ++i) {
    if ((*argv)[i][0] != '-' && commands.contains((*argv)[i])) {
      tail_argc = *argc - i;
      break;
    }
  }

  *argc -= tail_argc;
  ParseCommandLineFlags(argc, argv, /* remove_flag= */ true);
  *argc += tail_argc;
}

// ------------------------------------------------------------------------------------------------
// Help command by default
// ------------------------------------------------------------------------------------------------
const std::string kCommonHelpDescription = "Show help on command";

struct CommonHelpArguments {
  std::string command;
};

std::unique_ptr<OptionsDescription> CommonHelpOptions() {
  auto result = std::make_unique<OptionsDescriptionImpl<CommonHelpArguments>>(
      kCommonHelpDescription);
  result->positional.add("command", 1);
  result->hidden.add_options()
      ("command", boost::program_options::value(&result->args.command));
  return result;
}

template <class Action>
Status CommonHelpExecute(const CommonHelpArguments& args) {
  if (args.command.empty()) {
    ShowUsage<Action>();
  } else {
    ShowHelp(VERIFY_RESULT(ActionByName<Action>(args.command)));
  }
  return Status::OK();
}

// ------------------------------------------------------------------------------------------------
// Helpers for specifying valid ranges of options
// ------------------------------------------------------------------------------------------------

template<typename OptionType>
auto OptionLowerBound(const char* option_name, OptionType lower_bound) ->
    typename std::enable_if<std::is_integral<OptionType>::value,
                            std::function<void(OptionType)>>::type {
  return [option_name, lower_bound](OptionType value) {
    if (value < lower_bound) {
        throw boost::program_options::validation_error(
            boost::program_options::validation_error::invalid_option_value,
            option_name,
            std::to_string(value));
    }
  };
}

} // namespace tools
} // namespace yb
