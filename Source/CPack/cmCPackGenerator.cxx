/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file LICENSE.rst or https://cmake.org/licensing for details.  */
#include "cmCPackGenerator.h"

#include <algorithm>
#include <memory>
#include <utility>

#include <cmext/string_view>

#include "cmsys/FStream.hxx"
#include "cmsys/Glob.hxx"
#include "cmsys/RegularExpression.hxx"

#include "cmCPackComponentGroup.h"
#include "cmCPackLog.h"
#include "cmCryptoHash.h"
#include "cmDuration.h"
#include "cmFSPermissions.h"
#include "cmFileTimes.h"
#include "cmGeneratedFileStream.h"
#include "cmGlobalGenerator.h"
#include "cmList.h"
#include "cmMakefile.h"
#include "cmState.h"
#include "cmStateSnapshot.h"
#include "cmStringAlgorithms.h"
#include "cmSystemTools.h"
#include "cmValue.h"
#include "cmVersion.h"
#include "cmWorkingDirectory.h"
#include "cmXMLSafe.h"
#include "cmake.h"

#if defined(__HAIKU__)
#  include <FindDirectory.h>
#  include <StorageDefs.h>
#endif

cmCPackGenerator::cmCPackGenerator()
{
  this->GeneratorVerbose = cmSystemTools::OUTPUT_NONE;
  this->MakefileMap = nullptr;
  this->Logger = nullptr;
  this->componentPackageMethod = ONE_PACKAGE_PER_GROUP;
}

cmCPackGenerator::~cmCPackGenerator()
{
  this->MakefileMap = nullptr;
}

void cmCPackGenerator::DisplayVerboseOutput(std::string const& msg,
                                            float /*unused*/)
{
  cmCPackLogger(cmCPackLog::LOG_VERBOSE, msg << std::endl);
}

int cmCPackGenerator::PrepareNames()
{
  cmCPackLogger(cmCPackLog::LOG_DEBUG, "Create temp directory." << std::endl);

  // checks CPACK_SET_DESTDIR support
  if (this->IsOn("CPACK_SET_DESTDIR")) {
    if (SETDESTDIR_UNSUPPORTED == this->SupportsSetDestdir()) {
      cmCPackLogger(cmCPackLog::LOG_ERROR,
                    "CPACK_SET_DESTDIR is set to ON but the '"
                      << this->Name << "' generator does NOT support it."
                      << std::endl);
      return 0;
    }
    if (SETDESTDIR_SHOULD_NOT_BE_USED == this->SupportsSetDestdir()) {
      cmCPackLogger(cmCPackLog::LOG_WARNING,
                    "CPACK_SET_DESTDIR is set to ON but it is "
                      << "usually a bad idea to do that with '" << this->Name
                      << "' generator. Use at your own risk." << std::endl);
    }
  }

  // Determine package-directory.
  cmValue pkgDirectory = this->GetOption("CPACK_PACKAGE_DIRECTORY");
  if (!pkgDirectory) {
    cmCPackLogger(cmCPackLog::LOG_ERROR,
                  "CPACK_PACKAGE_DIRECTORY not specified" << std::endl);
    return 0;
  }
  // Determine base-filename of the package.
  cmValue pkgBaseFileName = this->GetOption("CPACK_PACKAGE_FILE_NAME");
  if (!pkgBaseFileName) {
    cmCPackLogger(cmCPackLog::LOG_ERROR,
                  "CPACK_PACKAGE_FILE_NAME not specified" << std::endl);
    return 0;
  }
  // Determine filename of the package.
  if (!this->GetOutputExtension()) {
    cmCPackLogger(cmCPackLog::LOG_ERROR,
                  "No output extension specified" << std::endl);
    return 0;
  }
  std::string pkgFileName =
    cmStrCat(pkgBaseFileName, this->GetOutputExtension());
  // Determine path to the package.
  std::string pkgFilePath = cmStrCat(pkgDirectory, '/', pkgFileName);
  // Determine top-level directory for packaging.
  std::string topDirectory = cmStrCat(pkgDirectory, "/_CPack_Packages/");
  {
    cmValue toplevelTag = this->GetOption("CPACK_TOPLEVEL_TAG");
    if (toplevelTag) {
      topDirectory += cmStrCat(toplevelTag, '/');
    }
  }
  topDirectory += *this->GetOption("CPACK_GENERATOR");
  // Determine temporary packaging-directory.
  std::string tmpDirectory = cmStrCat(topDirectory, '/', pkgBaseFileName);
  // Determine path to temporary package file.
  std::string tmpPkgFilePath = topDirectory + "/" + pkgFileName;

  // Set CPack variables which are not set already.
  this->SetOptionIfNotSet("CPACK_REMOVE_TOPLEVEL_DIRECTORY", "1");
  this->SetOptionIfNotSet("CPACK_TOPLEVEL_DIRECTORY", topDirectory);
  this->SetOptionIfNotSet("CPACK_TEMPORARY_DIRECTORY", tmpDirectory);
  this->SetOptionIfNotSet("CPACK_TEMPORARY_INSTALL_DIRECTORY", tmpDirectory);
  this->SetOptionIfNotSet("CPACK_OUTPUT_FILE_PREFIX", pkgDirectory);
  this->SetOptionIfNotSet("CPACK_OUTPUT_FILE_NAME", pkgFileName);
  this->SetOptionIfNotSet("CPACK_OUTPUT_FILE_PATH", pkgFilePath);
  this->SetOptionIfNotSet("CPACK_TEMPORARY_PACKAGE_FILE_NAME", tmpPkgFilePath);
  this->SetOptionIfNotSet("CPACK_INSTALL_DIRECTORY", this->GetInstallPath());
  this->SetOptionIfNotSet(
    "CPACK_NATIVE_INSTALL_DIRECTORY",
    cmsys::SystemTools::ConvertToOutputPath(this->GetInstallPath()));

  // Determine description of the package and set as CPack variable,
  // if not already set.
  cmCPackLogger(cmCPackLog::LOG_DEBUG,
                "Look for: CPACK_PACKAGE_DESCRIPTION_FILE" << std::endl);
  cmValue descFileName = this->GetOption("CPACK_PACKAGE_DESCRIPTION_FILE");
  if (descFileName && !this->GetOption("CPACK_PACKAGE_DESCRIPTION")) {
    cmCPackLogger(cmCPackLog::LOG_DEBUG,
                  "Look for: " << *descFileName << std::endl);
    if (!cmSystemTools::FileExists(*descFileName)) {
      cmCPackLogger(cmCPackLog::LOG_ERROR,
                    "Cannot find description file name: ["
                      << *descFileName << "]" << std::endl);
      return 0;
    }
    cmsys::ifstream ifs(descFileName->c_str());
    if (!ifs) {
      cmCPackLogger(cmCPackLog::LOG_ERROR,
                    "Cannot open description file name: " << *descFileName
                                                          << std::endl);
      return 0;
    }
    cmCPackLogger(cmCPackLog::LOG_VERBOSE,
                  "Read description file: " << *descFileName << std::endl);
    std::ostringstream ostr;
    std::string line;
    while (ifs && cmSystemTools::GetLineFromStream(ifs, line)) {
      ostr << cmXMLSafe(line) << std::endl;
    }

    this->SetOption("CPACK_PACKAGE_DESCRIPTION", ostr.str());
    cmValue defFileName =
      this->GetOption("CPACK_DEFAULT_PACKAGE_DESCRIPTION_FILE");
    if (defFileName && (*defFileName == *descFileName)) {
      this->SetOption("CPACK_USED_DEFAULT_PACKAGE_DESCRIPTION_FILE", "ON");
    }
  }
  if (!this->GetOption("CPACK_PACKAGE_DESCRIPTION")) {
    cmCPackLogger(
      cmCPackLog::LOG_ERROR,
      "Project description not specified. Please specify "
      "CPACK_PACKAGE_DESCRIPTION or CPACK_PACKAGE_DESCRIPTION_FILE."
        << std::endl);
    return 0;
  }

  // Check algorithm for calculating the checksum of the package.
  cmValue algoSignature = this->GetOption("CPACK_PACKAGE_CHECKSUM");
  if (algoSignature) {
    if (!cmCryptoHash::New(*algoSignature)) {
      cmCPackLogger(cmCPackLog::LOG_ERROR,
                    "Cannot recognize algorithm: " << algoSignature
                                                   << std::endl);
      return 0;
    }
  }

  return 1;
}

