/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file LICENSE.rst or https://cmake.org/licensing for details.  */
#include "cmNinjaNormalTargetGenerator.h"

#include <algorithm>
#include <cassert>
#include <iterator>
#include <set>
#include <sstream>
#include <unordered_set>
#include <utility>

#include <cm/memory>
#include <cm/optional>
#include <cm/vector>

#include "cmComputeLinkInformation.h"
#include "cmCustomCommand.h" // IWYU pragma: keep
#include "cmCustomCommandGenerator.h"
#include "cmGeneratedFileStream.h"
#include "cmGeneratorExpression.h"
#include "cmGeneratorOptions.h"
#include "cmGeneratorTarget.h"
#include "cmGlobalNinjaGenerator.h"
#include "cmLinkLineComputer.h"
#include "cmLinkLineDeviceComputer.h"
#include "cmList.h"
#include "cmLocalCommonGenerator.h"
#include "cmLocalGenerator.h"
#include "cmLocalNinjaGenerator.h"
#include "cmMakefile.h"
#include "cmMessageType.h"
#include "cmNinjaLinkLineDeviceComputer.h"
#include "cmNinjaTypes.h"
#include "cmOSXBundleGenerator.h"
#include "cmOutputConverter.h"
#include "cmRulePlaceholderExpander.h"
#include "cmSourceFile.h"
#include "cmState.h"
#include "cmStateDirectory.h"
#include "cmStateSnapshot.h"
#include "cmStateTypes.h"
#include "cmStringAlgorithms.h"
#include "cmSystemTools.h"
#include "cmValue.h"

cmNinjaNormalTargetGenerator::cmNinjaNormalTargetGenerator(
  cmGeneratorTarget* target)
  : cmNinjaTargetGenerator(target)
{
  if (target->GetType() != cmStateEnums::OBJECT_LIBRARY) {
    // on Windows the output dir is already needed at compile time
    // ensure the directory exists (OutDir test)
    for (auto const& config : this->GetConfigNames()) {
      this->EnsureDirectoryExists(target->GetDirectory(config));
    }
  }

  this->OSXBundleGenerator = cm::make_unique<cmOSXBundleGenerator>(target);
  this->OSXBundleGenerator->SetMacContentFolders(&this->MacContentFolders);
}

cmNinjaNormalTargetGenerator::~cmNinjaNormalTargetGenerator() = default;

void cmNinjaNormalTargetGenerator::Generate(std::string const& config)
{
  if (this->GetGeneratorTarget()->GetType() !=
      cmStateEnums::INTERFACE_LIBRARY) {
    std::string lang = this->GeneratorTarget->GetLinkerLanguage(config);
    if (this->TargetLinkLanguage(config).empty()) {
      cmSystemTools::Error(
        cmStrCat("CMake can not determine linker language for target: ",
                 this->GetGeneratorTarget()->GetName()));
      return;
    }
  }

  // Write the rules for each language.
  this->WriteLanguagesRules(config);

  // Write the build statements
  bool firstForConfig = true;
  for (auto const& fileConfig : this->GetConfigNames()) {
    if (!this->GetGlobalGenerator()
           ->GetCrossConfigs(fileConfig)
           .count(config)) {
      continue;
    }
    this->WriteObjectBuildStatements(config, fileConfig, firstForConfig);
    firstForConfig = false;
  }

  if (this->GetGeneratorTarget()->GetType() == cmStateEnums::OBJECT_LIBRARY) {
    this->WriteObjectLibStatement(config);
  } else if (this->GetGeneratorTarget()->GetType() ==
             cmStateEnums::INTERFACE_LIBRARY) {
    bool haveCxxModuleSources = false;
    if (this->GetGeneratorTarget()->HaveCxx20ModuleSources()) {
      haveCxxModuleSources = true;
    }

    if (!haveCxxModuleSources) {
      cmSystemTools::Error(cmStrCat(
        "Ninja does not support INTERFACE libraries without C++ module "
        "sources as a normal target: ",
        this->GetGeneratorTarget()->GetName()));
      return;
    }

    firstForConfig = true;
    for (auto const& fileConfig : this->GetConfigNames()) {
      if (!this->GetGlobalGenerator()
             ->GetCrossConfigs(fileConfig)
             .count(config)) {
        continue;
      }
      if (haveCxxModuleSources) {
        this->WriteCxxModuleLibraryStatement(config, fileConfig,
                                             firstForConfig);
      }
      firstForConfig = false;
    }
  } else {
    firstForConfig = true;
    for (auto const& fileConfig : this->GetConfigNames()) {
      if (!this->GetGlobalGenerator()
             ->GetCrossConfigs(fileConfig)
             .count(config)) {
        continue;
      }
      // If this target has cuda language link inputs, and we need to do
      // device linking
      this->WriteDeviceLinkStatement(config, fileConfig, firstForConfig);
      this->WriteLinkStatement(config, fileConfig, firstForConfig);
      firstForConfig = false;
    }
  }
  if (this->GetGlobalGenerator()->EnableCrossConfigBuild()) {
    this->GetGlobalGenerator()->AddTargetAlias(
      this->GetTargetName(), this->GetGeneratorTarget(), "all");
  }

  // Find ADDITIONAL_CLEAN_FILES
  this->AdditionalCleanFiles(config);
}

void cmNinjaNormalTargetGenerator::WriteLanguagesRules(
  std::string const& config)
{
#ifdef NINJA_GEN_VERBOSE_FILES
  cmGlobalNinjaGenerator::WriteDivider(this->GetRulesFileStream());
  this->GetRulesFileStream()
    << "# Rules for each language for "
    << cmState::GetTargetTypeName(this->GetGeneratorTarget()->GetType())
    << " target " << this->GetTargetName() << "\n\n";
#endif

  // Write rules for languages compiled in this target.
  {
    std::set<std::string> languages;
    std::vector<cmSourceFile const*> sourceFiles;
    this->GetGeneratorTarget()->GetObjectSources(sourceFiles, config);
    if (this->HaveRequiredLanguages(sourceFiles, languages)) {
      for (std::string const& language : languages) {
        this->WriteLanguageRules(language, config);
      }
    }
  }

  // Write rules for languages in BMI-only rules.
  {
    std::set<std::string> languages;
    std::vector<cmSourceFile const*> sourceFiles;
    this->GetGeneratorTarget()->GetCxxModuleSources(sourceFiles, config);
    if (this->HaveRequiredLanguages(sourceFiles, languages)) {
      for (std::string const& language : languages) {
        this->WriteLanguageRules(language, config);
      }
    }
  }
}

char const* cmNinjaNormalTargetGenerator::GetVisibleTypeName() const
{
  switch (this->GetGeneratorTarget()->GetType()) {
    case cmStateEnums::STATIC_LIBRARY:
      return "static library";
    case cmStateEnums::SHARED_LIBRARY:
      return "shared library";
    case cmStateEnums::MODULE_LIBRARY:
      if (this->GetGeneratorTarget()->IsCFBundleOnApple()) {
        return "CFBundle shared module";
      } else {
        return "shared module";
      }
    case cmStateEnums::EXECUTABLE:
      return "executable";
    default:
      return nullptr;
  }
}

std::string cmNinjaNormalTargetGenerator::LanguageLinkerRule(
  std::string const& config) const
{
  return cmStrCat(
    this->TargetLinkLanguage(config), '_',
    cmState::GetTargetTypeName(this->GetGeneratorTarget()->GetType()),
    "_LINKER__",
    cmGlobalNinjaGenerator::EncodeRuleName(
      this->GetGeneratorTarget()->GetName()),
    '_', config);
}

std::string cmNinjaNormalTargetGenerator::LanguageLinkerDeviceRule(
  std::string const& config) const
{
  return cmStrCat(
    this->TargetLinkLanguage(config), '_',
    cmState::GetTargetTypeName(this->GetGeneratorTarget()->GetType()),
    "_DEVICE_LINKER__",
    cmGlobalNinjaGenerator::EncodeRuleName(
      this->GetGeneratorTarget()->GetName()),
    '_', config);
}

std::string cmNinjaNormalTargetGenerator::LanguageLinkerCudaDeviceRule(
  std::string const& config) const
{
  return cmStrCat(
    this->TargetLinkLanguage(config), "_DEVICE_LINK__",
    cmGlobalNinjaGenerator::EncodeRuleName(this->GeneratorTarget->GetName()),
    '_', config);
}

std::string cmNinjaNormalTargetGenerator::LanguageLinkerCudaDeviceCompileRule(
  std::string const& config) const
{
  return cmStrCat(
    this->TargetLinkLanguage(config), "_DEVICE_LINK_COMPILE__",
    cmGlobalNinjaGenerator::EncodeRuleName(this->GeneratorTarget->GetName()),
    '_', config);
}

std::string cmNinjaNormalTargetGenerator::LanguageLinkerCudaFatbinaryRule(
  std::string const& config) const
{
  return cmStrCat(
    this->TargetLinkLanguage(config), "_FATBINARY__",
    cmGlobalNinjaGenerator::EncodeRuleName(this->GeneratorTarget->GetName()),
    '_', config);
}

std::string cmNinjaNormalTargetGenerator::TextStubsGeneratorRule(
  std::string const& config) const
{
  return cmStrCat(
    "TEXT_STUBS_GENERATOR__",
    cmGlobalNinjaGenerator::EncodeRuleName(this->GeneratorTarget->GetName()),
    '_', config);
}

