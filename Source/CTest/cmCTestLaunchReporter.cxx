/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#include "cmCTestLaunchReporter.h"

#include <utility>

#include "cmsys/FStream.hxx"
#include "cmsys/RegularExpression.hxx"

#include "cmCryptoHash.h"
#include "cmGeneratedFileStream.h"
#include "cmStringAlgorithms.h"
#include "cmSystemTools.h"
#include "cmXMLWriter.h"

#ifdef _WIN32
#  include <cstdio> // for std{out,err} and fileno

#  include <fcntl.h> // for _O_BINARY
#  include <io.h>    // for _setmode
#endif

cmCTestLaunchReporter::cmCTestLaunchReporter()
{
  this->Passthru = true;
  this->Status.Finished = true;
  this->ExitCode = 1;
  this->CWD = cmSystemTools::GetLogicalWorkingDirectory();

  this->ComputeFileNames();

  // Common compiler warning formats.  These are much simpler than the
  // full log-scraping expressions because we do not need to extract
  // file and line information.
  this->RegexWarning.emplace_back("(^|[ :])[Ww][Aa][Rr][Nn][Ii][Nn][Gg]");
  this->RegexWarning.emplace_back("(^|[ :])[Rr][Ee][Mm][Aa][Rr][Kk]");
  this->RegexWarning.emplace_back("(^|[ :])[Nn][Oo][Tt][Ee]");
}

cmCTestLaunchReporter::~cmCTestLaunchReporter()
{
  if (!this->Passthru) {
    cmSystemTools::RemoveFile(this->LogOut);
    cmSystemTools::RemoveFile(this->LogErr);
  }
}

void cmCTestLaunchReporter::ComputeFileNames()
{
  // We just passthru the behavior of the real command unless the
  // CTEST_LAUNCH_LOGS environment variable is set.
  std::string d;
  if (!cmSystemTools::GetEnv("CTEST_LAUNCH_LOGS", d) || d.empty()) {
    return;
  }
  this->Passthru = false;

  // The environment variable specifies the directory into which we
  // generate build logs.
  this->LogDir = d;
  cmSystemTools::ConvertToUnixSlashes(this->LogDir);
  this->LogDir += "/";

  // We hash the input command working dir and command line to obtain
  // a repeatable and (probably) unique name for log files.
  cmCryptoHash md5(cmCryptoHash::AlgoMD5);
  md5.Initialize();
  md5.Append(this->CWD);
  for (std::string const& realArg : this->RealArgs) {
    md5.Append(realArg);
  }
  this->LogHash = md5.FinalizeHex();

  // We store stdout and stderr in temporary log files.
  this->LogOut = cmStrCat(this->LogDir, "launch-", this->LogHash, "-out.txt");
  this->LogErr = cmStrCat(this->LogDir, "launch-", this->LogHash, "-err.txt");
}

void cmCTestLaunchReporter::LoadLabels()
{
  if (this->OptionBuildDir.empty() || this->OptionTargetName.empty()) {
    return;
  }

  // Labels are listed in per-target files.
  std::string fname = cmStrCat(this->OptionBuildDir, "/CMakeFiles/",
                               this->OptionTargetName, ".dir/Labels.txt");

  // We are interested in per-target labels for this source file.
  std::string source = this->OptionSource;
  cmSystemTools::ConvertToUnixSlashes(source);

  // Load the labels file.
  cmsys::ifstream fin(fname.c_str(), std::ios::in | std::ios::binary);
  if (!fin) {
    return;
  }
  bool inTarget = true;
  bool inSource = false;
  std::string line;
  while (cmSystemTools::GetLineFromStream(fin, line)) {
    if (line.empty() || line[0] == '#') {
      // Ignore blank and comment lines.
      continue;
    }
    if (line[0] == ' ') {
      // Label lines appear indented by one space.
      if (inTarget || inSource) {
        this->Labels.insert(line.substr(1));
      }
    } else if (!this->OptionSource.empty() && !inSource) {
      // Non-indented lines specify a source file name.  The first one
      // is the end of the target-wide labels.  Use labels following a
      // matching source.
      inTarget = false;
      inSource = this->SourceMatches(line, source);
    } else {
      return;
    }
  }
}

bool cmCTestLaunchReporter::SourceMatches(std::string const& lhs,
                                          std::string const& rhs)
{
  // TODO: Case sensitivity, UseRelativePaths, etc.  Note that both
  // paths in the comparison get generated by CMake.  This is done for
  // every source in the target, so it should be efficient (cannot use
  // cmSystemTools::IsSameFile).
  return lhs == rhs;
}

bool cmCTestLaunchReporter::IsError() const
{
  return this->ExitCode != 0;
}

void cmCTestLaunchReporter::WriteXML()
{
  // Name the xml file.
  std::string logXML =
    cmStrCat(this->LogDir, this->IsError() ? "error-" : "warning-",
             this->LogHash, ".xml");

  // Use cmGeneratedFileStream to atomically create the report file.
  cmGeneratedFileStream fxml(logXML);
  cmXMLWriter xml(fxml, 2);
  cmXMLElement e2(xml, "Failure");
  e2.Attribute("type", this->IsError() ? "Error" : "Warning");
  this->WriteXMLAction(e2);
  this->WriteXMLCommand(e2);
  this->WriteXMLResult(e2);
  this->WriteXMLLabels(e2);
}

