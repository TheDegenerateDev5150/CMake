/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file LICENSE.rst or https://cmake.org/licensing for details.  */
#pragma once

#include "cmConfigure.h" // IWYU pragma: keep

#include <iosfwd>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <cm/optional>

#include "cmAlgorithms.h"
#include "cmListFileCache.h"
#include "cmPolicies.h"
#include "cmStateTypes.h"
#include "cmStringAlgorithms.h"
#include "cmTargetLinkLibraryType.h"
#include "cmValue.h"

class cmCustomCommand;
class cmFileSet;
class cmFindPackageStack;
class cmGlobalGenerator;
class cmInstallTargetGenerator;
class cmMakefile;
class cmPropertyMap;
class cmSourceFile;
class cmTargetExport;
class cmTargetInternals;

enum class cmFileSetVisibility;

/** \class cmTarget
 * \brief Represent a library or executable target loaded from a makefile.
 *
 * cmTarget represents a target loaded from a makefile.
 */
class cmTarget
{
public:
  enum class Visibility
  {
    Normal,
    Generated,
    Imported,
    ImportedGlobally,
    Foreign,
  };

  enum class Origin
  {
    Cps,
    Unknown,
  };

  enum class PerConfig
  {
    Yes,
    No
  };

  cmTarget(std::string const& name, cmStateEnums::TargetType type,
           Visibility vis, cmMakefile* mf, PerConfig perConfig);

  cmTarget(cmTarget const&) = delete;
  cmTarget(cmTarget&&) noexcept;
  ~cmTarget();

  cmTarget& operator=(cmTarget const&) = delete;
  cmTarget& operator=(cmTarget&&) noexcept;

  //! Return the type of target.
  cmStateEnums::TargetType GetType() const;

  //! Set the origin of the target.
  void SetOrigin(Origin origin);

  //! Return the origin of the target.
  Origin GetOrigin() const;

  //! Get the cmMakefile that owns this target.
  cmMakefile* GetMakefile() const;

  //! Return the global generator.
  cmGlobalGenerator* GetGlobalGenerator() const;

  //! Set/Get the name of the target
  std::string const& GetName() const;
  std::string const& GetTemplateName() const;

  //! Get the policy map
  cmPolicies::PolicyMap const& GetPolicyMap() const;

  //! Get policy status
  cmPolicies::PolicyStatus GetPolicyStatus(cmPolicies::PolicyID policy) const;

#define DECLARE_TARGET_POLICY(POLICY)                                         \
  cmPolicies::PolicyStatus GetPolicyStatus##POLICY() const                    \
  {                                                                           \
    return this->GetPolicyStatus(cmPolicies::POLICY);                         \
  }

  CM_FOR_EACH_TARGET_POLICY(DECLARE_TARGET_POLICY)

#undef DECLARE_TARGET_POLICY

  //! Get the list of the PRE_BUILD custom commands for this target
  std::vector<cmCustomCommand> const& GetPreBuildCommands() const;
  void AddPreBuildCommand(cmCustomCommand const& cmd);
  void AddPreBuildCommand(cmCustomCommand&& cmd);

  //! Get the list of the PRE_LINK custom commands for this target
  std::vector<cmCustomCommand> const& GetPreLinkCommands() const;
  void AddPreLinkCommand(cmCustomCommand const& cmd);
  void AddPreLinkCommand(cmCustomCommand&& cmd);

  //! Get the list of the POST_BUILD custom commands for this target
  std::vector<cmCustomCommand> const& GetPostBuildCommands() const;
  void AddPostBuildCommand(cmCustomCommand const& cmd);
  void AddPostBuildCommand(cmCustomCommand&& cmd);

  //! Add sources to the target.
  void AddSources(std::vector<std::string> const& srcs);
  void AddTracedSources(std::vector<std::string> const& srcs);
  cmSourceFile* AddSource(std::string const& src, bool before = false);

  //! how we identify a library, by name and type
  using LibraryID = std::pair<std::string, cmTargetLinkLibraryType>;
  using LinkLibraryVectorType = std::vector<LibraryID>;
  LinkLibraryVectorType const& GetOriginalLinkLibraries() const;