bool cmNinjaNormalTargetGenerator::CheckUseResponseFileForLibraries(
  std::string const& l) const
{
  // Check for an explicit setting one way or the other.
  std::string const responseVar =
    "CMAKE_" + l + "_USE_RESPONSE_FILE_FOR_LIBRARIES";

  // If the option is defined, read it's value
  if (cmValue val = this->Makefile->GetDefinition(responseVar)) {
    return val.IsOn();
  }

  // Default to true
  return true;
}

struct cmNinjaRemoveNoOpCommands
{
  bool operator()(std::string const& cmd)
  {
    return cmd.empty() || cmd[0] == ':';
  }
};

void cmNinjaNormalTargetGenerator::WriteNvidiaDeviceLinkRule(
  bool useResponseFile, std::string const& config)
{
  cmNinjaRule rule(this->LanguageLinkerDeviceRule(config));
  if (!this->GetGlobalGenerator()->HasRule(rule.Name)) {
    cmRulePlaceholderExpander::RuleVariables vars;
    vars.CMTargetName = this->GetGeneratorTarget()->GetName().c_str();
    vars.CMTargetType =
      cmState::GetTargetTypeName(this->GetGeneratorTarget()->GetType())
        .c_str();
    vars.CMTargetLabels =
      this->GetGeneratorTarget()->GetTargetLabelsString().c_str();

    vars.Language = "CUDA";
    std::string linker =
      this->GetGeneratorTarget()->GetLinkerTool("CUDA", config);
    vars.Linker = linker.c_str();

    // build response file name
    std::string responseFlag = this->GetMakefile()->GetSafeDefinition(
      "CMAKE_CUDA_RESPONSE_FILE_DEVICE_LINK_FLAG");

    if (!useResponseFile || responseFlag.empty()) {
      vars.Objects = "$in";
      vars.LinkLibraries = "$LINK_PATH $LINK_LIBRARIES";
    } else {
      rule.RspFile = "$RSP_FILE";
      responseFlag += rule.RspFile;

      // build response file content
      if (this->GetGlobalGenerator()->IsGCCOnWindows()) {
        rule.RspContent = "$in";
      } else {
        rule.RspContent = "$in_newline";
      }

      // add the link command in the file if necessary
      if (this->CheckUseResponseFileForLibraries("CUDA")) {
        rule.RspContent += " $LINK_LIBRARIES";
        vars.LinkLibraries = "";
      } else {
        vars.LinkLibraries = "$LINK_PATH $LINK_LIBRARIES";
      }

      vars.Objects = responseFlag.c_str();
    }

    vars.ObjectDir = "$OBJECT_DIR";
    vars.TargetSupportDir = "$TARGET_SUPPORT_DIR";

    vars.Target = "$TARGET_FILE";

    vars.SONameFlag = "$SONAME_FLAG";
    vars.TargetSOName = "$SONAME";
    vars.TargetPDB = "$TARGET_PDB";
    vars.TargetCompilePDB = "$TARGET_COMPILE_PDB";

    vars.Flags = "$FLAGS";
    vars.LinkFlags = "$LINK_FLAGS";
    vars.Manifests = "$MANIFESTS";
    vars.Config = "$CONFIG";

    vars.LanguageCompileFlags = "$LANGUAGE_COMPILE_FLAGS";

    std::string launcher;
    std::string val = this->GetLocalGenerator()->GetRuleLauncher(
      this->GetGeneratorTarget(), "RULE_LAUNCH_LINK", config);
    if (cmNonempty(val)) {
      launcher = cmStrCat(val, ' ');
    }

    auto rulePlaceholderExpander =
      this->GetLocalGenerator()->CreateRulePlaceholderExpander(
        cmBuildStep::Link);

    // Rule for linking library/executable.
    std::vector<std::string> linkCmds = this->ComputeDeviceLinkCmd();
    for (std::string& linkCmd : linkCmds) {
      linkCmd = cmStrCat(launcher, linkCmd);
      rulePlaceholderExpander->ExpandRuleVariables(this->GetLocalGenerator(),
                                                   linkCmd, vars);
    }

    // If there is no ranlib the command will be ":".  Skip it.
    cm::erase_if(linkCmds, cmNinjaRemoveNoOpCommands());

    rule.Command =
      this->GetLocalGenerator()->BuildCommandLine(linkCmds, config, config);

    // Write the linker rule with response file if needed.
    rule.Comment =
      cmStrCat("Rule for linking ", this->TargetLinkLanguage(config), ' ',
               this->GetVisibleTypeName(), '.');
    rule.Description =
      cmStrCat("Linking ", this->TargetLinkLanguage(config), ' ',
               this->GetVisibleTypeName(), " $TARGET_FILE");
    rule.Restat = "$RESTAT";

    this->GetGlobalGenerator()->AddRule(rule);
  }
}

void cmNinjaNormalTargetGenerator::WriteDeviceLinkRules(
  std::string const& config)
{
  cmMakefile const* mf = this->GetMakefile();

  cmNinjaRule rule(this->LanguageLinkerCudaDeviceRule(config));
  rule.Command = this->GetLocalGenerator()->BuildCommandLine(
    { cmStrCat(mf->GetRequiredDefinition("CMAKE_CUDA_DEVICE_LINKER"),
               " -arch=$ARCH $REGISTER -o=$out $in") },
    config, config);
  rule.Comment = "Rule for CUDA device linking.";
  rule.Description = "Linking CUDA $out";
  this->GetGlobalGenerator()->AddRule(rule);

  cmRulePlaceholderExpander::RuleVariables vars;
  vars.CMTargetName = this->GetGeneratorTarget()->GetName().c_str();
  vars.CMTargetType =
    cmState::GetTargetTypeName(this->GetGeneratorTarget()->GetType()).c_str();
  vars.CMTargetLabels =
    this->GetGeneratorTarget()->GetTargetLabelsString().c_str();

  vars.Language = "CUDA";
  vars.Object = "$out";
  vars.Fatbinary = "$FATBIN";
  vars.RegisterFile = "$REGISTER";
  vars.LinkFlags = "$LINK_FLAGS";
  std::string linker =
    this->GetGeneratorTarget()->GetLinkerTool("CUDA", config);
  vars.Linker = linker.c_str();

  std::string flags = this->GetFlags("CUDA", config);
  vars.Flags = flags.c_str();
  vars.Config = "$CONFIG";

  std::string compileCmd = this->GetMakefile()->GetRequiredDefinition(
    "CMAKE_CUDA_DEVICE_LINK_COMPILE");
  auto rulePlaceholderExpander =
    this->GetLocalGenerator()->CreateRulePlaceholderExpander(
      cmBuildStep::Link);
  rulePlaceholderExpander->ExpandRuleVariables(this->GetLocalGenerator(),
                                               compileCmd, vars);

  rule.Name = this->LanguageLinkerCudaDeviceCompileRule(config);
  rule.Command = this->GetLocalGenerator()->BuildCommandLine({ compileCmd },
                                                             config, config);
  rule.Comment = "Rule for compiling CUDA device stubs.";
  rule.Description = "Compiling CUDA device stub $out";
  this->GetGlobalGenerator()->AddRule(rule);

  rule.Name = this->LanguageLinkerCudaFatbinaryRule(config);
  rule.Command = this->GetLocalGenerator()->BuildCommandLine(
    { cmStrCat(mf->GetRequiredDefinition("CMAKE_CUDA_FATBINARY"),
               " -64 -cmdline=--compile-only -compress-all -link "
               "--embedded-fatbin=$out $PROFILES") },
    config, config);
  rule.Comment = "Rule for CUDA fatbinaries.";
  rule.Description = "Creating fatbinary $out";
  this->GetGlobalGenerator()->AddRule(rule);
}