int cmCPackGenerator::InstallProject()
{
  cmCPackLogger(cmCPackLog::LOG_OUTPUT, "Install projects" << std::endl);
  this->CleanTemporaryDirectory();

  std::string bareTempInstallDirectory =
    this->GetOption("CPACK_TEMPORARY_DIRECTORY");
  std::string tempInstallDirectory = bareTempInstallDirectory;
  cmValue v = this->GetOption("CPACK_SET_DESTDIR");
  bool setDestDir = v.IsOn() || cmIsInternallyOn(v);
  if (!setDestDir) {
    tempInstallDirectory += this->GetPackagingInstallPrefix();
  }

  int res = 1;
  if (!cmsys::SystemTools::MakeDirectory(bareTempInstallDirectory)) {
    cmCPackLogger(
      cmCPackLog::LOG_ERROR,
      "Problem creating temporary directory: "
        << (!tempInstallDirectory.empty() ? tempInstallDirectory : "(NULL)")
        << std::endl);
    return 0;
  }

  if (setDestDir) {
    std::string destDir = cmStrCat("DESTDIR=", tempInstallDirectory);
    cmSystemTools::PutEnv(destDir);
  } else {
    // Make sure there is no destdir
    cmSystemTools::PutEnv("DESTDIR=");
  }

  // prepare default created directory permissions
  mode_t default_dir_mode_v = 0;
  mode_t* default_dir_mode = nullptr;
  cmValue default_dir_install_permissions =
    this->GetOption("CPACK_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS");
  if (cmNonempty(default_dir_install_permissions)) {
    cmList items{ default_dir_install_permissions };
    for (auto const& arg : items) {
      if (!cmFSPermissions::stringToModeT(arg, default_dir_mode_v)) {
        cmCPackLogger(cmCPackLog::LOG_ERROR,
                      "Invalid permission value '"
                        << arg
                        << "'."
                           " CPACK_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS "
                           "value is invalid."
                        << std::endl);
        return 0;
      }
    }

    default_dir_mode = &default_dir_mode_v;
  }

  // If the CPackConfig file sets CPACK_INSTALL_COMMANDS then run them
  // as listed
  if (!this->InstallProjectViaInstallCommands(setDestDir,
                                              tempInstallDirectory)) {
    return 0;
  }

  // If the CPackConfig file sets CPACK_INSTALL_SCRIPT(S) then run them
  // as listed
  if (!this->InstallProjectViaInstallScript(setDestDir,
                                            tempInstallDirectory)) {
    return 0;
  }

  // If the CPackConfig file sets CPACK_INSTALLED_DIRECTORIES
  // then glob it and copy it to CPACK_TEMPORARY_DIRECTORY
  // This is used in Source packaging
  if (!this->InstallProjectViaInstalledDirectories(
        setDestDir, tempInstallDirectory, default_dir_mode)) {
    return 0;
  }

  // If the project is a CMAKE project then run pre-install
  // and then read the cmake_install script to run it
  if (!this->InstallProjectViaInstallCMakeProjects(
        setDestDir, bareTempInstallDirectory, default_dir_mode)) {
    return 0;
  }

  // Run pre-build actions
  cmValue preBuildScripts = this->GetOption("CPACK_PRE_BUILD_SCRIPTS");
  if (preBuildScripts) {
    cmList const scripts{ preBuildScripts };
    for (auto const& script : scripts) {
      cmCPackLogger(cmCPackLog::LOG_OUTPUT,
                    "Executing pre-build script: " << script << std::endl);

      if (!this->MakefileMap->ReadListFile(script)) {
        cmCPackLogger(cmCPackLog::LOG_ERROR,
                      "The pre-build script not found: " << script
                                                         << std::endl);
        return 0;
      }
    }
  }

  if (setDestDir) {
    cmSystemTools::PutEnv("DESTDIR=");
  }

  return res;
}

int cmCPackGenerator::InstallProjectViaInstallCommands(
  bool setDestDir, std::string const& tempInstallDirectory)
{
  (void)setDestDir;
  cmValue installCommands = this->GetOption("CPACK_INSTALL_COMMANDS");
  if (cmNonempty(installCommands)) {
    std::string tempInstallDirectoryEnv =
      cmStrCat("CMAKE_INSTALL_PREFIX=", tempInstallDirectory);
    cmSystemTools::PutEnv(tempInstallDirectoryEnv);
    cmList installCommandsVector{ installCommands };
    for (std::string const& ic : installCommandsVector) {
      cmCPackLogger(cmCPackLog::LOG_VERBOSE, "Execute: " << ic << std::endl);
      std::string output;
      int retVal = 1;
      bool resB = cmSystemTools::RunSingleCommand(
        ic, &output, &output, &retVal, nullptr, this->GeneratorVerbose,
        cmDuration::zero());
      if (!resB || retVal) {
        std::string tmpFile = cmStrCat(
          this->GetOption("CPACK_TOPLEVEL_DIRECTORY"), "/InstallOutput.log");
        cmGeneratedFileStream ofs(tmpFile);
        ofs << "# Run command: " << ic << std::endl
            << "# Output:" << std::endl
            << output << std::endl;
        cmCPackLogger(cmCPackLog::LOG_ERROR,
                      "Problem running install command: "
                        << ic << std::endl
                        << "Please check " << tmpFile << " for errors"
                        << std::endl);
        return 0;
      }
    }
  }
  return 1;
}

int cmCPackGenerator::InstallProjectViaInstalledDirectories(
  bool setDestDir, std::string const& tempInstallDirectory,
  mode_t const* default_dir_mode)
{
  (void)setDestDir;
  (void)tempInstallDirectory;
  std::vector<cmsys::RegularExpression> ignoreFilesRegex;
  cmValue cpackIgnoreFiles = this->GetOption("CPACK_IGNORE_FILES");
  if (cpackIgnoreFiles) {
    cmList ignoreFilesRegexString{ cpackIgnoreFiles };
    for (std::string const& ifr : ignoreFilesRegexString) {
      cmCPackLogger(cmCPackLog::LOG_VERBOSE,
                    "Create ignore files regex for: " << ifr << std::endl);
      ignoreFilesRegex.emplace_back(ifr);
    }
  }
  cmValue installDirectories = this->GetOption("CPACK_INSTALLED_DIRECTORIES");
  if (cmNonempty(installDirectories)) {
    cmList installDirectoriesList{ installDirectories };
    if (installDirectoriesList.size() % 2 != 0) {
      cmCPackLogger(
        cmCPackLog::LOG_ERROR,
        "CPACK_INSTALLED_DIRECTORIES should contain pairs of <directory> "
        "and "
        "<subdirectory>. The <subdirectory> can be '.' to be installed in "
        "the toplevel directory of installation."
          << std::endl);
      return 0;
    }
    cmList::iterator it;
    std::string const& tempDir = tempInstallDirectory;
    for (it = installDirectoriesList.begin();
         it != installDirectoriesList.end(); ++it) {
      std::vector<std::pair<std::string, std::string>> symlinkedFiles;
      cmCPackLogger(cmCPackLog::LOG_DEBUG, "Find files" << std::endl);
      cmsys::Glob gl;
      std::string top = *it;
      ++it;
      std::string subdir = *it;
      std::string findExpr = cmStrCat(top, "/*");
      cmCPackLogger(cmCPackLog::LOG_OUTPUT,
                    "- Install directory: " << top << std::endl);
      gl.RecurseOn();
      gl.SetRecurseListDirs(true);
      gl.SetRecurseThroughSymlinks(false);
      if (!gl.FindFiles(findExpr)) {
        cmCPackLogger(cmCPackLog::LOG_ERROR,
                      "Cannot find any files in the installed directory"
                        << std::endl);
        return 0;
      }
      this->files = gl.GetFiles();
      for (std::string const& gf : this->files) {
        bool skip = false;
        std::string inFile = gf;
        if (cmSystemTools::FileIsDirectory(gf) &&
            !cmSystemTools::FileIsSymlink(gf)) {
          inFile += '/';
        }
        for (cmsys::RegularExpression& reg : ignoreFilesRegex) {
          if (reg.find(inFile)) {
            cmCPackLogger(cmCPackLog::LOG_VERBOSE,
                          "Ignore file: " << inFile << std::endl);
            skip = true;
          }
        }
        if (skip) {
          continue;
        }
        std::string filePath = cmStrCat(tempDir, '/', subdir, '/',
                                        cmSystemTools::RelativePath(top, gf));
        cmCPackLogger(cmCPackLog::LOG_DEBUG,
                      "Copy file: " << inFile << " -> " << filePath
                                    << std::endl);
        /* If the file is a symlink we will have to re-create it */
        if (cmSystemTools::FileIsSymlink(inFile)) {
          std::string targetFile;
          std::string inFileRelative =
            cmSystemTools::RelativePath(top, inFile);
          cmSystemTools::ReadSymlink(inFile, targetFile);
          symlinkedFiles.emplace_back(std::move(targetFile),
                                      std::move(inFileRelative));
        }
        /* If it is not a symlink then do a plain copy */
        else if (!(cmSystemTools::CopyFileIfDifferent(inFile, filePath) &&
                   cmFileTimes::Copy(inFile, filePath))) {
          cmCPackLogger(cmCPackLog::LOG_ERROR,
                        "Problem copying file: " << inFile << " -> "
                                                 << filePath << std::endl);
          return 0;
        }
      }
      /* rebuild symlinks in the installed tree */
      if (!symlinkedFiles.empty()) {
        std::string goToDir = cmStrCat(tempDir, '/', subdir);
        cmCPackLogger(cmCPackLog::LOG_DEBUG,
                      "Change dir to: " << goToDir << std::endl);
        cmWorkingDirectory workdir(goToDir);
        if (workdir.Failed()) {
          cmCPackLogger(cmCPackLog::LOG_ERROR,
                        workdir.GetError() << std::endl);
          return 0;
        }
        for (auto const& symlinked : symlinkedFiles) {
          cmCPackLogger(cmCPackLog::LOG_DEBUG,
                        "Will create a symlink: " << symlinked.second << "--> "
                                                  << symlinked.first
                                                  << std::endl);
          // make sure directory exists for symlink
          std::string destDir =
            cmSystemTools::GetFilenamePath(symlinked.second);
          if (!destDir.empty() &&
              !cmSystemTools::MakeDirectory(destDir, default_dir_mode)) {
            cmCPackLogger(cmCPackLog::LOG_ERROR,
                          "Cannot create dir: "
                            << destDir << "\nTrying to create symlink: "
                            << symlinked.second << "--> " << symlinked.first
                            << std::endl);
          }
          if (!cmSystemTools::CreateSymlink(symlinked.first,
                                            symlinked.second)) {
            cmCPackLogger(cmCPackLog::LOG_ERROR,
                          "Cannot create symlink: "
                            << symlinked.second << "--> " << symlinked.first
                            << std::endl);
            return 0;
          }
        }
        cmCPackLogger(cmCPackLog::LOG_DEBUG,
                      "Going back to: " << workdir.GetOldDirectory()
                                        << std::endl);
      }
    }
  }
  return 1;
}