  //! Clear the dependency information recorded for this target, if any.
  void ClearDependencyInformation(cmMakefile& mf) const;

  void AddLinkLibrary(cmMakefile& mf, std::string const& lib,
                      cmTargetLinkLibraryType llt);

  enum TLLSignature
  {
    KeywordTLLSignature,
    PlainTLLSignature
  };
  bool PushTLLCommandTrace(TLLSignature signature,
                           cmListFileContext const& lfc);
  void GetTllSignatureTraces(std::ostream& s, TLLSignature sig) const;

  /**
   * Set the path where this target should be installed. This is relative to
   * INSTALL_PREFIX
   */
  std::string const& GetInstallPath() const;
  void SetInstallPath(std::string const& name);

  /**
   * Set the path where this target (if it has a runtime part) should be
   * installed. This is relative to INSTALL_PREFIX
   */
  std::string const& GetRuntimeInstallPath() const;
  void SetRuntimeInstallPath(std::string const& name);

  /**
   * Get/Set whether there is an install rule for this target.
   */
  bool GetHaveInstallRule() const;
  void SetHaveInstallRule(bool hir);

  void AddInstallGenerator(cmInstallTargetGenerator* g);
  std::vector<cmInstallTargetGenerator*> const& GetInstallGenerators() const;

  /**
   * Get/Set whether this target was auto-created by a generator.
   */
  bool GetIsGeneratorProvided() const;
  void SetIsGeneratorProvided(bool igp);

  /**
   * Add a utility on which this project depends. A utility is an executable
   * name as would be specified to the ADD_EXECUTABLE or UTILITY_SOURCE
   * commands. It is not a full path nor does it have an extension.
   */
  void AddUtility(std::string const& name, bool cross,
                  cmMakefile const* mf = nullptr);
  void AddUtility(BT<std::pair<std::string, bool>> util);

  void AddCodegenDependency(std::string const& name);

  std::set<std::string> const& GetCodegenDeps() const;

  //! Get the utilities used by this target
  std::set<BT<std::pair<std::string, bool>>> const& GetUtilities() const;

  //! Set/Get a property of this target file
  void SetProperty(std::string const& prop, cmValue value);
  void SetProperty(std::string const& prop, std::nullptr_t)
  {
    this->SetProperty(prop, cmValue{ nullptr });
  }
  void SetProperty(std::string const& prop, std::string const& value)
  {
    this->SetProperty(prop, cmValue(value));
  }
  void AppendProperty(
    std::string const& prop, std::string const& value,
    cm::optional<cmListFileBacktrace> const& bt = cm::nullopt,
    bool asString = false);
  //! Might return a nullptr if the property is not set or invalid
  cmValue GetProperty(std::string const& prop) const;
  //! Always returns a valid pointer
  std::string const& GetSafeProperty(std::string const& prop) const;
  bool GetPropertyAsBool(std::string const& prop) const;
  void CheckProperty(std::string const& prop, cmMakefile* context) const;
  cmValue GetComputedProperty(std::string const& prop, cmMakefile& mf) const;
  //! Get all properties
  cmPropertyMap const& GetProperties() const;

  //! Return whether or not the target is for a DLL platform.
  bool IsDLLPlatform() const;

  //! Return whether or not we are targeting AIX.
  bool IsAIX() const;
  //! Return whether or not we are targeting Apple.
  bool IsApple() const;

  bool IsNormal() const;
  bool IsSynthetic() const;
  bool IsImported() const;
  bool IsImportedGloballyVisible() const;
  bool IsForeign() const;
  bool IsPerConfig() const;
  bool IsRuntimeBinary() const;
  bool CanCompileSources() const;

  bool GetMappedConfig(std::string const& desired_config, cmValue& loc,
                       cmValue& imp, std::string& suffix) const;

  //! Return whether this target is an executable with symbol exports enabled.
  bool IsExecutableWithExports() const;

  //! Return whether this target is a shared library with symbol exports
  //! enabled.
  bool IsSharedLibraryWithExports() const;

  //! Return whether this target is a shared library Framework on Apple.
  bool IsFrameworkOnApple() const;

  //! Return whether to archive shared library or not on AIX.
  bool IsArchivedAIXSharedLibrary() const;