void cmNinjaNormalTargetGenerator::WriteLinkRule(
  bool useResponseFile, std::string const& config,
  std::vector<std::string> const& preLinkComments,
  std::vector<std::string> const& postBuildComments)
{
  cmStateEnums::TargetType targetType = this->GetGeneratorTarget()->GetType();

  std::string linkRuleName = this->LanguageLinkerRule(config);
  if (!this->GetGlobalGenerator()->HasRule(linkRuleName)) {
    cmNinjaRule rule(std::move(linkRuleName));
    cmRulePlaceholderExpander::RuleVariables vars;
    vars.CMTargetName = this->GetGeneratorTarget()->GetName().c_str();
    vars.CMTargetType = cmState::GetTargetTypeName(targetType).c_str();
    vars.CMTargetLabels =
      this->GetGeneratorTarget()->GetTargetLabelsString().c_str();

    std::string linker = this->GetGeneratorTarget()->GetLinkerTool(config);
    vars.Linker = linker.c_str();
    std::string lang = this->TargetLinkLanguage(config);
    vars.Language = lang.c_str();
    vars.AIXExports = "$AIX_EXPORTS";

    if (!this->GetLocalGenerator()->IsSplitSwiftBuild() &&
        this->TargetLinkLanguage(config) == "Swift") {
      vars.SwiftLibraryName = "$SWIFT_LIBRARY_NAME";
      vars.SwiftModule = "$SWIFT_MODULE";
      vars.SwiftModuleName = "$SWIFT_MODULE_NAME";
      vars.SwiftSources = "$SWIFT_SOURCES";

      vars.Defines = "$DEFINES";
      vars.Flags = "$FLAGS";
      vars.Includes = "$INCLUDES";
    }

    std::string responseFlag;

    std::string cmakeVarLang =
      cmStrCat("CMAKE_", this->TargetLinkLanguage(config));

    if (this->GeneratorTarget->HasLinkDependencyFile(config)) {
      auto DepFileFormat = this->GetMakefile()->GetDefinition(
        cmStrCat(cmakeVarLang, "_LINKER_DEPFILE_FORMAT"));
      rule.DepType = DepFileFormat;
      rule.DepFile = "$DEP_FILE";
    }

    // build response file name
    std::string cmakeLinkVar = cmakeVarLang + "_RESPONSE_FILE_LINK_FLAG";
    cmValue flag = this->GetMakefile()->GetDefinition(cmakeLinkVar);

    if (flag) {
      responseFlag = *flag;
    } else {
      responseFlag = "@";
    }

    if (!useResponseFile || responseFlag.empty()) {
      vars.Objects = "$in";
      vars.LinkLibraries = "$LINK_PATH $LINK_LIBRARIES";
    } else {
      rule.RspFile = "$RSP_FILE";
      responseFlag += rule.RspFile;

      // build response file content
      if (this->GetGlobalGenerator()->IsGCCOnWindows()) {
        rule.RspContent = "$in";
      } else {
        rule.RspContent = "$in_newline";
      }

      // If libraries in rsp is enable
      if (this->CheckUseResponseFileForLibraries(lang)) {
        rule.RspContent += " $LINK_PATH $LINK_LIBRARIES";
        vars.LinkLibraries = "";
      } else {
        vars.LinkLibraries = "$LINK_PATH $LINK_LIBRARIES";
      }

      if (!this->GetLocalGenerator()->IsSplitSwiftBuild() &&
          this->TargetLinkLanguage(config) == "Swift") {
        vars.SwiftSources = responseFlag.c_str();
      } else {
        vars.Objects = responseFlag.c_str();
      }
    }

    vars.ObjectDir = "$OBJECT_DIR";
    vars.TargetSupportDir = "$TARGET_SUPPORT_DIR";

    vars.Target = "$TARGET_FILE";

    vars.SONameFlag = "$SONAME_FLAG";
    vars.TargetSOName = "$SONAME";
    vars.TargetInstallNameDir = "$INSTALLNAME_DIR";
    vars.TargetPDB = "$TARGET_PDB";

    // Setup the target version.
    std::string targetVersionMajor;
    std::string targetVersionMinor;
    {
      std::ostringstream majorStream;
      std::ostringstream minorStream;
      int major;
      int minor;
      this->GetGeneratorTarget()->GetTargetVersion(major, minor);
      majorStream << major;
      minorStream << minor;
      targetVersionMajor = majorStream.str();
      targetVersionMinor = minorStream.str();
    }
    vars.TargetVersionMajor = targetVersionMajor.c_str();
    vars.TargetVersionMinor = targetVersionMinor.c_str();

    vars.Flags = "$FLAGS";
    vars.LinkFlags = "$LINK_FLAGS";
    vars.Manifests = "$MANIFESTS";
    vars.Config = "$CONFIG";

    std::string langFlags;
    if (targetType != cmStateEnums::EXECUTABLE) {
      langFlags += "$LANGUAGE_COMPILE_FLAGS $ARCH_FLAGS";
      vars.LanguageCompileFlags = langFlags.c_str();
    }

    std::string linkerLauncher = this->GetLinkerLauncher(config);
    if (cmNonempty(linkerLauncher)) {
      vars.Launcher = linkerLauncher.c_str();
    }

    std::string launcher;
    std::string val = this->GetLocalGenerator()->GetRuleLauncher(
      this->GetGeneratorTarget(), "RULE_LAUNCH_LINK", config);
    if (cmNonempty(val)) {
      launcher = cmStrCat(val, ' ');
    }

    auto rulePlaceholderExpander =
      this->GetLocalGenerator()->CreateRulePlaceholderExpander(
        cmBuildStep::Link);

    // Rule for linking library/executable.
    std::vector<std::string> linkCmds = this->ComputeLinkCmd(config);
    for (std::string& linkCmd : linkCmds) {
      linkCmd = cmStrCat(launcher, linkCmd);
      rulePlaceholderExpander->ExpandRuleVariables(this->GetLocalGenerator(),
                                                   linkCmd, vars);
    }

    // If there is no ranlib the command will be ":".  Skip it.
    cm::erase_if(linkCmds, cmNinjaRemoveNoOpCommands());

    linkCmds.insert(linkCmds.begin(), "$PRE_LINK");
    linkCmds.emplace_back("$POST_BUILD");
    rule.Command =
      this->GetLocalGenerator()->BuildCommandLine(linkCmds, config, config);

    // Write the linker rule with response file if needed.
    rule.Comment =
      cmStrCat("Rule for linking ", this->TargetLinkLanguage(config), ' ',
               this->GetVisibleTypeName(), '.');
    char const* presep = "";
    char const* postsep = "";
    auto prelink = cmJoin(preLinkComments, "; ");
    if (!prelink.empty()) {
      presep = "; ";
    }
    auto postbuild = cmJoin(postBuildComments, "; ");
    if (!postbuild.empty()) {
      postsep = "; ";
    }
    rule.Description = cmStrCat(
      prelink, presep, "Linking ", this->TargetLinkLanguage(config), ' ',
      this->GetVisibleTypeName(), " $TARGET_FILE", postsep, postbuild);
    rule.Restat = "$RESTAT";
    this->GetGlobalGenerator()->AddRule(rule);
  }

  auto const tgtNames = this->TargetNames(config);
  if (tgtNames.Output != tgtNames.Real &&
      !this->GetGeneratorTarget()->IsFrameworkOnApple()) {
    std::string cmakeCommand =
      this->GetLocalGenerator()->ConvertToOutputFormat(
        cmSystemTools::GetCMakeCommand(), cmOutputConverter::SHELL);
    if (targetType == cmStateEnums::EXECUTABLE) {
      cmNinjaRule rule("CMAKE_SYMLINK_EXECUTABLE");
      {
        std::vector<std::string> cmd;
        cmd.push_back(cmakeCommand + " -E cmake_symlink_executable $in $out");
        cmd.emplace_back("$POST_BUILD");
        rule.Command =
          this->GetLocalGenerator()->BuildCommandLine(cmd, config, config);
      }
      rule.Description = "Creating executable symlink $out";
      rule.Comment = "Rule for creating executable symlink.";
      this->GetGlobalGenerator()->AddRule(rule);
    } else {
      cmNinjaRule rule("CMAKE_SYMLINK_LIBRARY");
      {
        std::vector<std::string> cmd;
        cmd.push_back(cmakeCommand +
                      " -E cmake_symlink_library $in $SONAME $out");
        cmd.emplace_back("$POST_BUILD");
        rule.Command =
          this->GetLocalGenerator()->BuildCommandLine(cmd, config, config);
      }
      rule.Description = "Creating library symlink $out";
      rule.Comment = "Rule for creating library symlink.";
      this->GetGlobalGenerator()->AddRule(rule);
    }
  }

  if (this->GetGeneratorTarget()->IsApple() &&
      this->GetGeneratorTarget()->HasImportLibrary(config)) {
    cmNinjaRule rule(this->TextStubsGeneratorRule(config));
    rule.Comment = cmStrCat("Rule for generating text-based stubs for ",
                            this->GetVisibleTypeName(), '.');
    rule.Description = "Creating text-based stubs $out";

    std::string cmd =
      this->GetMakefile()->GetDefinition("CMAKE_CREATE_TEXT_STUBS");
    auto rulePlaceholderExpander =
      this->GetLocalGenerator()->CreateRulePlaceholderExpander();
    cmRulePlaceholderExpander::RuleVariables vars;
    vars.Target = "$in";
    rulePlaceholderExpander->SetTargetImpLib("$out");
    rulePlaceholderExpander->ExpandRuleVariables(this->GetLocalGenerator(),
                                                 cmd, vars);

    rule.Command =
      this->GetLocalGenerator()->BuildCommandLine({ cmd }, config, config);
    this->GetGlobalGenerator()->AddRule(rule);

    if (tgtNames.ImportOutput != tgtNames.ImportReal &&
        !this->GetGeneratorTarget()->IsFrameworkOnApple()) {
      cmNinjaRule slRule("CMAKE_SYMLINK_IMPORT_LIBRARY");
      {
        std::string cmakeCommand =
          this->GetLocalGenerator()->ConvertToOutputFormat(
            cmSystemTools::GetCMakeCommand(), cmOutputConverter::SHELL);
        std::string slCmd =
          cmStrCat(cmakeCommand, " -E cmake_symlink_library $in $SONAME $out");
        slRule.Command = this->GetLocalGenerator()->BuildCommandLine(
          { slCmd }, config, config);
      }
      slRule.Description = "Creating import library symlink $out";
      slRule.Comment = "Rule for creating import library symlink.";
      this->GetGlobalGenerator()->AddRule(slRule);
    }
  }
}

std::vector<std::string> cmNinjaNormalTargetGenerator::ComputeDeviceLinkCmd()
{
  cmList linkCmds;

  // this target requires separable cuda compilation
  // now build the correct command depending on if the target is
  // an executable or a dynamic library.
  switch (this->GetGeneratorTarget()->GetType()) {
    case cmStateEnums::STATIC_LIBRARY:
    case cmStateEnums::SHARED_LIBRARY:
    case cmStateEnums::MODULE_LIBRARY: {
      linkCmds.assign(
        this->GetMakefile()->GetDefinition("CMAKE_CUDA_DEVICE_LINK_LIBRARY"));
    } break;
    case cmStateEnums::EXECUTABLE: {
      linkCmds.assign(this->GetMakefile()->GetDefinition(
        "CMAKE_CUDA_DEVICE_LINK_EXECUTABLE"));
    } break;
    default:
      break;
  }
  return std::move(linkCmds.data());
}