void cmCTestLaunchReporter::WriteXMLAction(cmXMLElement& e2) const
{
  e2.Comment("Meta-information about the build action");
  cmXMLElement e3(e2, "Action");

  // TargetName
  if (!this->OptionTargetName.empty()) {
    e3.Element("TargetName", this->OptionTargetName);
  }

  // Language
  if (!this->OptionLanguage.empty()) {
    e3.Element("Language", this->OptionLanguage);
  }

  // SourceFile
  if (!this->OptionSource.empty()) {
    std::string source = this->OptionSource;
    cmSystemTools::ConvertToUnixSlashes(source);

    // If file is in source tree use its relative location.
    if (cmSystemTools::FileIsFullPath(this->SourceDir) &&
        cmSystemTools::FileIsFullPath(source) &&
        cmSystemTools::IsSubDirectory(source, this->SourceDir)) {
      source = cmSystemTools::RelativePath(this->SourceDir, source);
    }

    e3.Element("SourceFile", source);
  }

  // OutputFile
  if (!this->OptionOutput.empty()) {
    e3.Element("OutputFile", this->OptionOutput);
  }

  // OutputType
  char const* outputType = nullptr;
  if (!this->OptionTargetType.empty()) {
    if (this->OptionTargetType == "EXECUTABLE") {
      outputType = "executable";
    } else if (this->OptionTargetType == "SHARED_LIBRARY") {
      outputType = "shared library";
    } else if (this->OptionTargetType == "MODULE_LIBRARY") {
      outputType = "module library";
    } else if (this->OptionTargetType == "STATIC_LIBRARY") {
      outputType = "static library";
    }
  } else if (!this->OptionSource.empty()) {
    outputType = "object file";
  }
  if (outputType) {
    e3.Element("OutputType", outputType);
  }
}

void cmCTestLaunchReporter::WriteXMLCommand(cmXMLElement& e2)
{
  e2.Comment("Details of command");
  cmXMLElement e3(e2, "Command");
  if (!this->CWD.empty()) {
    e3.Element("WorkingDirectory", this->CWD);
  }
  for (std::string const& realArg : this->RealArgs) {
    e3.Element("Argument", realArg);
  }
}

void cmCTestLaunchReporter::WriteXMLResult(cmXMLElement& e2)
{
  e2.Comment("Result of command");
  cmXMLElement e3(e2, "Result");

  // StdOut
  this->DumpFileToXML(e3, "StdOut", this->LogOut);

  // StdErr
  this->DumpFileToXML(e3, "StdErr", this->LogErr);

  // ExitCondition
  cmXMLElement e4(e3, "ExitCondition");
  if (this->Status.Finished) {
    auto exception = this->Status.GetException();
    switch (exception.first) {
      case cmUVProcessChain::ExceptionCode::None:
        e4.Content(this->ExitCode);
        break;
      case cmUVProcessChain::ExceptionCode::Spawn:
        e4.Content("Error administrating child process: ");
        e4.Content(exception.second);
        break;
      default:
        e4.Content("Terminated abnormally: ");
        e4.Content(exception.second);
        break;
    }
  } else {
    e4.Content("Killed when timeout expired");
  }
}

void cmCTestLaunchReporter::WriteXMLLabels(cmXMLElement& e2)
{
  this->LoadLabels();
  if (!this->Labels.empty()) {
    e2.Comment("Interested parties");
    cmXMLElement e3(e2, "Labels");
    for (std::string const& label : this->Labels) {
      e3.Element("Label", label);
    }
  }
}

void cmCTestLaunchReporter::DumpFileToXML(cmXMLElement& e3, char const* tag,
                                          std::string const& fname)
{
  cmsys::ifstream fin(fname.c_str(), std::ios::in | std::ios::binary);

  std::string line;
  char const* sep = "";

  cmXMLElement e4(e3, tag);
  while (cmSystemTools::GetLineFromStream(fin, line)) {
    if (this->MatchesFilterPrefix(line)) {
      continue;
    }
    if (this->Match(line, this->RegexWarningSuppress)) {
      line = cmStrCat("[CTest: warning suppressed] ", line);
    } else if (this->Match(line, this->RegexWarning)) {
      line = cmStrCat("[CTest: warning matched] ", line);
    }
    e4.Content(sep);
    e4.Content(line);
    sep = "\n";
  }
}

bool cmCTestLaunchReporter::Match(
  std::string const& line, std::vector<cmsys::RegularExpression>& regexps)
{
  for (cmsys::RegularExpression& r : regexps) {
    if (r.find(line)) {
      return true;
    }
  }
  return false;
}

bool cmCTestLaunchReporter::MatchesFilterPrefix(std::string const& line) const
{
  return !this->OptionFilterPrefix.empty() &&
    cmHasPrefix(line, this->OptionFilterPrefix);
}