int cmCPackGenerator::InstallProjectViaInstallScript(
  bool setDestDir, std::string const& tempInstallDirectory)
{
  cmValue cmakeScripts = this->GetOption("CPACK_INSTALL_SCRIPTS");
  {
    cmValue const cmakeScript = this->GetOption("CPACK_INSTALL_SCRIPT");
    if (cmakeScript && cmakeScripts) {
      cmCPackLogger(
        cmCPackLog::LOG_WARNING,
        "Both CPACK_INSTALL_SCRIPTS and CPACK_INSTALL_SCRIPT are set, "
        "the latter will be ignored."
          << std::endl);
    } else if (cmakeScript && !cmakeScripts) {
      cmakeScripts = cmakeScript;
    }
  }
  if (cmakeScripts && !cmakeScripts->empty()) {
    cmCPackLogger(cmCPackLog::LOG_OUTPUT,
                  "- Install scripts: " << cmakeScripts << std::endl);
    cmList cmakeScriptsVector{ cmakeScripts };
    for (std::string const& installScript : cmakeScriptsVector) {

      cmCPackLogger(cmCPackLog::LOG_OUTPUT,
                    "- Install script: " << installScript << std::endl);

      if (setDestDir) {
        // For DESTDIR based packaging, use the *project*
        // CMAKE_INSTALL_PREFIX underneath the tempInstallDirectory. The
        // value of the project's CMAKE_INSTALL_PREFIX is sent in here as the
        // value of the CPACK_INSTALL_PREFIX variable.

        std::string dir;
        if (this->GetOption("CPACK_INSTALL_PREFIX")) {
          dir += *this->GetOption("CPACK_INSTALL_PREFIX");
        }
        this->SetOption("CMAKE_INSTALL_PREFIX", dir);
        cmCPackLogger(
          cmCPackLog::LOG_DEBUG,
          "- Using DESTDIR + CPACK_INSTALL_PREFIX... (this->SetOption)"
            << std::endl);
        cmCPackLogger(cmCPackLog::LOG_DEBUG,
                      "- Setting CMAKE_INSTALL_PREFIX to '" << dir << "'"
                                                            << std::endl);
      } else {
        this->SetOption("CMAKE_INSTALL_PREFIX", tempInstallDirectory);

        cmCPackLogger(cmCPackLog::LOG_DEBUG,
                      "- Using non-DESTDIR install... (this->SetOption)"
                        << std::endl);
        cmCPackLogger(cmCPackLog::LOG_DEBUG,
                      "- Setting CMAKE_INSTALL_PREFIX to '"
                        << tempInstallDirectory << "'" << std::endl);
      }

      this->SetOptionIfNotSet("CMAKE_CURRENT_BINARY_DIR",
                              tempInstallDirectory);
      this->SetOptionIfNotSet("CMAKE_CURRENT_SOURCE_DIR",
                              tempInstallDirectory);
      bool res = this->MakefileMap->ReadListFile(installScript);
      if (cmSystemTools::GetErrorOccurredFlag() || !res) {
        return 0;
      }
    }
  }
  return 1;
}

int cmCPackGenerator::InstallProjectViaInstallCMakeProjects(
  bool setDestDir, std::string const& baseTempInstallDirectory,
  mode_t const* default_dir_mode)
{
  cmValue cmakeProjects = this->GetOption("CPACK_INSTALL_CMAKE_PROJECTS");
  cmValue cmakeGenerator = this->GetOption("CPACK_CMAKE_GENERATOR");
  std::string absoluteDestFiles;
  if (cmNonempty(cmakeProjects)) {
    if (!cmakeGenerator) {
      cmCPackLogger(cmCPackLog::LOG_ERROR,
                    "CPACK_INSTALL_CMAKE_PROJECTS is specified, but "
                    "CPACK_CMAKE_GENERATOR is not. CPACK_CMAKE_GENERATOR "
                    "is required to install the project."
                      << std::endl);
      return 0;
    }
    cmList cmakeProjectsList{ cmakeProjects };
    cmList::iterator it;
    for (it = cmakeProjectsList.begin(); it != cmakeProjectsList.end(); ++it) {
      if (it + 1 == cmakeProjectsList.end() ||
          it + 2 == cmakeProjectsList.end() ||
          it + 3 == cmakeProjectsList.end()) {
        cmCPackLogger(
          cmCPackLog::LOG_ERROR,
          "Not enough items on list: CPACK_INSTALL_CMAKE_PROJECTS. "
          "CPACK_INSTALL_CMAKE_PROJECTS should hold quadruplet of install "
          "directory, install project name, install component, and install "
          "subdirectory."
            << std::endl);
        return 0;
      }
      std::string installDirectory = *it;
      ++it;
      std::string installProjectName = *it;
      ++it;
      cmCPackInstallCMakeProject project;

      project.Directory = installDirectory;
      project.ProjectName = installProjectName;
      project.Component = *it;
      ++it;
      project.SubDirectory = *it;

      cmList componentsList;

      bool componentInstall = false;
      /*
       * We do a component install iff
       *    - the CPack generator support component
       *    - the user did not request Monolithic install
       *      (this works at CPack time too)
       */
      if (this->SupportsComponentInstallation() &&
          !(this->IsOn("CPACK_MONOLITHIC_INSTALL"))) {
        // Determine the installation types for this project (if provided).
        std::string installTypesVar = "CPACK_" +
          cmSystemTools::UpperCase(project.Component) + "_INSTALL_TYPES";
        cmValue installTypes = this->GetOption(installTypesVar);
        if (!installTypes.IsEmpty()) {
          cmList installTypesList{ installTypes };
          for (std::string const& installType : installTypesList) {
            project.InstallationTypes.push_back(
              this->GetInstallationType(project.ProjectName, installType));
          }
        }

        // Determine the set of components that will be used in this project
        std::string componentsVar =
          "CPACK_COMPONENTS_" + cmSystemTools::UpperCase(project.Component);
        cmValue components = this->GetOption(componentsVar);
        if (!components.IsEmpty()) {
          componentsList.assign(components);
          for (auto const& comp : componentsList) {
            project.Components.push_back(
              this->GetComponent(project.ProjectName, comp));
          }
          componentInstall = true;
        }
      }
      if (componentsList.empty()) {
        componentsList.push_back(project.Component);
      }

      cmList buildConfigs;

      // Try get configuration names given via `-C` CLI option
      buildConfigs.assign(this->GetOption("CPACK_BUILD_CONFIG"));

      // Remove duplicates
      std::sort(buildConfigs.begin(), buildConfigs.end());
      buildConfigs.erase(std::unique(buildConfigs.begin(), buildConfigs.end()),
                         buildConfigs.end());

      // Ensure we have at least one configuration.
      if (buildConfigs.empty()) {
        buildConfigs.emplace_back();
      }

      std::unique_ptr<cmGlobalGenerator> globalGenerator =
        this->MakefileMap->GetCMakeInstance()->CreateGlobalGenerator(
          *cmakeGenerator);
      if (!globalGenerator) {
        cmCPackLogger(cmCPackLog::LOG_ERROR,
                      "Specified package generator not found. "
                      "CPACK_CMAKE_GENERATOR value is invalid."
                        << std::endl);
        return 0;
      }
      // set the global flag for unix style paths on cmSystemTools as
      // soon as the generator is set.  This allows gmake to be used
      // on windows.
      cmSystemTools::SetForceUnixPaths(globalGenerator->GetForceUnixPaths());

      // Run the installation for the selected build configurations
      for (auto const& buildConfig : buildConfigs) {
        if (!this->RunPreinstallTarget(project.ProjectName, project.Directory,
                                       globalGenerator.get(), buildConfig)) {
          return 0;
        }

        cmCPackLogger(cmCPackLog::LOG_OUTPUT,
                      "- Install project: " << project.ProjectName << " ["
                                            << buildConfig << ']'
                                            << std::endl);
        // Run the installation for each component
        for (std::string const& component : componentsList) {
          if (!this->InstallCMakeProject(
                setDestDir, project.Directory, baseTempInstallDirectory,
                default_dir_mode, component, componentInstall,
                project.SubDirectory, buildConfig, absoluteDestFiles)) {
            return 0;
          }
        }
      }

      this->CMakeProjects.emplace_back(std::move(project));
    }
  }
  this->SetOption("CPACK_ABSOLUTE_DESTINATION_FILES", absoluteDestFiles);
  return 1;
}