std::vector<std::string> cmNinjaNormalTargetGenerator::ComputeLinkCmd(
  std::string const& config)
{
  cmList linkCmds;
  cmMakefile* mf = this->GetMakefile();
  {
    // If we have a rule variable prefer it. In the case of static libraries
    // this occurs when things like IPO is enabled, and we need to use the
    // CMAKE_<lang>_CREATE_STATIC_LIBRARY_IPO define instead.
    std::string linkCmdVar = this->GetGeneratorTarget()->GetCreateRuleVariable(
      this->TargetLinkLanguage(config), config);
    cmValue linkCmd = mf->GetDefinition(linkCmdVar);
    if (linkCmd) {
      std::string linkCmdStr = *linkCmd;
      if (this->GetGeneratorTarget()->HasImplibGNUtoMS(config)) {
        std::string ruleVar =
          cmStrCat("CMAKE_", this->GeneratorTarget->GetLinkerLanguage(config),
                   "_GNUtoMS_RULE");
        if (cmValue rule = this->Makefile->GetDefinition(ruleVar)) {
          linkCmdStr += *rule;
        }
      }
      linkCmds.assign(linkCmdStr);
      if (this->UseLWYU) {
        cmValue lwyuCheck = mf->GetDefinition("CMAKE_LINK_WHAT_YOU_USE_CHECK");
        if (lwyuCheck) {
          std::string cmakeCommand = cmStrCat(
            this->GetLocalGenerator()->ConvertToOutputFormat(
              cmSystemTools::GetCMakeCommand(), cmLocalGenerator::SHELL),
            " -E __run_co_compile --lwyu=");
          cmakeCommand +=
            this->GetLocalGenerator()->EscapeForShell(*lwyuCheck);

          std::string targetOutputReal =
            this->ConvertToNinjaPath(this->GetGeneratorTarget()->GetFullPath(
              config, cmStateEnums::RuntimeBinaryArtifact,
              /*realname=*/true));
          cmakeCommand += cmStrCat(" --source=", targetOutputReal);
          linkCmds.push_back(std::move(cmakeCommand));
        }
      }
      return std::move(linkCmds.data());
    }
  }
  switch (this->GetGeneratorTarget()->GetType()) {
    case cmStateEnums::STATIC_LIBRARY: {
      // We have archive link commands set. First, delete the existing archive.
      {
        std::string cmakeCommand =
          this->GetLocalGenerator()->ConvertToOutputFormat(
            cmSystemTools::GetCMakeCommand(), cmOutputConverter::SHELL);
        linkCmds.push_back(cmakeCommand + " -E rm -f $TARGET_FILE");
      }
      // TODO: Use ARCHIVE_APPEND for archives over a certain size.
      {
        std::string linkCmdVar = cmStrCat(
          "CMAKE_", this->TargetLinkLanguage(config), "_ARCHIVE_CREATE");

        linkCmdVar = this->GeneratorTarget->GetFeatureSpecificLinkRuleVariable(
          linkCmdVar, this->TargetLinkLanguage(config), config);

        std::string const& linkCmd = mf->GetRequiredDefinition(linkCmdVar);
        linkCmds.append(linkCmd);
      }
      {
        std::string linkCmdVar = cmStrCat(
          "CMAKE_", this->TargetLinkLanguage(config), "_ARCHIVE_FINISH");

        linkCmdVar = this->GeneratorTarget->GetFeatureSpecificLinkRuleVariable(
          linkCmdVar, this->TargetLinkLanguage(config), config);

        std::string const& linkCmd = mf->GetRequiredDefinition(linkCmdVar);
        linkCmds.append(linkCmd);
      }
#ifdef __APPLE__
      // On macOS ranlib truncates the fractional part of the static archive
      // file modification time.  If the archive and at least one contained
      // object file were created within the same second this will make look
      // the archive older than the object file. On subsequent ninja runs this
      // leads to re-archiving and updating dependent targets.
      // As a work-around we touch the archive after ranlib (see #19222).
      {
        std::string cmakeCommand =
          this->GetLocalGenerator()->ConvertToOutputFormat(
            cmSystemTools::GetCMakeCommand(), cmOutputConverter::SHELL);
        linkCmds.push_back(cmakeCommand + " -E touch $TARGET_FILE");
      }
#endif
    } break;
    case cmStateEnums::SHARED_LIBRARY:
    case cmStateEnums::MODULE_LIBRARY:
    case cmStateEnums::EXECUTABLE:
      break;
    default:
      assert(false && "Unexpected target type");
  }
  return std::move(linkCmds.data());
}

void cmNinjaNormalTargetGenerator::WriteDeviceLinkStatement(
  std::string const& config, std::string const& fileConfig,
  bool firstForConfig)
{
  cmGlobalNinjaGenerator* globalGen = this->GetGlobalGenerator();
  if (!globalGen->GetLanguageEnabled("CUDA")) {
    return;
  }

  cmGeneratorTarget* genTarget = this->GetGeneratorTarget();

  bool requiresDeviceLinking = requireDeviceLinking(
    *this->GeneratorTarget, *this->GetLocalGenerator(), config);
  if (!requiresDeviceLinking) {
    return;
  }

  // First and very important step is to make sure while inside this
  // step our link language is set to CUDA
  std::string const& objExt =
    this->Makefile->GetSafeDefinition("CMAKE_CUDA_OUTPUT_EXTENSION");

  std::string targetOutputDir =
    this->GetLocalGenerator()->MaybeRelativeToTopBinDir(
      cmStrCat(genTarget->GetSupportDirectory(),
               globalGen->ConfigDirectory(config), '/'));
  targetOutputDir = globalGen->ExpandCFGIntDir(targetOutputDir, config);

  std::string targetOutputReal =
    this->ConvertToNinjaPath(targetOutputDir + "cmake_device_link" + objExt);

  if (firstForConfig) {
    globalGen->GetByproductsForCleanTarget(config).push_back(targetOutputReal);
  }
  this->DeviceLinkObject = targetOutputReal;

  // Write comments.
  cmGlobalNinjaGenerator::WriteDivider(this->GetCommonFileStream());
  this->GetCommonFileStream()
    << "# Device Link build statements for "
    << cmState::GetTargetTypeName(genTarget->GetType()) << " target "
    << this->GetTargetName() << "\n\n";

  if (this->Makefile->GetSafeDefinition("CMAKE_CUDA_COMPILER_ID") == "Clang") {
    std::string architecturesStr =
      this->GeneratorTarget->GetSafeProperty("CUDA_ARCHITECTURES");

    if (cmIsOff(architecturesStr)) {
      this->Makefile->IssueMessage(MessageType::FATAL_ERROR,
                                   "CUDA_SEPARABLE_COMPILATION on Clang "
                                   "requires CUDA_ARCHITECTURES to be set.");
      return;
    }

    this->WriteDeviceLinkRules(config);
    this->WriteDeviceLinkStatements(config, cmList{ architecturesStr },
                                    targetOutputReal);
  } else {
    this->WriteNvidiaDeviceLinkStatement(config, fileConfig, targetOutputDir,
                                         targetOutputReal);
  }
}

void cmNinjaNormalTargetGenerator::WriteDeviceLinkStatements(
  std::string const& config, std::vector<std::string> const& architectures,
  std::string const& output)
{
  // Ensure there are no duplicates.
  cmNinjaDeps const explicitDeps = [&]() -> std::vector<std::string> {
    std::unordered_set<std::string> depsSet;
    cmNinjaDeps const linkDeps =
      this->ComputeLinkDeps(this->TargetLinkLanguage(config), config, true);
    cmNinjaDeps const objects = this->GetObjects(config);
    depsSet.insert(linkDeps.begin(), linkDeps.end());
    depsSet.insert(objects.begin(), objects.end());

    std::vector<std::string> deps;
    std::copy(depsSet.begin(), depsSet.end(), std::back_inserter(deps));
    return deps;
  }();

  cmGlobalNinjaGenerator* globalGen{ this->GetGlobalGenerator() };
  std::string const objectDir =
    cmStrCat(this->GeneratorTarget->GetSupportDirectory(),
             globalGen->ConfigDirectory(config));
  std::string const ninjaOutputDir = this->ConvertToNinjaPath(objectDir);

  cmNinjaBuild fatbinary(this->LanguageLinkerCudaFatbinaryRule(config));

  // Link device code for each architecture.
  for (std::string const& architectureKind : architectures) {
    // Clang always generates real code, so strip the specifier.
    std::string const architecture =
      architectureKind.substr(0, architectureKind.find('-'));
    std::string const cubin =
      cmStrCat(ninjaOutputDir, "/sm_", architecture, ".cubin");

    cmNinjaBuild dlink(this->LanguageLinkerCudaDeviceRule(config));
    dlink.ExplicitDeps = explicitDeps;
    dlink.Outputs = { cubin };
    dlink.Variables["ARCH"] = cmStrCat("sm_", architecture);

    // The generated register file contains macros that when expanded register
    // the device routines. Because the routines are the same for all
    // architectures the register file will be the same too. Thus generate it
    // only on the first invocation to reduce overhead.
    if (fatbinary.ExplicitDeps.empty()) {
      dlink.Variables["REGISTER"] = cmStrCat(
        "--register-link-binaries=", ninjaOutputDir, "/cmake_cuda_register.h");
    }

    fatbinary.Variables["PROFILES"] +=
      cmStrCat(" -im=profile=sm_", architecture, ",file=", cubin);
    fatbinary.ExplicitDeps.emplace_back(cubin);

    globalGen->WriteBuild(this->GetCommonFileStream(), dlink);
  }

  // Combine all architectures into a single fatbinary.
  fatbinary.Outputs = { cmStrCat(ninjaOutputDir, "/cmake_cuda_fatbin.h") };
  globalGen->WriteBuild(this->GetCommonFileStream(), fatbinary);

  // Compile the stub that registers the kernels and contains the fatbinaries.
  cmLocalNinjaGenerator* localGen{ this->GetLocalGenerator() };
  cmNinjaBuild dcompile(this->LanguageLinkerCudaDeviceCompileRule(config));
  dcompile.Outputs = { output };
  dcompile.ExplicitDeps = { cmStrCat(ninjaOutputDir, "/cmake_cuda_fatbin.h") };
  dcompile.Variables["FATBIN"] = localGen->ConvertToOutputFormat(
    cmStrCat(objectDir, "/cmake_cuda_fatbin.h"), cmOutputConverter::SHELL);
  dcompile.Variables["REGISTER"] = localGen->ConvertToOutputFormat(
    cmStrCat(objectDir, "/cmake_cuda_register.h"), cmOutputConverter::SHELL);

  cmNinjaLinkLineDeviceComputer linkLineComputer(
    localGen, localGen->GetStateSnapshot().GetDirectory(), globalGen);
  linkLineComputer.SetUseNinjaMulti(globalGen->IsMultiConfig());

  // Link libraries and paths are only used during the final executable/library
  // link.
  std::string frameworkPath;
  std::string linkPath;
  std::string linkLibs;
  localGen->GetDeviceLinkFlags(linkLineComputer, config, linkLibs,
                               dcompile.Variables["LINK_FLAGS"], frameworkPath,
                               linkPath, this->GetGeneratorTarget());

  globalGen->WriteBuild(this->GetCommonFileStream(), dcompile);
}