  //! Return whether this target is an executable Bundle on Apple.
  bool IsAppBundleOnApple() const;

  //! Return whether this target is a GUI executable on Android.
  bool IsAndroidGuiExecutable() const;

  bool HasKnownObjectFileLocation(std::string* reason = nullptr) const;

  //! Get a backtrace from the creation of the target.
  cmListFileBacktrace const& GetBacktrace() const;

  //! Get a find_package call stack from the creation of the target.
  cmFindPackageStack const& GetFindPackageStack() const;

  void InsertInclude(BT<std::string> const& entry, bool before = false);
  void InsertCompileOption(BT<std::string> const& entry, bool before = false);
  void InsertCompileDefinition(BT<std::string> const& entry);
  void InsertLinkOption(BT<std::string> const& entry, bool before = false);
  void InsertLinkDirectory(BT<std::string> const& entry, bool before = false);
  void InsertPrecompileHeader(BT<std::string> const& entry);

  void AppendBuildInterfaceIncludes();
  void FinalizeTargetConfiguration(cmBTStringRange compileDefinitions);

  std::string GetDebugGeneratorExpressions(std::string const& value,
                                           cmTargetLinkLibraryType llt) const;

  void AddSystemIncludeDirectories(std::set<std::string> const& incs);
  std::set<std::string> const& GetSystemIncludeDirectories() const;

  void AddInstallIncludeDirectories(cmTargetExport const& te,
                                    cmStringRange incs);
  cmStringRange GetInstallIncludeDirectoriesEntries(
    cmTargetExport const& te) const;

  BTs<std::string> const* GetLanguageStandardProperty(
    std::string const& propertyName) const;

  void SetLanguageStandardProperty(std::string const& lang,
                                   std::string const& value,
                                   std::string const& feature);

  cmBTStringRange GetIncludeDirectoriesEntries() const;

  cmBTStringRange GetCompileOptionsEntries() const;

  cmBTStringRange GetCompileFeaturesEntries() const;

  cmBTStringRange GetCompileDefinitionsEntries() const;

  cmBTStringRange GetPrecompileHeadersEntries() const;

  cmBTStringRange GetSourceEntries() const;

  cmBTStringRange GetLinkOptionsEntries() const;

  cmBTStringRange GetLinkDirectoriesEntries() const;

  cmBTStringRange GetLinkImplementationEntries() const;

  cmBTStringRange GetLinkInterfaceEntries() const;
  cmBTStringRange GetLinkInterfaceDirectEntries() const;
  cmBTStringRange GetLinkInterfaceDirectExcludeEntries() const;

  void CopyPolicyStatuses(cmTarget const* tgt);
  void CopyImportedCxxModulesEntries(cmTarget const* tgt);
  void CopyImportedCxxModulesProperties(cmTarget const* tgt);

  cmBTStringRange GetHeaderSetsEntries() const;
  cmBTStringRange GetCxxModuleSetsEntries() const;

  cmBTStringRange GetInterfaceHeaderSetsEntries() const;
  cmBTStringRange GetInterfaceCxxModuleSetsEntries() const;

  std::string ImportedGetFullPath(std::string const& config,
                                  cmStateEnums::ArtifactType artifact) const;

  struct StrictTargetComparison
  {
    bool operator()(cmTarget const* t1, cmTarget const* t2) const;
  };

  cmFileSet const* GetFileSet(std::string const& name) const;
  cmFileSet* GetFileSet(std::string const& name);
  std::pair<cmFileSet*, bool> GetOrCreateFileSet(std::string const& name,
                                                 std::string const& type,
                                                 cmFileSetVisibility vis);

  std::vector<std::string> GetAllFileSetNames() const;
  std::vector<std::string> GetAllInterfaceFileSets() const;

  static std::string GetFileSetsPropertyName(std::string const& type);
  static std::string GetInterfaceFileSetsPropertyName(std::string const& type);

  bool HasFileSets() const;

private:
  // Internal representation details.
  friend class cmGeneratorTarget;

  char const* GetSuffixVariableInternal(
    cmStateEnums::ArtifactType artifact) const;
  char const* GetPrefixVariableInternal(
    cmStateEnums::ArtifactType artifact) const;

  std::unique_ptr<cmTargetInternals> impl;
};