int cmCPackGenerator::RunPreinstallTarget(
  std::string const& installProjectName, std::string const& installDirectory,
  cmGlobalGenerator* globalGenerator, std::string const& buildConfig)
{
  // Does this generator require pre-install?
  if (char const* preinstall = globalGenerator->GetPreinstallTargetName()) {
    std::string buildCommand = globalGenerator->GenerateCMakeBuildCommand(
      preinstall, buildConfig, "", "", false);
    cmCPackLogger(cmCPackLog::LOG_DEBUG,
                  "- Install command: " << buildCommand << std::endl);
    cmCPackLogger(cmCPackLog::LOG_OUTPUT,
                  "- Run preinstall target for: " << installProjectName
                                                  << std::endl);
    std::string output;
    int retVal = 1;
    bool resB = cmSystemTools::RunSingleCommand(
      buildCommand, &output, &output, &retVal, installDirectory.c_str(),
      this->GeneratorVerbose, cmDuration::zero());
    if (!resB || retVal) {
      std::string tmpFile = cmStrCat(
        this->GetOption("CPACK_TOPLEVEL_DIRECTORY"), "/PreinstallOutput.log");
      cmGeneratedFileStream ofs(tmpFile);
      ofs << "# Run command: " << buildCommand << std::endl
          << "# Directory: " << installDirectory << std::endl
          << "# Output:" << std::endl
          << output << std::endl;
      cmCPackLogger(cmCPackLog::LOG_ERROR,
                    "Problem running install command: "
                      << buildCommand << std::endl
                      << "Please check " << tmpFile << " for errors"
                      << std::endl);
      return 0;
    }
  }

  return 1;
}

int cmCPackGenerator::InstallCMakeProject(
  bool setDestDir, std::string const& installDirectory,
  std::string const& baseTempInstallDirectory, mode_t const* default_dir_mode,
  std::string const& component, bool componentInstall,
  std::string const& installSubDirectory, std::string const& buildConfig,
  std::string& absoluteDestFiles)
{
  std::string tempInstallDirectory = baseTempInstallDirectory;
  std::string installFile = installDirectory + "/cmake_install.cmake";

  if (componentInstall) {
    cmCPackLogger(cmCPackLog::LOG_OUTPUT,
                  "-   Install component: " << component << std::endl);
  }

  cmake cm(cmake::RoleScript, cmState::CPack);
  cm.SetHomeDirectory("");
  cm.SetHomeOutputDirectory("");
  cm.GetCurrentSnapshot().SetDefaultDefinitions();
  cm.AddCMakePaths();
  cm.SetProgressCallback([this](std::string const& msg, float prog) {
    this->DisplayVerboseOutput(msg, prog);
  });
  cm.SetTrace(this->Trace);
  cm.SetTraceExpand(this->TraceExpand);
  cmGlobalGenerator gg(&cm);
  cmMakefile mf(&gg, cm.GetCurrentSnapshot());
  if (!installSubDirectory.empty() && installSubDirectory != "/" &&
      installSubDirectory != ".") {
    tempInstallDirectory += installSubDirectory;
  }
  if (componentInstall) {
    tempInstallDirectory += "/";
    // Some CPack generators would rather chose
    // the local installation directory suffix.
    // Some (e.g. RPM) use
    //  one install directory for each component **GROUP**
    // instead of the default
    //  one install directory for each component.
    tempInstallDirectory += this->GetComponentInstallDirNameSuffix(component);

    if (this->IsOn("CPACK_COMPONENT_INCLUDE_TOPLEVEL_DIRECTORY")) {
      tempInstallDirectory += "/";
      tempInstallDirectory += *this->GetOption("CPACK_PACKAGE_FILE_NAME");
    }
  }

  cmValue default_dir_inst_permissions =
    this->GetOption("CPACK_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS");
  if (cmNonempty(default_dir_inst_permissions)) {
    mf.AddDefinition("CMAKE_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS",
                     default_dir_inst_permissions);
  }

  if (!setDestDir) {
    tempInstallDirectory += this->GetPackagingInstallPrefix();
  }

  if (setDestDir) {
    // For DESTDIR based packaging, use the *project*
    // CMAKE_INSTALL_PREFIX underneath the tempInstallDirectory. The
    // value of the project's CMAKE_INSTALL_PREFIX is sent in here as
    // the value of the CPACK_INSTALL_PREFIX variable.
    //
    // If DESTDIR has been 'internally set ON' this means that
    // the underlying CPack specific generator did ask for that
    // In this case we may override CPACK_INSTALL_PREFIX with
    // CPACK_PACKAGING_INSTALL_PREFIX
    // I know this is tricky and awkward but it's the price for
    // CPACK_SET_DESTDIR backward compatibility.
    if (cmIsInternallyOn(this->GetOption("CPACK_SET_DESTDIR"))) {
      this->SetOption("CPACK_INSTALL_PREFIX",
                      this->GetOption("CPACK_PACKAGING_INSTALL_PREFIX"));
    }
    std::string dir;
    if (this->GetOption("CPACK_INSTALL_PREFIX")) {
      dir += *this->GetOption("CPACK_INSTALL_PREFIX");
    }
    mf.AddDefinition("CMAKE_INSTALL_PREFIX", dir);

    cmCPackLogger(
      cmCPackLog::LOG_DEBUG,
      "- Using DESTDIR + CPACK_INSTALL_PREFIX... (mf.AddDefinition)"
        << std::endl);
    cmCPackLogger(cmCPackLog::LOG_DEBUG,
                  "- Setting CMAKE_INSTALL_PREFIX to '" << dir << "'"
                                                        << std::endl);

    // Make sure that DESTDIR + CPACK_INSTALL_PREFIX directory
    // exists:
    //
    if (cmHasLiteralPrefix(dir, "/")) {
      dir = tempInstallDirectory + dir;
    } else {
      dir = tempInstallDirectory + "/" + dir;
    }
    /*
     *  We must re-set DESTDIR for each component
     *  We must not add the CPACK_INSTALL_PREFIX part because
     *  it will be added using the override of CMAKE_INSTALL_PREFIX
     *  The main reason for this awkward trick is that
     *  are using DESTDIR for 2 different reasons:
     *     - Because it was asked by the CPack Generator or the user
     *       using CPACK_SET_DESTDIR
     *     - Because it was already used for component install
     *       in order to put things in subdirs...
     */
    cmSystemTools::PutEnv("DESTDIR=" + tempInstallDirectory);
    cmCPackLogger(cmCPackLog::LOG_DEBUG,
                  "- Creating directory: '" << dir << "'" << std::endl);

    if (!cmsys::SystemTools::MakeDirectory(dir, default_dir_mode)) {
      cmCPackLogger(cmCPackLog::LOG_ERROR,
                    "Problem creating temporary directory: " << dir
                                                             << std::endl);
      return 0;
    }
  } else {
    mf.AddDefinition("CMAKE_INSTALL_PREFIX", tempInstallDirectory);

    if (!cmsys::SystemTools::MakeDirectory(tempInstallDirectory,
                                           default_dir_mode)) {
      cmCPackLogger(cmCPackLog::LOG_ERROR,
                    "Problem creating temporary directory: "
                      << tempInstallDirectory << std::endl);
      return 0;
    }

    cmCPackLogger(cmCPackLog::LOG_DEBUG,
                  "- Using non-DESTDIR install... (mf.AddDefinition)"
                    << std::endl);
    cmCPackLogger(cmCPackLog::LOG_DEBUG,
                  "- Setting CMAKE_INSTALL_PREFIX to '" << tempInstallDirectory
                                                        << "'" << std::endl);
  }

  if (!buildConfig.empty()) {
    mf.AddDefinition("BUILD_TYPE", buildConfig);
  }
  std::string installComponentLowerCase = cmSystemTools::LowerCase(component);
  if (installComponentLowerCase != "all") {
    mf.AddDefinition("CMAKE_INSTALL_COMPONENT", component);
  }

  // strip on TRUE, ON, 1, one or several file names, but not on
  // FALSE, OFF, 0 and an empty string
  if (!this->GetOption("CPACK_STRIP_FILES").IsOff()) {
    mf.AddDefinition("CMAKE_INSTALL_DO_STRIP", "1");
  }
  // Remember the list of files before installation
  // of the current component (if we are in component install)
  std::string const& InstallPrefix = tempInstallDirectory;
  std::vector<std::string> filesBefore;
  std::string findExpr = tempInstallDirectory;
  if (componentInstall) {
    cmsys::Glob glB;
    findExpr += "/*";
    glB.RecurseOn();
    glB.SetRecurseListDirs(true);
    glB.SetRecurseThroughSymlinks(false);
    glB.FindFiles(findExpr);
    filesBefore = glB.GetFiles();
    std::sort(filesBefore.begin(), filesBefore.end());
  }

  // If CPack was asked to warn on ABSOLUTE INSTALL DESTINATION
  // then forward request to cmake_install.cmake script
  if (this->IsOn("CPACK_WARN_ON_ABSOLUTE_INSTALL_DESTINATION")) {
    mf.AddDefinition("CMAKE_WARN_ON_ABSOLUTE_INSTALL_DESTINATION", "1");
  }
  // If current CPack generator does not support
  // ABSOLUTE INSTALL DESTINATION or CPack has been asked for
  // then ask cmake_install.cmake script to error out
  // as soon as it occurs (before installing file)
  if (!this->SupportsAbsoluteDestination() ||
      this->IsOn("CPACK_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION")) {
    mf.AddDefinition("CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION", "1");
  }

  cmList custom_variables{ this->MakefileMap->GetDefinition(
    "CPACK_CUSTOM_INSTALL_VARIABLES") };

  for (auto const& custom_variable : custom_variables) {
    std::string value;

    auto i = custom_variable.find('=');

    if (i != std::string::npos) {
      value = custom_variable.substr(i + 1);
    }

    mf.AddDefinition(custom_variable.substr(0, i), value);
  }

  // do installation
  bool res = mf.ReadListFile(installFile);
  // forward definition of CMAKE_ABSOLUTE_DESTINATION_FILES
  // to CPack (may be used by generators like CPack RPM or DEB)
  // in order to transparently handle ABSOLUTE PATH
  if (cmValue def = mf.GetDefinition("CMAKE_ABSOLUTE_DESTINATION_FILES")) {
    mf.AddDefinition("CPACK_ABSOLUTE_DESTINATION_FILES", *def);
  }

  // Now rebuild the list of files after installation
  // of the current component (if we are in component install)
  if (componentInstall) {
    cmsys::Glob glA;
    glA.RecurseOn();
    glA.SetRecurseListDirs(true);
    glA.SetRecurseThroughSymlinks(false);
    glA.FindFiles(findExpr);
    std::vector<std::string> filesAfter = glA.GetFiles();
    std::sort(filesAfter.begin(), filesAfter.end());
    std::vector<std::string>::iterator diff;
    std::vector<std::string> result(filesAfter.size());
    diff = std::set_difference(filesAfter.begin(), filesAfter.end(),
                               filesBefore.begin(), filesBefore.end(),
                               result.begin());

    std::vector<std::string>::iterator fit;
    std::string localFileName;
    // Populate the File field of each component
    for (fit = result.begin(); fit != diff; ++fit) {
      localFileName = cmSystemTools::RelativePath(InstallPrefix, *fit);
      localFileName =
        localFileName.substr(localFileName.find_first_not_of('/'));
      this->Components[component].Files.push_back(localFileName);
      cmCPackLogger(cmCPackLog::LOG_DEBUG,
                    "Adding file <" << localFileName << "> to component <"
                                    << component << ">" << std::endl);
    }
  }

  if (cmValue d = mf.GetDefinition("CPACK_ABSOLUTE_DESTINATION_FILES")) {
    if (!absoluteDestFiles.empty()) {
      absoluteDestFiles += ";";
    }
    absoluteDestFiles += *d;
    cmCPackLogger(cmCPackLog::LOG_DEBUG,
                  "Got some ABSOLUTE DESTINATION FILES: " << absoluteDestFiles
                                                          << std::endl);
    // define component specific var
    if (componentInstall) {
      std::string absoluteDestFileComponent =
        std::string("CPACK_ABSOLUTE_DESTINATION_FILES") + "_" +
        this->GetComponentInstallSuffix(component);
      if (cmValue v = this->GetOption(absoluteDestFileComponent)) {
        std::string absoluteDestFilesListComponent = cmStrCat(*v, ';', *d);
        this->SetOption(absoluteDestFileComponent,
                        absoluteDestFilesListComponent);
      } else {
        this->SetOption(absoluteDestFileComponent,
                        mf.GetDefinition("CPACK_ABSOLUTE_DESTINATION_FILES"));
      }
    }
  }
  if (cmSystemTools::GetErrorOccurredFlag() || !res) {
    return 0;
  }
  return 1;
}