void cmNinjaNormalTargetGenerator::WriteNvidiaDeviceLinkStatement(
  std::string const& config, std::string const& fileConfig,
  std::string const& outputDir, std::string const& output)
{
  cmGeneratorTarget* genTarget = this->GetGeneratorTarget();
  cmGlobalNinjaGenerator* globalGen = this->GetGlobalGenerator();

  std::string targetOutputImplib = this->ConvertToNinjaPath(
    genTarget->GetFullPath(config, cmStateEnums::ImportLibraryArtifact));

  if (config != fileConfig) {
    std::string targetOutputFileConfigDir =
      this->GetLocalGenerator()->MaybeRelativeToTopBinDir(
        cmStrCat(genTarget->GetSupportDirectory(),
                 globalGen->ConfigDirectory(config), '/'));
    targetOutputFileConfigDir =
      globalGen->ExpandCFGIntDir(outputDir, fileConfig);
    if (outputDir == targetOutputFileConfigDir) {
      return;
    }

    if (!genTarget->GetFullName(config, cmStateEnums::ImportLibraryArtifact)
           .empty() &&
        !genTarget
           ->GetFullName(fileConfig, cmStateEnums::ImportLibraryArtifact)
           .empty() &&
        targetOutputImplib ==
          this->ConvertToNinjaPath(genTarget->GetFullPath(
            fileConfig, cmStateEnums::ImportLibraryArtifact))) {
      return;
    }
  }

  // Compute the comment.
  cmNinjaBuild build(this->LanguageLinkerDeviceRule(config));
  build.Comment =
    cmStrCat("Link the ", this->GetVisibleTypeName(), ' ', output);

  cmNinjaVars& vars = build.Variables;

  // Compute outputs.
  build.Outputs.push_back(output);
  // Compute specific libraries to link with.
  build.ExplicitDeps = this->GetObjects(config);
  build.ImplicitDeps =
    this->ComputeLinkDeps(this->TargetLinkLanguage(config), config);

  std::string frameworkPath;
  std::string linkPath;

  std::string createRule =
    genTarget->GetCreateRuleVariable(this->TargetLinkLanguage(config), config);
  cmLocalNinjaGenerator& localGen = *this->GetLocalGenerator();

  vars["TARGET_FILE"] =
    localGen.ConvertToOutputFormat(output, cmOutputConverter::SHELL);

  cmNinjaLinkLineDeviceComputer linkLineComputer(
    this->GetLocalGenerator(),
    this->GetLocalGenerator()->GetStateSnapshot().GetDirectory(), globalGen);
  linkLineComputer.SetUseNinjaMulti(globalGen->IsMultiConfig());

  localGen.GetDeviceLinkFlags(linkLineComputer, config, vars["LINK_LIBRARIES"],
                              vars["LINK_FLAGS"], frameworkPath, linkPath,
                              genTarget);

  this->addPoolNinjaVariable("JOB_POOL_LINK", genTarget, nullptr, vars);

  vars["MANIFESTS"] = this->GetManifests(config);

  vars["LINK_PATH"] = frameworkPath + linkPath;

  // Compute language specific link flags.
  std::string langFlags;
  localGen.AddLanguageFlagsForLinking(langFlags, genTarget, "CUDA", config);
  vars["LANGUAGE_COMPILE_FLAGS"] = langFlags;

  auto const tgtNames = this->TargetNames(config);
  if (genTarget->HasSOName(config) ||
      genTarget->IsArchivedAIXSharedLibrary()) {
    vars["SONAME_FLAG"] =
      this->GetMakefile()->GetSONameFlag(this->TargetLinkLanguage(config));
    vars["SONAME"] = localGen.ConvertToOutputFormat(tgtNames.SharedObject,
                                                    cmOutputConverter::SHELL);
    if (genTarget->GetType() == cmStateEnums::SHARED_LIBRARY) {
      std::string install_dir =
        this->GetGeneratorTarget()->GetInstallNameDirForBuildTree(config);
      if (!install_dir.empty()) {
        vars["INSTALLNAME_DIR"] = localGen.ConvertToOutputFormat(
          install_dir, cmOutputConverter::SHELL);
      }
    }
  }

  if (!tgtNames.ImportLibrary.empty()) {
    std::string const impLibPath = localGen.ConvertToOutputFormat(
      targetOutputImplib, cmOutputConverter::SHELL);
    vars["TARGET_IMPLIB"] = impLibPath;
    this->EnsureParentDirectoryExists(targetOutputImplib);
  }

  std::string const objPath =
    cmStrCat(this->GetGeneratorTarget()->GetSupportDirectory(),
             globalGen->ConfigDirectory(config));

  vars["OBJECT_DIR"] = this->GetLocalGenerator()->ConvertToOutputFormat(
    this->ConvertToNinjaPath(objPath), cmOutputConverter::SHELL);
  this->EnsureDirectoryExists(objPath);

  std::string const targetSupportPath =
    this->GetGeneratorTarget()->GetCMFSupportDirectory();

  vars["TARGET_SUPPORT_DIR"] =
    this->GetLocalGenerator()->ConvertToOutputFormat(
      this->ConvertToNinjaPath(targetSupportPath), cmOutputConverter::SHELL);
  this->EnsureDirectoryExists(targetSupportPath);

  this->SetMsvcTargetPdbVariable(vars, config);

  std::string& linkLibraries = vars["LINK_LIBRARIES"];
  std::string& link_path = vars["LINK_PATH"];
  if (globalGen->IsGCCOnWindows()) {
    // ar.exe can't handle backslashes in rsp files (implicitly used by gcc)
    std::replace(linkLibraries.begin(), linkLibraries.end(), '\\', '/');
    std::replace(link_path.begin(), link_path.end(), '\\', '/');
  }

  // Device linking currently doesn't support response files so
  // do not check if the user has explicitly forced a response file.
  int const commandLineLengthLimit =
    static_cast<int>(cmSystemTools::CalculateCommandLineLengthLimit()) -
    globalGen->GetRuleCmdLength(build.Rule);

  build.RspFile = this->ConvertToNinjaPath(
    cmStrCat("CMakeFiles/", genTarget->GetName(),
             globalGen->IsMultiConfig() ? cmStrCat('.', config) : "", ".rsp"));

  // Gather order-only dependencies.
  this->GetLocalGenerator()->AppendTargetDepends(
    this->GetGeneratorTarget(), build.OrderOnlyDeps, config, config,
    DependOnTargetArtifact);

  // Write the build statement for this target.
  bool usedResponseFile = false;
  globalGen->WriteBuild(this->GetCommonFileStream(), build,
                        commandLineLengthLimit, &usedResponseFile);
  this->WriteNvidiaDeviceLinkRule(usedResponseFile, config);
}

