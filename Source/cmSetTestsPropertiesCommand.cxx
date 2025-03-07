/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file LICENSE.rst or https://cmake.org/licensing for details.  */
#include "cmSetTestsPropertiesCommand.h"

#include <algorithm>
#include <iterator>

#include <cmext/string_view>

#include "cmArgumentParser.h"
#include "cmExecutionStatus.h"
#include "cmGlobalGenerator.h"
#include "cmMakefile.h"
#include "cmRange.h"
#include "cmStringAlgorithms.h"
#include "cmSystemTools.h"
#include "cmTest.h"

bool cmSetTestsPropertiesCommand(std::vector<std::string> const& args,
                                 cmExecutionStatus& status)
{
  if (args.empty()) {
    status.SetError("called with incorrect number of arguments");
    return false;
  }

  // first identify the properties arguments
  auto propsIter = std::find(args.begin(), args.end(), "PROPERTIES");
  if (propsIter == args.end() || propsIter + 1 == args.end()) {
    status.SetError("called with illegal arguments, maybe missing a "
                    "PROPERTIES specifier?");
    return false;
  }

  if (std::distance(propsIter, args.end()) % 2 != 1) {
    status.SetError("called with incorrect number of arguments.");
    return false;
  }

  std::vector<std::string> tests;
  std::string directory;
  cmArgumentParser<void> parser;
  parser.Bind("DIRECTORY"_s, directory);
  auto result = parser.Parse(cmStringRange{ args.begin(), propsIter }, &tests);

  cmMakefile* mf = &status.GetMakefile();
  if (result.MaybeReportError(*mf)) {
    return false;
  }
  if (!directory.empty()) {
    std::string absDirectory = cmSystemTools::CollapseFullPath(
      directory, mf->GetCurrentSourceDirectory());
    mf = mf->GetGlobalGenerator()->FindMakefile(absDirectory);
    if (!mf) {
      status.SetError(cmStrCat("given non-existent DIRECTORY ", directory));
      return false;
    }
  }

  // loop over all the tests
  for (std::string const& tname : tests) {
    if (cmTest* test = mf->GetTest(tname)) {
      // loop through all the props and set them
      for (auto k = propsIter + 1; k != args.end(); k += 2) {
        if (!k->empty()) {
          test->SetProperty(*k, *(k + 1));
        }
      }
    } else {
      status.SetError(
        cmStrCat("Can not find test to add properties to: ", tname));
      return false;
    }
  }
  return true;
}