bool cmCPackGenerator::GenerateChecksumFile(cmCryptoHash& crypto,
                                            cm::string_view filename) const
{
  std::string packageFileName =
    cmStrCat(this->GetOption("CPACK_OUTPUT_FILE_PREFIX"), '/', filename);
  std::string hashFile = cmStrCat(
    packageFileName, "." + cmSystemTools::LowerCase(crypto.GetHashAlgoName()));
  cmsys::ofstream outF(hashFile.c_str());
  if (!outF) {
    cmCPackLogger(cmCPackLog::LOG_ERROR,
                  "Cannot create checksum file: " << hashFile << std::endl);
    return false;
  }
  outF << crypto.HashFile(packageFileName) << "  " << filename << "\n";
  cmCPackLogger(cmCPackLog::LOG_OUTPUT,
                "- checksum file: " << hashFile << " generated." << std::endl);
  return true;
}

bool cmCPackGenerator::CopyPackageFile(std::string const& srcFilePath,
                                       cm::string_view filename) const
{
  std::string destFilePath =
    cmStrCat(this->GetOption("CPACK_OUTPUT_FILE_PREFIX"), '/', filename);
  cmCPackLogger(cmCPackLog::LOG_DEBUG,
                "Copy final package(s): "
                  << (!srcFilePath.empty() ? srcFilePath : "(NULL)") << " to "
                  << (!destFilePath.empty() ? destFilePath : "(NULL)")
                  << std::endl);
  if (!cmSystemTools::CopyFileIfDifferent(srcFilePath, destFilePath)) {
    cmCPackLogger(
      cmCPackLog::LOG_ERROR,
      "Problem copying the package: "
        << (!srcFilePath.empty() ? srcFilePath : "(NULL)") << " to "
        << (!destFilePath.empty() ? destFilePath : "(NULL)") << std::endl);
    return false;
  }
  cmCPackLogger(cmCPackLog::LOG_OUTPUT,
                "- package: " << destFilePath << " generated." << std::endl);
  return true;
}

bool cmCPackGenerator::ReadListFile(char const* moduleName)
{
  bool retval;
  std::string fullPath = this->MakefileMap->GetModulesFile(moduleName);
  retval = this->MakefileMap->ReadListFile(fullPath);
  // include FATAL_ERROR and ERROR in the return status
  retval = retval && (!cmSystemTools::GetErrorOccurredFlag());
  return retval;
}

template <typename ValueType>
void cmCPackGenerator::StoreOptionIfNotSet(std::string const& op,
                                           ValueType value)
{
  cmValue def = this->MakefileMap->GetDefinition(op);
  if (cmNonempty(def)) {
    return;
  }
  this->StoreOption(op, value);
}

void cmCPackGenerator::SetOptionIfNotSet(std::string const& op,
                                         char const* value)
{
  this->StoreOptionIfNotSet(op, value);
}
void cmCPackGenerator::SetOptionIfNotSet(std::string const& op, cmValue value)
{
  this->StoreOptionIfNotSet(op, value);
}

template <typename ValueType>
void cmCPackGenerator::StoreOption(std::string const& op, ValueType value)
{
  if (!value) {
    this->MakefileMap->RemoveDefinition(op);
    return;
  }
  cmCPackLogger(cmCPackLog::LOG_DEBUG,
                this->GetNameOfClass() << "::SetOption(" << op << ", " << value
                                       << ")" << std::endl);
  this->MakefileMap->AddDefinition(op, value);
}

void cmCPackGenerator::SetOption(std::string const& op, char const* value)
{
  this->StoreOption(op, value);
}
void cmCPackGenerator::SetOption(std::string const& op, cmValue value)
{
  this->StoreOption(op, value);
}