void cmNinjaNormalTargetGenerator::WriteLinkStatement(
  std::string const& config, std::string const& fileConfig,
  bool firstForConfig)
{
  cmMakefile* mf = this->GetMakefile();
  cmGlobalNinjaGenerator* globalGen = this->GetGlobalGenerator();
  cmGeneratorTarget* gt = this->GetGeneratorTarget();

  std::string targetOutput = this->ConvertToNinjaPath(gt->GetFullPath(config));
  std::string targetOutputReal = this->ConvertToNinjaPath(
    gt->GetFullPath(config, cmStateEnums::RuntimeBinaryArtifact,
                    /*realname=*/true));
  std::string targetOutputImplib = this->ConvertToNinjaPath(
    gt->GetFullPath(config, cmStateEnums::ImportLibraryArtifact));

  if (config != fileConfig) {
    if (targetOutput ==
        this->ConvertToNinjaPath(gt->GetFullPath(fileConfig))) {
      return;
    }
    if (targetOutputReal ==
        this->ConvertToNinjaPath(
          gt->GetFullPath(fileConfig, cmStateEnums::RuntimeBinaryArtifact,
                          /*realname=*/true))) {
      return;
    }
    if (!gt->GetFullName(config, cmStateEnums::ImportLibraryArtifact)
           .empty() &&
        !gt->GetFullName(fileConfig, cmStateEnums::ImportLibraryArtifact)
           .empty() &&
        targetOutputImplib ==
          this->ConvertToNinjaPath(gt->GetFullPath(
            fileConfig, cmStateEnums::ImportLibraryArtifact))) {
      return;
    }
  }

  auto const tgtNames = this->TargetNames(config);
  if (gt->IsAppBundleOnApple()) {
    // Create the app bundle
    std::string outpath = gt->GetDirectory(config);
    this->OSXBundleGenerator->CreateAppBundle(tgtNames.Output, outpath,
                                              config);

    // Calculate the output path
    targetOutput = cmStrCat(outpath, '/', tgtNames.Output);
    targetOutput = this->ConvertToNinjaPath(targetOutput);
    targetOutputReal = cmStrCat(outpath, '/', tgtNames.Real);
    targetOutputReal = this->ConvertToNinjaPath(targetOutputReal);
  } else if (gt->IsFrameworkOnApple()) {
    // Create the library framework.

    cmOSXBundleGenerator::SkipParts bundleSkipParts;
    if (globalGen->GetName() == "Ninja Multi-Config") {
      auto const postFix = this->GeneratorTarget->GetFilePostfix(config);
      // Skip creating Info.plist when there are multiple configurations, and
      // the current configuration has a postfix. The non-postfix configuration
      // Info.plist can be used by all the other configurations.
      if (!postFix.empty()) {
        bundleSkipParts.InfoPlist = true;
      }
    }
    if (gt->HasImportLibrary(config)) {
      bundleSkipParts.TextStubs = false;
    }

    this->OSXBundleGenerator->CreateFramework(
      tgtNames.Output, gt->GetDirectory(config), config, bundleSkipParts);
  } else if (gt->IsCFBundleOnApple()) {
    // Create the core foundation bundle.
    this->OSXBundleGenerator->CreateCFBundle(tgtNames.Output,
                                             gt->GetDirectory(config), config);
  }

  // Write comments.
  cmGlobalNinjaGenerator::WriteDivider(this->GetImplFileStream(fileConfig));
  cmStateEnums::TargetType const targetType = gt->GetType();
  this->GetImplFileStream(fileConfig)
    << "# Link build statements for " << cmState::GetTargetTypeName(targetType)
    << " target " << this->GetTargetName() << "\n\n";

  cmNinjaBuild linkBuild(this->LanguageLinkerRule(config));
  cmNinjaVars& vars = linkBuild.Variables;

  if (this->GeneratorTarget->HasLinkDependencyFile(config)) {
    this->AddDepfileBinding(vars,
                            this->ConvertToNinjaPath(
                              this->GetLocalGenerator()->GetLinkDependencyFile(
                                this->GeneratorTarget, config)));
  }

  // Compute the comment.
  linkBuild.Comment =
    cmStrCat("Link the ", this->GetVisibleTypeName(), ' ', targetOutputReal);

  // Compute outputs.
  linkBuild.Outputs.push_back(targetOutputReal);
  if (firstForConfig) {
    globalGen->GetByproductsForCleanTarget(config).push_back(targetOutputReal);
  }

  // If we can't split the Swift build model (CMP0157 is OLD or unset), fall
  // back on the old one-step "build/link" logic.
  if (!this->GetLocalGenerator()->IsSplitSwiftBuild() &&
      this->TargetLinkLanguage(config) == "Swift") {
    vars["SWIFT_LIBRARY_NAME"] = [this, config]() -> std::string {
      cmGeneratorTarget::Names targetNames =
        this->GetGeneratorTarget()->GetLibraryNames(config);
      return targetNames.Base;
    }();

    vars["SWIFT_MODULE_NAME"] = gt->GetSwiftModuleName();
    vars["SWIFT_MODULE"] = this->GetLocalGenerator()->ConvertToOutputFormat(
      this->ConvertToNinjaPath(gt->GetSwiftModulePath(config)),
      cmOutputConverter::SHELL);

    vars["SWIFT_SOURCES"] = [this, config]() -> std::string {
      std::vector<cmSourceFile const*> sourceFiles;
      std::stringstream oss;

      this->GetGeneratorTarget()->GetObjectSources(sourceFiles, config);
      cmLocalGenerator const* LocalGen = this->GetLocalGenerator();
      for (auto const& source : sourceFiles) {
        std::string const sourcePath = source->GetLanguage() == "Swift"
          ? this->GetCompiledSourceNinjaPath(source)
          : this->GetObjectFilePath(source, config);
        oss << " "
            << LocalGen->ConvertToOutputFormat(sourcePath,
                                               cmOutputConverter::SHELL);
      }
      return oss.str();
    }();

    // Since we do not perform object builds, compute the
    // defines/flags/includes here so that they can be passed along
    // appropriately.
    vars["DEFINES"] = this->GetDefines("Swift", config);
    vars["FLAGS"] = this->GetFlags("Swift", config);
    vars["INCLUDES"] = this->GetIncludes("Swift", config);
    this->GenerateSwiftOutputFileMap(config, vars["FLAGS"]);

    // Compute specific libraries to link with.
    std::vector<cmSourceFile const*> sources;
    gt->GetObjectSources(sources, config);
    for (auto const& source : sources) {
      if (source->GetLanguage() == "Swift") {
        linkBuild.Outputs.push_back(
          this->ConvertToNinjaPath(this->GetObjectFilePath(source, config)));
        linkBuild.ExplicitDeps.emplace_back(
          this->GetCompiledSourceNinjaPath(source));
      } else {
        linkBuild.ExplicitDeps.emplace_back(
          this->GetObjectFilePath(source, config));
      }
    }
    if (targetType != cmStateEnums::EXECUTABLE ||
        gt->IsExecutableWithExports()) {
      linkBuild.Outputs.push_back(vars["SWIFT_MODULE"]);
    }
  } else {
    linkBuild.ExplicitDeps = this->GetObjects(config);
  }

  std::vector<std::string> extraISPCObjects =
    this->GetGeneratorTarget()->GetGeneratedISPCObjects(config);
  std::transform(extraISPCObjects.begin(), extraISPCObjects.end(),
                 std::back_inserter(linkBuild.ExplicitDeps),
                 this->MapToNinjaPath());

  linkBuild.ImplicitDeps =
    this->ComputeLinkDeps(this->TargetLinkLanguage(config), config);

  if (!this->DeviceLinkObject.empty()) {
    linkBuild.ExplicitDeps.push_back(this->DeviceLinkObject);
  }

  std::string frameworkPath;
  std::string linkPath;

  std::string createRule =
    gt->GetCreateRuleVariable(this->TargetLinkLanguage(config), config);
  bool useWatcomQuote = mf->IsOn(createRule + "_USE_WATCOM_QUOTE");
  cmLocalNinjaGenerator& localGen = *this->GetLocalGenerator();

  vars["TARGET_FILE"] =
    localGen.ConvertToOutputFormat(targetOutputReal, cmOutputConverter::SHELL);

  std::unique_ptr<cmLinkLineComputer> linkLineComputer =
    globalGen->CreateLinkLineComputer(
      this->GetLocalGenerator(),
      this->GetLocalGenerator()->GetStateSnapshot().GetDirectory());
  linkLineComputer->SetUseWatcomQuote(useWatcomQuote);
  linkLineComputer->SetUseNinjaMulti(globalGen->IsMultiConfig());

  localGen.GetTargetFlags(linkLineComputer.get(), config,
                          vars["LINK_LIBRARIES"], vars["FLAGS"],
                          vars["LINK_FLAGS"], frameworkPath, linkPath, gt);

  localGen.AppendDependencyInfoLinkerFlags(vars["LINK_FLAGS"], gt, config,
                                           this->TargetLinkLanguage(config));

  // Add OS X version flags, if any.
  if (this->GeneratorTarget->GetType() == cmStateEnums::SHARED_LIBRARY ||
      this->GeneratorTarget->GetType() == cmStateEnums::MODULE_LIBRARY) {
    this->AppendOSXVerFlag(vars["LINK_FLAGS"],
                           this->TargetLinkLanguage(config), "COMPATIBILITY",
                           true);
    this->AppendOSXVerFlag(vars["LINK_FLAGS"],
                           this->TargetLinkLanguage(config), "CURRENT", false);
  }

  this->addPoolNinjaVariable("JOB_POOL_LINK", gt, nullptr, vars);

  this->UseLWYU = this->GetLocalGenerator()->AppendLWYUFlags(
    vars["LINK_FLAGS"], this->GetGeneratorTarget(),
    this->TargetLinkLanguage(config));

  vars["MANIFESTS"] = this->GetManifests(config);
  vars["AIX_EXPORTS"] = this->GetAIXExports(config);

  vars["LINK_PATH"] = frameworkPath + linkPath;
  vars["CONFIG"] = config;

  // Compute architecture specific link flags.  Yes, these go into a different
  // variable for executables, probably due to a mistake made when duplicating
  // code between the Makefile executable and library generators.
  if (targetType == cmStateEnums::EXECUTABLE) {
    std::string t = vars["FLAGS"];
    localGen.AddArchitectureFlags(t, gt, this->TargetLinkLanguage(config),
                                  config);
    vars["FLAGS"] = t;
  } else {
    std::string t = vars["ARCH_FLAGS"];
    localGen.AddArchitectureFlags(t, gt, this->TargetLinkLanguage(config),
                                  config);
    vars["ARCH_FLAGS"] = t;
    t.clear();
    localGen.AddLanguageFlagsForLinking(
      t, gt, this->TargetLinkLanguage(config), config);
    vars["LANGUAGE_COMPILE_FLAGS"] = t;
  }
  if (gt->HasSOName(config) || gt->IsArchivedAIXSharedLibrary()) {
    vars["SONAME_FLAG"] = mf->GetSONameFlag(this->TargetLinkLanguage(config));
    vars["SONAME"] = localGen.ConvertToOutputFormat(tgtNames.SharedObject,
                                                    cmOutputConverter::SHELL);
    if (targetType == cmStateEnums::SHARED_LIBRARY) {
      std::string install_dir = gt->GetInstallNameDirForBuildTree(config);
      if (!install_dir.empty()) {
        vars["INSTALLNAME_DIR"] = localGen.ConvertToOutputFormat(
          install_dir, cmOutputConverter::SHELL);
      }
    }
  }

  cmGlobalNinjaGenerator::CCOutputs byproducts(this->GetGlobalGenerator());

  if (!gt->IsApple() && !tgtNames.ImportLibrary.empty()) {
    std::string const impLibPath = localGen.ConvertToOutputFormat(
      targetOutputImplib, cmOutputConverter::SHELL);
    vars["TARGET_IMPLIB"] = impLibPath;
    this->EnsureParentDirectoryExists(targetOutputImplib);
    if (gt->HasImportLibrary(config)) {
      // Some linkers may update a binary without touching its import lib.
      byproducts.ExplicitOuts.emplace_back(targetOutputImplib);
      if (firstForConfig) {
        globalGen->GetByproductsForCleanTarget(config).push_back(
          targetOutputImplib);
      }
    }
  }

  if (!this->SetMsvcTargetPdbVariable(vars, config)) {
    // It is common to place debug symbols at a specific place,
    // so we need a plain target name in the rule available.
    cmGeneratorTarget::NameComponents const& components =
      gt->GetFullNameComponents(config);
    std::string dbg_suffix = ".dbg";
    // TODO: Where to document?
    if (cmValue d = mf->GetDefinition("CMAKE_DEBUG_SYMBOL_SUFFIX")) {
      dbg_suffix = *d;
    }
    vars["TARGET_PDB"] = components.base + components.suffix + dbg_suffix;
  }

  std::string const objPath =
    cmStrCat(gt->GetSupportDirectory(), globalGen->ConfigDirectory(config));
  vars["OBJECT_DIR"] = this->GetLocalGenerator()->ConvertToOutputFormat(
    this->ConvertToNinjaPath(objPath), cmOutputConverter::SHELL);
  this->EnsureDirectoryExists(objPath);

  std::string const targetSupportPath = gt->GetCMFSupportDirectory();
  vars["TARGET_SUPPORT_DIR"] =
    this->GetLocalGenerator()->ConvertToOutputFormat(
      this->ConvertToNinjaPath(targetSupportPath), cmOutputConverter::SHELL);
  this->EnsureDirectoryExists(targetSupportPath);

  std::string& linkLibraries = vars["LINK_LIBRARIES"];
  std::string& link_path = vars["LINK_PATH"];
  if (globalGen->IsGCCOnWindows()) {
    // ar.exe can't handle backslashes in rsp files (implicitly used by gcc)
    std::replace(linkLibraries.begin(), linkLibraries.end(), '\\', '/');
    std::replace(link_path.begin(), link_path.end(), '\\', '/');
  }

  std::vector<cmCustomCommand> const* cmdLists[3] = {
    &gt->GetPreBuildCommands(), &gt->GetPreLinkCommands(),
    &gt->GetPostBuildCommands()
  };

  std::vector<std::string> preLinkComments;
  std::vector<std::string> postBuildComments;

  std::vector<std::string> preLinkCmdLines;
  std::vector<std::string> postBuildCmdLines;

  std::vector<std::string>* cmdComments[3] = { &preLinkComments,
                                               &preLinkComments,
                                               &postBuildComments };
  std::vector<std::string>* cmdLineLists[3] = { &preLinkCmdLines,
                                                &preLinkCmdLines,
                                                &postBuildCmdLines };
  cmGeneratorExpression ge(*this->GetLocalGenerator()->GetCMakeInstance());

  for (unsigned i = 0; i != 3; ++i) {
    for (cmCustomCommand const& cc : *cmdLists[i]) {
      if (config == fileConfig ||
          this->GetLocalGenerator()->HasUniqueByproducts(cc.GetByproducts(),
                                                         cc.GetBacktrace())) {
        cmCustomCommandGenerator ccg(cc, fileConfig, this->GetLocalGenerator(),
                                     true, config);
        localGen.AppendCustomCommandLines(ccg, *cmdLineLists[i]);
        if (cc.GetComment()) {
          auto cge = ge.Parse(cc.GetComment());
          cmdComments[i]->emplace_back(
            cge->Evaluate(this->GetLocalGenerator(), config));
        }
        std::vector<std::string> const& ccByproducts = ccg.GetByproducts();
        byproducts.Add(ccByproducts);
        std::transform(
          ccByproducts.begin(), ccByproducts.end(),
          std::back_inserter(globalGen->GetByproductsForCleanTarget()),
          this->MapToNinjaPath());
      }
    }
  }

  // If we have any PRE_LINK commands, we need to go back to CMAKE_BINARY_DIR
  // for the link commands.
  if (!preLinkCmdLines.empty()) {
    std::string const homeOutDir = localGen.ConvertToOutputFormat(
      localGen.GetBinaryDirectory(), cmOutputConverter::SHELL);
    preLinkCmdLines.push_back("cd " + homeOutDir);
  }

  // maybe create .def file from list of objects
  cmGeneratorTarget::ModuleDefinitionInfo const* mdi =
    gt->GetModuleDefinitionInfo(config);
  if (mdi && mdi->DefFileGenerated) {
    std::string cmakeCommand =
      this->GetLocalGenerator()->ConvertToOutputFormat(
        cmSystemTools::GetCMakeCommand(), cmOutputConverter::SHELL);
    std::string cmd =
      cmStrCat(cmakeCommand, " -E __create_def ",
               this->GetLocalGenerator()->ConvertToOutputFormat(
                 mdi->DefFile, cmOutputConverter::SHELL),
               ' ');
    std::string obj_list_file = mdi->DefFile + ".objs";
    cmd += this->GetLocalGenerator()->ConvertToOutputFormat(
      obj_list_file, cmOutputConverter::SHELL);

    cmValue nm_executable = this->GetMakefile()->GetDefinition("CMAKE_NM");
    if (cmNonempty(nm_executable)) {
      cmd += " --nm=";
      cmd += this->LocalCommonGenerator->ConvertToOutputFormat(
        *nm_executable, cmOutputConverter::SHELL);
    }
    preLinkCmdLines.push_back(std::move(cmd));

    // create a list of obj files for the -E __create_def to read
    cmGeneratedFileStream fout(obj_list_file);

    if (mdi->WindowsExportAllSymbols) {
      cmNinjaDeps objs = this->GetObjects(config);
      for (std::string const& obj : objs) {
        if (cmHasLiteralSuffix(obj, ".obj")) {
          fout << obj << "\n";
        }
      }
    }

    for (cmSourceFile const* src : mdi->Sources) {
      fout << src->GetFullPath() << "\n";
    }
  }

  vars["PRE_LINK"] = localGen.BuildCommandLine(
    preLinkCmdLines, config, fileConfig, "pre-link", this->GeneratorTarget);
  std::string postBuildCmdLine =
    localGen.BuildCommandLine(postBuildCmdLines, config, fileConfig,
                              "post-build", this->GeneratorTarget);

  cmNinjaVars symlinkVars;
  bool const symlinkNeeded =
    (targetOutput != targetOutputReal && !gt->IsFrameworkOnApple() &&
     !gt->IsArchivedAIXSharedLibrary());
  if (!symlinkNeeded) {
    vars["POST_BUILD"] = postBuildCmdLine;
  } else {
    vars["POST_BUILD"] = cmGlobalNinjaGenerator::SHELL_NOOP;
    symlinkVars["POST_BUILD"] = postBuildCmdLine;
  }

  std::string cmakeVarLang =
    cmStrCat("CMAKE_", this->TargetLinkLanguage(config));

  // build response file name
  std::string cmakeLinkVar = cmakeVarLang + "_RESPONSE_FILE_LINK_FLAG";

  cmValue flag = this->GetMakefile()->GetDefinition(cmakeLinkVar);

  bool const lang_supports_response =
    !(this->TargetLinkLanguage(config) == "RC" ||
      (this->TargetLinkLanguage(config) == "CUDA" && !flag));
  int commandLineLengthLimit = -1;
  if (!lang_supports_response || !this->ForceResponseFile()) {
    commandLineLengthLimit =
      static_cast<int>(cmSystemTools::CalculateCommandLineLengthLimit()) -
      globalGen->GetRuleCmdLength(linkBuild.Rule);
  }

  linkBuild.RspFile = this->ConvertToNinjaPath(
    cmStrCat("CMakeFiles/", gt->GetName(),
             globalGen->IsMultiConfig() ? cmStrCat('.', config) : "", ".rsp"));

  // Gather order-only dependencies.
  this->GetLocalGenerator()->AppendTargetDepends(
    gt, linkBuild.OrderOnlyDeps, config, fileConfig, DependOnTargetArtifact);

  // Add order-only dependencies on versioning symlinks of shared libs we link.
  // If our target is not producing a runtime binary, it doesn't need the
  // symlinks (anything that links to the target might, but that consumer will
  // get its own order-only dependency).
  if (!gt->IsDLLPlatform() && gt->IsRuntimeBinary()) {
    if (cmComputeLinkInformation* cli = gt->GetLinkInformation(config)) {
      for (auto const& item : cli->GetItems()) {
        if (item.Target &&
            item.Target->GetType() == cmStateEnums::SHARED_LIBRARY &&
            !item.Target->IsFrameworkOnApple()) {
          std::string const& lib =
            this->ConvertToNinjaPath(item.Target->GetFullPath(config));
          if (std::find(linkBuild.ImplicitDeps.begin(),
                        linkBuild.ImplicitDeps.end(),
                        lib) == linkBuild.ImplicitDeps.end()) {
            linkBuild.OrderOnlyDeps.emplace_back(lib);
          }
        }
      }
    }
  }

  // Add dependencies on swiftmodule files when using the swift linker
  if (this->TargetLinkLanguage(config) == "Swift") {
    if (cmComputeLinkInformation* cli =
          this->GeneratorTarget->GetLinkInformation(config)) {
      for (auto const& dependency : cli->GetItems()) {
        // Both the current target and the linked target must be swift targets
        // in order for there to be a swiftmodule to depend on
        if (dependency.Target &&
            dependency.Target->GetLinkerLanguage(config) == "Swift") {
          std::string swiftmodule = this->ConvertToNinjaPath(
            dependency.Target->GetSwiftModulePath(config));
          linkBuild.ImplicitDeps.emplace_back(swiftmodule);
        }
      }
    }
  }

  // Ninja should restat after linking if and only if there are byproducts.
  vars["RESTAT"] = byproducts.ExplicitOuts.empty() ? "" : "1";

  linkBuild.Outputs.reserve(linkBuild.Outputs.size() +
                            byproducts.ExplicitOuts.size());
  std::move(byproducts.ExplicitOuts.begin(), byproducts.ExplicitOuts.end(),
            std::back_inserter(linkBuild.Outputs));
  linkBuild.WorkDirOuts = std::move(byproducts.WorkDirOuts);

  // Write the build statement for this target.
  bool usedResponseFile = false;
  globalGen->WriteBuild(this->GetImplFileStream(fileConfig), linkBuild,
                        commandLineLengthLimit, &usedResponseFile);
  this->WriteLinkRule(usedResponseFile, config, preLinkComments,
                      postBuildComments);

  if (symlinkNeeded) {
    if (targetType == cmStateEnums::EXECUTABLE) {
      cmNinjaBuild build("CMAKE_SYMLINK_EXECUTABLE");
      build.Comment = "Create executable symlink " + targetOutput;
      build.Outputs.push_back(targetOutput);
      if (firstForConfig) {
        globalGen->GetByproductsForCleanTarget(config).push_back(targetOutput);
      }
      build.ExplicitDeps.push_back(targetOutputReal);
      build.Variables = std::move(symlinkVars);
      globalGen->WriteBuild(this->GetImplFileStream(fileConfig), build);
    } else {
      cmNinjaBuild build("CMAKE_SYMLINK_LIBRARY");
      build.Comment = "Create library symlink " + targetOutput;

      std::string const soName = this->ConvertToNinjaPath(
        this->GetTargetFilePath(tgtNames.SharedObject, config));
      // If one link has to be created.
      if (targetOutputReal == soName || targetOutput == soName) {
        symlinkVars["SONAME"] =
          this->GetLocalGenerator()->ConvertToOutputFormat(
            soName, cmOutputConverter::SHELL);
      } else {
        symlinkVars["SONAME"].clear();
        build.Outputs.push_back(soName);
        if (firstForConfig) {
          globalGen->GetByproductsForCleanTarget(config).push_back(soName);
        }
      }
      build.Outputs.push_back(targetOutput);
      if (firstForConfig) {
        globalGen->GetByproductsForCleanTarget(config).push_back(targetOutput);
      }
      build.ExplicitDeps.push_back(targetOutputReal);
      build.Variables = std::move(symlinkVars);

      globalGen->WriteBuild(this->GetImplFileStream(fileConfig), build);
    }
  }

  // Add aliases for the file name and the target name.
  globalGen->AddTargetAlias(tgtNames.Output, gt, config);
  globalGen->AddTargetAlias(this->GetTargetName(), gt, config);

  if (this->GetGeneratorTarget()->IsApple() &&
      this->GetGeneratorTarget()->HasImportLibrary(config)) {
    auto dirTBD =
      gt->GetDirectory(config, cmStateEnums::ImportLibraryArtifact);
    auto targetTBD =
      this->ConvertToNinjaPath(cmStrCat(dirTBD, '/', tgtNames.ImportReal));
    this->EnsureParentDirectoryExists(targetTBD);
    cmNinjaBuild build(this->TextStubsGeneratorRule(config));
    build.Comment = cmStrCat("Generate the text-based stubs file ", targetTBD);
    build.Outputs.push_back(targetTBD);
    build.ExplicitDeps.push_back(targetOutputReal);
    globalGen->WriteBuild(this->GetImplFileStream(fileConfig), build);

    if (tgtNames.ImportOutput != tgtNames.ImportReal &&
        !this->GetGeneratorTarget()->IsFrameworkOnApple()) {
      auto outputTBD =
        this->ConvertToNinjaPath(cmStrCat(dirTBD, '/', tgtNames.ImportOutput));
      std::string const soNameTBD = this->ConvertToNinjaPath(
        cmStrCat(dirTBD, '/', tgtNames.ImportLibrary));

      cmNinjaBuild slBuild("CMAKE_SYMLINK_IMPORT_LIBRARY");
      slBuild.Comment = cmStrCat("Create import library symlink ", outputTBD);
      cmNinjaVars slVars;

      // If one link has to be created.
      if (targetTBD == soNameTBD || outputTBD == soNameTBD) {
        slVars["SONAME"] = this->GetLocalGenerator()->ConvertToOutputFormat(
          soNameTBD, cmOutputConverter::SHELL);
      } else {
        slVars["SONAME"].clear();
        slBuild.Outputs.push_back(soNameTBD);
        if (firstForConfig) {
          globalGen->GetByproductsForCleanTarget(config).push_back(soNameTBD);
        }
      }
      slBuild.Outputs.push_back(outputTBD);
      if (firstForConfig) {
        globalGen->GetByproductsForCleanTarget(config).push_back(outputTBD);
      }
      slBuild.ExplicitDeps.push_back(targetTBD);
      slBuild.Variables = std::move(slVars);

      globalGen->WriteBuild(this->GetImplFileStream(fileConfig), slBuild);
    }

    // Add alias for the import file name
    globalGen->AddTargetAlias(tgtNames.ImportOutput, gt, config);
  }
}

