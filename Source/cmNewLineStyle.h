/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file LICENSE.rst or https://cmake.org/licensing for details.  */
#pragma once

#include "cmConfigure.h" // IWYU pragma: keep

#include <string>
#include <vector>

class cmNewLineStyle
{
public:
  cmNewLineStyle();

  enum Style
  {
    Invalid,
    // LF = '\n', 0x0A, 10
    // CR = '\r', 0x0D, 13
    LF,  // Unix
    CRLF // Dos
  };

  void SetStyle(Style);
  Style GetStyle() const;

  bool IsValid() const;

  bool ReadFromArguments(std::vector<std::string> const& args,
                         std::string& errorString);

  std::string GetCharacters() const;

private:
  Style NewLineStyle = Invalid;
};