int cmCPackGenerator::DoPackage()
{
  cmCPackLogger(cmCPackLog::LOG_OUTPUT,
                "Create package using " << this->Name << std::endl);

  // Prepare CPack internal name and check
  // values for many CPACK_xxx vars
  if (!this->PrepareNames()) {
    return 0;
  }

  // Digest Component grouping specification
  if (!this->PrepareGroupingKind()) {
    return 0;
  }

  // Possibly remove the top-level packaging-directory.
  if (this->GetOption("CPACK_REMOVE_TOPLEVEL_DIRECTORY").IsOn()) {
    cmValue toplevelDirectory = this->GetOption("CPACK_TOPLEVEL_DIRECTORY");
    if (toplevelDirectory && cmSystemTools::FileExists(*toplevelDirectory)) {
      cmCPackLogger(cmCPackLog::LOG_VERBOSE,
                    "Remove toplevel directory: " << *toplevelDirectory
                                                  << std::endl);
      if (!cmSystemTools::RepeatedRemoveDirectory(*toplevelDirectory)) {
        cmCPackLogger(cmCPackLog::LOG_ERROR,
                      "Problem removing toplevel directory: "
                        << *toplevelDirectory << std::endl);
        return 0;
      }
    }
  }

  // Install the project (to the temporary install-directory).
  cmCPackLogger(cmCPackLog::LOG_DEBUG,
                "About to install project " << std::endl);
  if (!this->InstallProject()) {
    return 0;
  }
  cmCPackLogger(cmCPackLog::LOG_DEBUG, "Done install project " << std::endl);

  // Determine the temporary directory whose content shall be packaged.
  cmValue tempDirectory = this->GetOption("CPACK_TEMPORARY_DIRECTORY");

  // Determine and store internally the list of files to be installed.
  cmCPackLogger(cmCPackLog::LOG_DEBUG, "Find files" << std::endl);
  {
    cmsys::Glob gl;
    std::string findExpr = cmStrCat(tempDirectory, "/*");
    gl.RecurseOn();
    gl.SetRecurseListDirs(true);
    gl.SetRecurseThroughSymlinks(false);
    if (!gl.FindFiles(findExpr)) {
      cmCPackLogger(cmCPackLog::LOG_ERROR,
                    "Cannot find any files in the packaging tree"
                      << std::endl);
      return 0;
    }
    this->files = gl.GetFiles();
  }

  // Determine and store internally the directory that shall be packaged.
  if (this->GetOption("CPACK_INCLUDE_TOPLEVEL_DIRECTORY").IsOn()) {
    tempDirectory = this->GetOption("CPACK_TOPLEVEL_DIRECTORY");
  }
  this->toplevel = *tempDirectory;

  cmCPackLogger(cmCPackLog::LOG_OUTPUT, "Create package" << std::endl);

  // Determine and store internally the list of packages to create.
  // Note: Initially, this only contains a single package.
  {
    cmValue tempPackageFileName =
      this->GetOption("CPACK_TEMPORARY_PACKAGE_FILE_NAME");
    cmCPackLogger(cmCPackLog::LOG_VERBOSE,
                  "Package files to: "
                    << (tempPackageFileName ? *tempPackageFileName : "(NULL)")
                    << std::endl);
    if (tempPackageFileName &&
        cmSystemTools::FileExists(*tempPackageFileName)) {
      cmCPackLogger(cmCPackLog::LOG_VERBOSE,
                    "Remove old package file" << std::endl);
      cmSystemTools::RemoveFile(*tempPackageFileName);
    }
    this->packageFileNames.clear();
    /* Put at least one file name into the list of
     * wanted packageFileNames. The specific generator
     * may update this during PackageFiles.
     * (either putting several names or updating the provided one)
     */
    this->packageFileNames.emplace_back(tempPackageFileName);
  }

  // Do package the files (using the derived CPack generators.
  { // scope that enables package generators to run internal scripts with
    // latest CMake policies enabled
    cmMakefile::ScopePushPop pp{ this->MakefileMap };
    this->MakefileMap->SetPolicyVersion(cmVersion::GetCMakeVersion(),
                                        std::string());

    if (!this->PackageFiles() || cmSystemTools::GetErrorOccurredFlag()) {
      cmCPackLogger(cmCPackLog::LOG_ERROR,
                    "Problem compressing the directory" << std::endl);
      return 0;
    }
  }

  // Run post-build actions
  cmValue postBuildScripts = this->GetOption("CPACK_POST_BUILD_SCRIPTS");
  if (postBuildScripts) {
    this->MakefileMap->AddDefinition(
      "CPACK_PACKAGE_FILES", cmList::to_string(this->packageFileNames));

    cmList const scripts{ postBuildScripts };
    for (auto const& script : scripts) {
      cmCPackLogger(cmCPackLog::LOG_OUTPUT,
                    "Executing post-build script: " << script << std::endl);

      if (!this->MakefileMap->ReadListFile(script)) {
        cmCPackLogger(cmCPackLog::LOG_ERROR,
                      "The post-build script not found: " << script
                                                          << std::endl);
        return 0;
      }
    }
  }

  /* Prepare checksum algorithm*/
  cmValue algo = this->GetOption("CPACK_PACKAGE_CHECKSUM");
  std::unique_ptr<cmCryptoHash> crypto = cmCryptoHash::New(*algo);

  /*
   * Copy the generated packages to final destination
   *  - there may be several of them
   *  - the initially provided name may have changed
   *    (because the specific generator did 'normalize' it)
   */
  cmCPackLogger(cmCPackLog::LOG_VERBOSE,
                "Copying final package(s) [" << this->packageFileNames.size()
                                             << "]:" << std::endl);
  /* now copy package one by one */
  for (std::string const& pkgFileName : this->packageFileNames) {
    std::string filename(cmSystemTools::GetFilenameName(pkgFileName));
    if (!this->CopyPackageFile(pkgFileName, filename)) {
      return 0;
    }
    /* Generate checksum file */
    if (crypto) {
      if (!this->GenerateChecksumFile(*crypto, filename)) {
        return 0;
      }
    }
  }

  return 1;
}

int cmCPackGenerator::Initialize(std::string const& name, cmMakefile* mf)
{
  this->MakefileMap = mf;
  this->Name = name;
  // set the running generator name
  this->SetOption("CPACK_GENERATOR", this->Name);
  // Load the project specific config file
  cmValue config = this->GetOption("CPACK_PROJECT_CONFIG_FILE");
  if (config) {
    if (!mf->ReadListFile(*config)) {
      cmCPackLogger(cmCPackLog::LOG_WARNING,
                    "CPACK_PROJECT_CONFIG_FILE not found: " << *config
                                                            << std::endl);
    }
  }
  int result = this->InitializeInternal();
  if (cmSystemTools::GetErrorOccurredFlag()) {
    return 0;
  }

  // If a generator subclass did not already set this option in its
  // InitializeInternal implementation, and the project did not already set
  // it, the default value should be:
  this->SetOptionIfNotSet("CPACK_PACKAGING_INSTALL_PREFIX", "/");

  // Special handling for CPACK_TEMPORARY[_INSTALL]_DIRECTORY.
  // Note: Make sure that if only one of these variables is already set, the
  //       other will be set to the same value. If they are set to different
  //       values, however, we cannot proceed.
  cmValue val1 =
    this->MakefileMap->GetDefinition("CPACK_TEMPORARY_INSTALL_DIRECTORY");
  cmValue val2 = this->MakefileMap->GetDefinition("CPACK_TEMPORARY_DIRECTORY");
  if (val1 != val2) {
    // One variable is set but not the other?
    // Then set the other variable to the same value (even if it is invalid).
    if (val1.Get() && !val2.Get()) {
      cmCPackLogger(cmCPackLog::LOG_WARNING,
                    "Variable CPACK_TEMPORARY_INSTALL_DIRECTORY is set, which "
                    "is not recommended. For backwards-compatibility we will "
                    "also set CPACK_TEMPORARY_DIRECTORY to the same value and "
                    "proceed. However, better set neither of them!"
                      << std::endl);
      this->MakefileMap->AddDefinition("CPACK_TEMPORARY_DIRECTORY", val1);
    } else if (!val1.Get() && val2.Get()) {
      cmCPackLogger(
        cmCPackLog::LOG_WARNING,
        "Variable CPACK_TEMPORARY_DIRECTORY is set, which is not recommended."
          << std::endl);
      cmCPackLogger(
        cmCPackLog::LOG_DEBUG,
        "For backwards-compatibility we will set "
        "CPACK_TEMPORARY_INSTALL_DIRECTORY to the same value as "
        "CPACK_TEMPORARY_DIRECTORY. However, better set neither of them!"
          << std::endl);
      this->MakefileMap->AddDefinition("CPACK_TEMPORARY_INSTALL_DIRECTORY",
                                       val2);
    } else {
      cmCPackLogger(cmCPackLog::LOG_VERBOSE,
                    "CPACK_TEMPORARY_INSTALL_DIRECTORY is already set to: "
                      << val1 << std::endl);
      cmCPackLogger(
        cmCPackLog::LOG_VERBOSE,
        "CPACK_TEMPORARY_DIRECTORY is already set to: " << val2 << std::endl);
      cmCPackLogger(cmCPackLog::LOG_ERROR,
                    "Variables CPACK_TEMPORARY_DIRECTORY and "
                    "CPACK_TEMPORARY_INSTALL_DIRECTORY are both set but to "
                    "different values. This is not supported!"
                      << std::endl);
      return 0;
    }
  } else if (val1.Get() && val2.Get()) {
    cmCPackLogger(cmCPackLog::LOG_WARNING,
                  "Variables CPACK_TEMPORARY_DIRECTORY and "
                  "CPACK_TEMPORARY_INSTALL_DIRECTORY are both set. Because "
                  "they are set to the same value we can still proceed. "
                  "However, better set neither of them!"
                    << std::endl);
  }

  return result;
}