void cmNinjaNormalTargetGenerator::WriteObjectLibStatement(
  std::string const& config)
{
  // Write a phony output that depends on all object files.
  {
    cmNinjaBuild build("phony");
    build.Comment = "Object library " + this->GetTargetName();
    this->GetLocalGenerator()->AppendTargetOutputs(this->GetGeneratorTarget(),
                                                   build.Outputs, config);
    this->GetLocalGenerator()->AppendTargetOutputs(
      this->GetGeneratorTarget(),
      this->GetGlobalGenerator()->GetByproductsForCleanTarget(config), config);
    build.ExplicitDeps = this->GetObjects(config);
    this->GetGlobalGenerator()->WriteBuild(this->GetCommonFileStream(), build);
  }

  // Add aliases for the target name.
  this->GetGlobalGenerator()->AddTargetAlias(
    this->GetTargetName(), this->GetGeneratorTarget(), config);
}

void cmNinjaNormalTargetGenerator::WriteCxxModuleLibraryStatement(
  std::string const& config, std::string const& /*fileConfig*/,
  bool firstForConfig)
{
  // TODO: How to use `fileConfig` properly?

  // Write a phony output that depends on the scanning output.
  {
    cmNinjaBuild build("phony");
    build.Comment =
      cmStrCat("Imported C++ module library ", this->GetTargetName());
    this->GetLocalGenerator()->AppendTargetOutputs(this->GetGeneratorTarget(),
                                                   build.Outputs, config);
    if (firstForConfig) {
      this->GetLocalGenerator()->AppendTargetOutputs(
        this->GetGeneratorTarget(),
        this->GetGlobalGenerator()->GetByproductsForCleanTarget(config),
        config);
    }
    build.ExplicitDeps.emplace_back(this->GetDyndepFilePath("CXX", config));
    this->GetGlobalGenerator()->WriteBuild(this->GetCommonFileStream(), build);
  }

  // Add aliases for the target name.
  this->GetGlobalGenerator()->AddTargetAlias(
    this->GetTargetName(), this->GetGeneratorTarget(), config);
}

cmGeneratorTarget::Names cmNinjaNormalTargetGenerator::TargetNames(
  std::string const& config) const
{
  if (this->GeneratorTarget->GetType() == cmStateEnums::EXECUTABLE) {
    return this->GeneratorTarget->GetExecutableNames(config);
  }
  return this->GeneratorTarget->GetLibraryNames(config);
}

std::string cmNinjaNormalTargetGenerator::TargetLinkLanguage(
  std::string const& config) const
{
  return this->GeneratorTarget->GetLinkerLanguage(config);
}
