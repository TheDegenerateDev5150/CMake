/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file LICENSE.rst or https://cmake.org/licensing for details.  */

#include "cmCTestTestMeasurementXMLParser.h"

#include <cstring>

void cmCTestTestMeasurementXMLParser::StartElement(std::string const& name,
                                                   char const** attributes)
{
  this->CharacterData.clear();
  this->ElementName = name;
  for (char const** attr = attributes; *attr; attr += 2) {
    if (strcmp(attr[0], "name") == 0) {
      this->MeasurementName = attr[1];
    } else if (strcmp(attr[0], "type") == 0) {
      this->MeasurementType = attr[1];
    }
  }
}

void cmCTestTestMeasurementXMLParser::CharacterDataHandler(char const* data,
                                                           int length)
{
  this->CharacterData.append(data, length);
}