int cmCPackGenerator::InitializeInternal()
{
  return 1;
}

bool cmCPackGenerator::IsSet(std::string const& name) const
{
  return this->MakefileMap->IsSet(name);
}

cmValue cmCPackGenerator::GetOptionIfSet(std::string const& name) const
{
  cmValue ret = this->MakefileMap->GetDefinition(name);
  if (!ret || ret->empty() || cmIsNOTFOUND(*ret)) {
    return {};
  }
  return ret;
}

bool cmCPackGenerator::IsOn(std::string const& name) const
{
  return this->GetOption(name).IsOn();
}

bool cmCPackGenerator::IsSetToOff(std::string const& op) const
{
  cmValue ret = this->MakefileMap->GetDefinition(op);
  if (cmNonempty(ret)) {
    return ret.IsOff();
  }
  return false;
}

bool cmCPackGenerator::IsSetToEmpty(std::string const& op) const
{
  cmValue ret = this->MakefileMap->GetDefinition(op);
  if (ret) {
    return ret->empty();
  }
  return false;
}

cmValue cmCPackGenerator::GetOption(std::string const& op) const
{
  cmValue ret = this->MakefileMap->GetDefinition(op);
  if (!ret) {
    cmCPackLogger(cmCPackLog::LOG_DEBUG,
                  "Warning, GetOption return NULL for: " << op << std::endl);
  }
  return ret;
}

std::vector<std::string> cmCPackGenerator::GetOptions() const
{
  return this->MakefileMap->GetDefinitions();
}

int cmCPackGenerator::PackageFiles()
{
  return 0;
}

char const* cmCPackGenerator::GetInstallPath()
{
  if (!this->InstallPath.empty()) {
    return this->InstallPath.c_str();
  }
#if defined(_WIN32) && !defined(__CYGWIN__)
  std::string prgfiles;
  std::string sysDrive;
  if (cmsys::SystemTools::GetEnv("ProgramFiles", prgfiles)) {
    this->InstallPath = prgfiles;
  } else if (cmsys::SystemTools::GetEnv("SystemDrive", sysDrive)) {
    this->InstallPath = cmStrCat(sysDrive, "/Program Files");
  } else {
    this->InstallPath = "c:/Program Files";
  }
  this->InstallPath += "/";
  this->InstallPath += this->GetOption("CPACK_PACKAGE_NAME");
  this->InstallPath += "-";
  this->InstallPath += this->GetOption("CPACK_PACKAGE_VERSION");
#elif defined(__HAIKU__)
  char dir[B_PATH_NAME_LENGTH];
  if (find_directory(B_SYSTEM_DIRECTORY, -1, false, dir, sizeof(dir)) ==
      B_OK) {
    this->InstallPath = dir;
  } else {
    this->InstallPath = "/boot/system";
  }
#else
  this->InstallPath = "/usr/local/";
#endif
  return this->InstallPath.c_str();
}

char const* cmCPackGenerator::GetPackagingInstallPrefix()
{
  cmCPackLogger(cmCPackLog::LOG_DEBUG,
                "GetPackagingInstallPrefix: '"
                  << this->GetOption("CPACK_PACKAGING_INSTALL_PREFIX") << "'"
                  << std::endl);

  return this->GetOption("CPACK_PACKAGING_INSTALL_PREFIX")->c_str();
}

std::string cmCPackGenerator::FindTemplate(cm::string_view name,
                                           cm::optional<cm::string_view> alt)
{
  cmCPackLogger(cmCPackLog::LOG_DEBUG,
                "Look for template: " << name << std::endl);
  // Search CMAKE_MODULE_PATH for a custom template.
  std::string ffile = this->MakefileMap->GetModulesFile(name);
  if (ffile.empty()) {
    // Fall back to our internal builtin default.
    ffile = cmStrCat(cmSystemTools::GetCMakeRoot(), "/Modules/Internal/CPack/",
                     alt ? *alt : ""_s, name);
    cmSystemTools::ConvertToUnixSlashes(ffile);
    if (!cmSystemTools::FileExists(ffile)) {
      ffile.clear();
    }
  }
  cmCPackLogger(cmCPackLog::LOG_DEBUG,
                "Found template: " << ffile << std::endl);
  return ffile;
}

bool cmCPackGenerator::ConfigureString(std::string const& inString,
                                       std::string& outString)
{
  this->MakefileMap->ConfigureString(inString, outString, true, false);
  return true;
}

bool cmCPackGenerator::ConfigureFile(std::string const& inName,
                                     std::string const& outName,
                                     bool copyOnly /* = false */)
{
  return this->MakefileMap->ConfigureFile(inName, outName, copyOnly, true,
                                          false) == 1;
}

int cmCPackGenerator::CleanTemporaryDirectory()
{
  std::string tempInstallDirectory =
    this->GetOption("CPACK_TEMPORARY_DIRECTORY");
  if (cmsys::SystemTools::FileExists(tempInstallDirectory)) {
    cmCPackLogger(cmCPackLog::LOG_OUTPUT,
                  "- Clean temporary : " << tempInstallDirectory << std::endl);
    if (!cmSystemTools::RepeatedRemoveDirectory(tempInstallDirectory)) {
      cmCPackLogger(cmCPackLog::LOG_ERROR,
                    "Problem removing temporary directory: "
                      << tempInstallDirectory << std::endl);
      return 0;
    }
  }
  return 1;
}

cmInstalledFile const* cmCPackGenerator::GetInstalledFile(
  std::string const& name) const
{
  cmake const* cm = this->MakefileMap->GetCMakeInstance();
  return cm->GetInstalledFile(name);
}

int cmCPackGenerator::PrepareGroupingKind()
{
  // find a component package method specified by the user
  ComponentPackageMethod method = UNKNOWN_COMPONENT_PACKAGE_METHOD;

  if (this->GetOption("CPACK_COMPONENTS_ALL_IN_ONE_PACKAGE")) {
    method = ONE_PACKAGE;
  }

  if (this->GetOption("CPACK_COMPONENTS_IGNORE_GROUPS")) {
    method = ONE_PACKAGE_PER_COMPONENT;
  }

  if (this->GetOption("CPACK_COMPONENTS_ONE_PACKAGE_PER_GROUP")) {
    method = ONE_PACKAGE_PER_GROUP;
  }

  // Second way to specify grouping
  std::string groupingType = *this->GetOption("CPACK_COMPONENTS_GROUPING");

  if (!groupingType.empty()) {
    cmCPackLogger(cmCPackLog::LOG_VERBOSE,
                  "[" << this->Name << "]"
                      << " requested component grouping = " << groupingType
                      << std::endl);
    if (groupingType == "ALL_COMPONENTS_IN_ONE") {
      method = ONE_PACKAGE;
    } else if (groupingType == "IGNORE") {
      method = ONE_PACKAGE_PER_COMPONENT;
    } else if (groupingType == "ONE_PER_GROUP") {
      method = ONE_PACKAGE_PER_GROUP;
    } else {
      cmCPackLogger(
        cmCPackLog::LOG_WARNING,
        "[" << this->Name << "]"
            << " requested component grouping type <" << groupingType
            << "> UNKNOWN not in (ALL_COMPONENTS_IN_ONE,IGNORE,ONE_PER_GROUP)"
            << std::endl);
    }
  }

  // Some components were defined but NO group
  // fallback to default if not group based
  if (method == ONE_PACKAGE_PER_GROUP && this->ComponentGroups.empty() &&
      !this->Components.empty()) {
    if (this->componentPackageMethod == ONE_PACKAGE) {
      method = ONE_PACKAGE;
    } else {
      method = ONE_PACKAGE_PER_COMPONENT;
    }
    cmCPackLogger(
      cmCPackLog::LOG_WARNING,
      "[" << this->Name << "]"
          << " One package per component group requested, "
          << "but NO component groups exist: Ignoring component group."
          << std::endl);
  }

  // if user specified packaging method, override the default packaging
  // method
  if (method != UNKNOWN_COMPONENT_PACKAGE_METHOD) {
    this->componentPackageMethod = method;
  }

  char const* method_names[] = { "ALL_COMPONENTS_IN_ONE", "IGNORE",
                                 "ONE_PER_GROUP", "UNKNOWN" };

  cmCPackLogger(cmCPackLog::LOG_VERBOSE,
                "[" << this->Name << "]"
                    << " requested component grouping = "
                    << method_names[this->componentPackageMethod]
                    << std::endl);

  return 1;
}

