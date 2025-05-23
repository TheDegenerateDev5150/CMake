/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file LICENSE.rst or https://cmake.org/licensing for details.  */
#pragma once

#include <iosfwd>
#include <string>
#include <vector>

#include "cmInstallGenerator.h"

class cmListFileBacktrace;
class cmLocalGenerator;
class cmInstallRuntimeDependencySet;

class cmInstallGetRuntimeDependenciesGenerator : public cmInstallGenerator
{
public:
  cmInstallGetRuntimeDependenciesGenerator(
    cmInstallRuntimeDependencySet* runtimeDependencySet,
    std::vector<std::string> directories,
    std::vector<std::string> preIncludeRegexes,
    std::vector<std::string> preExcludeRegexes,
    std::vector<std::string> postIncludeRegexes,
    std::vector<std::string> postExcludeRegexes,
    std::vector<std::string> postIncludeFiles,
    std::vector<std::string> postExcludeFiles, std::string libraryComponent,
    std::string frameworkComponent, bool noInstallRPath, char const* depsVar,
    char const* rpathPrefix, std::vector<std::string> const& configurations,
    MessageLevel message, bool exclude_from_all,
    cmListFileBacktrace backtrace);

  bool Compute(cmLocalGenerator* lg) override;

protected:
  void GenerateScript(std::ostream& os) override;

  void GenerateScriptForConfig(std::ostream& os, std::string const& config,
                               Indent indent) override;

private:
  cmInstallRuntimeDependencySet* RuntimeDependencySet;
  std::vector<std::string> Directories;
  std::vector<std::string> PreIncludeRegexes;
  std::vector<std::string> PreExcludeRegexes;
  std::vector<std::string> PostIncludeRegexes;
  std::vector<std::string> PostExcludeRegexes;
  std::vector<std::string> PostIncludeFiles;
  std::vector<std::string> PostExcludeFiles;
  std::string LibraryComponent;
  std::string FrameworkComponent;
  bool NoInstallRPath;
  char const* DepsVar;
  char const* RPathPrefix;
  cmLocalGenerator* LocalGenerator = nullptr;
};