std::string cmCPackGenerator::GetSanitizedDirOrFileName(
  std::string const& name, bool isFullName) const
{
  if (isFullName) {
#ifdef _WIN32
    // Given name matches a reserved name (on Windows)?
    // Then return it prepended with an underscore.
    // See https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file
    cmsys::RegularExpression reserved_pattern("^("
                                              "[Cc][Oo][Nn]|"
                                              "[Pp][Rr][Nn]|"
                                              "[Aa][Uu][Xx]|"
                                              "[Nn][Uu][Ll]|"
                                              "[Cc][Oo][Mm][1-9]|"
                                              "[Ll][Pp][Tt][1-9]"
                                              ")[.]?$");
    if (reserved_pattern.find(name)) {
      return "_" + name;
    }
    // Given name ends in a dot (on Windows)?
    // Then return it appended with an underscore.
    if (name.back() == '.') {
      return name + '_';
    }
#endif
  }

#ifndef _WIN32
  constexpr char const* prohibited_chars = "<>\"/\\|?*`";
#else
  // Note: Windows also excludes the colon.
  constexpr char const* prohibited_chars = "<>\"/\\|?*`:";
#endif
  // Given name contains non-supported character?
  // Then return its MD5 hash.
  if (name.find_first_of(prohibited_chars) != std::string::npos) {
    cmCryptoHash hasher(cmCryptoHash::AlgoMD5);
    return hasher.HashString(name);
  }

  // Otherwise return unmodified.
  return name;
}

std::string cmCPackGenerator::GetComponentInstallSuffix(
  std::string const& componentName)
{
  return componentName;
}

std::string cmCPackGenerator::GetComponentInstallDirNameSuffix(
  std::string const& componentName)
{
  return this->GetSanitizedDirOrFileName(
    this->GetComponentInstallSuffix(componentName));
}

std::string cmCPackGenerator::GetComponentPackageFileName(
  std::string const& initialPackageFileName,
  std::string const& groupOrComponentName, bool isGroupName)
{

  /*
   * the default behavior is to use the
   * component [group] name as a suffix
   */
  std::string suffix = "-" + groupOrComponentName;
  /* check if we should use DISPLAY name */
  std::string dispNameVar =
    "CPACK_" + this->Name + "_USE_DISPLAY_NAME_IN_FILENAME";
  if (this->IsOn(dispNameVar)) {
    /* the component Group case */
    if (isGroupName) {
      std::string groupDispVar = "CPACK_COMPONENT_GROUP_" +
        cmSystemTools::UpperCase(groupOrComponentName) + "_DISPLAY_NAME";
      cmValue groupDispName = this->GetOption(groupDispVar);
      if (groupDispName) {
        suffix = "-" + *groupDispName;
      }
    }
    /* the [single] component case */
    else {
      std::string dispVar = "CPACK_COMPONENT_" +
        cmSystemTools::UpperCase(groupOrComponentName) + "_DISPLAY_NAME";
      cmValue dispName = this->GetOption(dispVar);
      if (dispName) {
        suffix = "-" + *dispName;
      }
    }
  }
  return initialPackageFileName + suffix;
}

enum cmCPackGenerator::CPackSetDestdirSupport
cmCPackGenerator::SupportsSetDestdir() const
{
  return cmCPackGenerator::SETDESTDIR_SUPPORTED;
}

bool cmCPackGenerator::SupportsAbsoluteDestination() const
{
  return true;
}

bool cmCPackGenerator::SupportsComponentInstallation() const
{
  return false;
}

bool cmCPackGenerator::WantsComponentInstallation() const
{
  return (!this->IsOn("CPACK_MONOLITHIC_INSTALL") &&
          this->SupportsComponentInstallation()
          // check that we have at least one group or component
          && (!this->ComponentGroups.empty() || !this->Components.empty()));
}

cmCPackInstallationType* cmCPackGenerator::GetInstallationType(
  std::string const& projectName, std::string const& name)
{
  (void)projectName;
  bool hasInstallationType = this->InstallationTypes.count(name) != 0;
  cmCPackInstallationType* installType = &this->InstallationTypes[name];
  if (!hasInstallationType) {
    // Define the installation type
    std::string macroPrefix =
      "CPACK_INSTALL_TYPE_" + cmsys::SystemTools::UpperCase(name);
    installType->Name = name;

    cmValue displayName = this->GetOption(macroPrefix + "_DISPLAY_NAME");
    if (cmNonempty(displayName)) {
      installType->DisplayName = *displayName;
    } else {
      installType->DisplayName = installType->Name;
    }

    installType->Index = static_cast<unsigned>(this->InstallationTypes.size());
  }
  return installType;
}

cmCPackComponent* cmCPackGenerator::GetComponent(
  std::string const& projectName, std::string const& name)
{
  bool hasComponent = this->Components.count(name) != 0;
  cmCPackComponent* component = &this->Components[name];
  if (!hasComponent) {
    // Define the component
    std::string macroPrefix =
      "CPACK_COMPONENT_" + cmsys::SystemTools::UpperCase(name);
    component->Name = name;
    cmValue displayName = this->GetOption(macroPrefix + "_DISPLAY_NAME");
    if (cmNonempty(displayName)) {
      component->DisplayName = *displayName;
    } else {
      component->DisplayName = component->Name;
    }
    component->IsHidden = this->IsOn(macroPrefix + "_HIDDEN");
    component->IsRequired = this->IsOn(macroPrefix + "_REQUIRED");
    component->IsDisabledByDefault = this->IsOn(macroPrefix + "_DISABLED");
    component->IsDownloaded = this->IsOn(macroPrefix + "_DOWNLOADED") ||
      this->GetOption("CPACK_DOWNLOAD_ALL").IsOn();

    cmValue archiveFile = this->GetOption(macroPrefix + "_ARCHIVE_FILE");
    if (cmNonempty(archiveFile)) {
      component->ArchiveFile = *archiveFile;
    }

    cmValue plist = this->GetOption(macroPrefix + "_PLIST");
    if (cmNonempty(plist)) {
      component->Plist = *plist;
    }

    cmValue groupName = this->GetOption(macroPrefix + "_GROUP");
    if (cmNonempty(groupName)) {
      component->Group = this->GetComponentGroup(projectName, *groupName);
      component->Group->Components.push_back(component);
    } else {
      component->Group = nullptr;
    }

    cmValue description = this->GetOption(macroPrefix + "_DESCRIPTION");
    if (cmNonempty(description)) {
      component->Description = *description;
    }

    // Determine the installation types.
    cmValue installTypes = this->GetOption(macroPrefix + "_INSTALL_TYPES");
    if (cmNonempty(installTypes)) {
      cmList installTypesList{ installTypes };
      for (auto const& installType : installTypesList) {
        component->InstallationTypes.push_back(
          this->GetInstallationType(projectName, installType));
      }
    }

    // Determine the component dependencies.
    cmValue depends = this->GetOption(macroPrefix + "_DEPENDS");
    if (cmNonempty(depends)) {
      cmList dependsList{ depends };
      for (auto const& depend : dependsList) {
        cmCPackComponent* child = this->GetComponent(projectName, depend);
        component->Dependencies.push_back(child);
        child->ReverseDependencies.push_back(component);
      }
    }
  }
  return component;
}

cmCPackComponentGroup* cmCPackGenerator::GetComponentGroup(
  std::string const& projectName, std::string const& name)
{
  (void)projectName;
  std::string macroPrefix =
    "CPACK_COMPONENT_GROUP_" + cmsys::SystemTools::UpperCase(name);
  bool hasGroup = this->ComponentGroups.count(name) != 0;
  cmCPackComponentGroup* group = &this->ComponentGroups[name];
  if (!hasGroup) {
    // Define the group
    group->Name = name;
    cmValue displayName = this->GetOption(macroPrefix + "_DISPLAY_NAME");
    if (cmNonempty(displayName)) {
      group->DisplayName = *displayName;
    } else {
      group->DisplayName = group->Name;
    }

    cmValue description = this->GetOption(macroPrefix + "_DESCRIPTION");
    if (cmNonempty(description)) {
      group->Description = *description;
    }
    group->IsBold = this->IsOn(macroPrefix + "_BOLD_TITLE");
    group->IsExpandedByDefault = this->IsOn(macroPrefix + "_EXPANDED");
    cmValue parentGroupName = this->GetOption(macroPrefix + "_PARENT_GROUP");
    if (cmNonempty(parentGroupName)) {
      group->ParentGroup =
        this->GetComponentGroup(projectName, *parentGroupName);
      group->ParentGroup->Subgroups.push_back(group);
    } else {
      group->ParentGroup = nullptr;
    }
  }
  return group;
}
