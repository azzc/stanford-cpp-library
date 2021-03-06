/*
 * File: stylecheck.cpp
 * --------------------
 * This file contains declarations of code to perform regex-based rough style
 * checking on C++ code.
 * See sylecheck.h for documentation of each function.
 * 
 * @author Marty Stepp
 * @version 2014/11/15
 * - added warning description to top of window if 'omitOnPass' is turned on
 * @version 2014/10/31
 * - added support for graphical style checker output
 * @since 2014/10/14
 */

#include "stylecheck.h"
#include <cstring>
#include "autograder.h"
#include "filelib.h"
#include "gtest-marty.h"
#include "rapidxml.h"
#include "regexpr.h"
#include "stringutils.h"
#include "strlib.h"
#include "xmlutils.h"
#include "private/platform.h"

namespace stylecheck {

static const int DEFAULT_MIN_COUNT = 0;
static const int DEFAULT_MAX_COUNT = 999999999;

static bool processPatternNode(const std::string& codeFileName, rapidxml::xml_node<>* patternNode, const std::string& categoryName, const std::string& codeFileText, bool omitOnPass) {
    std::ostringstream out;
    std::string patternRegex = xmlutils::getAttribute(patternNode, "regex");
    patternRegex = stringReplace(patternRegex, "(:IDENTIFIER:)", "(?:[a-zA-Z_$][a-zA-Z0-9_$]{0,255})");
    patternRegex = stringReplace(patternRegex, "(:IDENT:)", "(?:[a-zA-Z_$][a-zA-Z0-9_$]{0,255})");
    patternRegex = stringReplace(patternRegex, "(:SPACES:)", "(?:[ \\t]{0,999})");
    patternRegex = stringReplace(patternRegex, "(:SPACE:)", "(?:[ \\t])");
    patternRegex = stringReplace(patternRegex, "(:TEMPLATE:)", "(?:&lt;[ \t]{0,255}[a-zA-Z_$][a-zA-Z0-9_$]{0,255}[ \t]{0,255}&gt;)");

    std::string patternDescription = xmlutils::getAttribute(patternNode, "description", patternRegex);
    int patternMinCount = xmlutils::getAttributeInt(patternNode, "mincount", DEFAULT_MIN_COUNT);
    int patternMaxCount = xmlutils::getAttributeInt(patternNode, "maxcount", DEFAULT_MAX_COUNT);
    int patternCount = xmlutils::getAttributeInt(patternNode, "count", -1);
    if (patternCount != -1) {
        patternMinCount = patternMaxCount = patternCount;
    }
    bool patternList = xmlutils::getAttributeBool(patternNode, "list", true);
    bool showCounts = xmlutils::getAttributeBool(patternNode, "showcounts", true);
    
    std::string failType = "";   // default
    if (xmlutils::hasAttribute(patternNode, "failtype")) {
        failType = trim(xmlutils::getAttribute(patternNode, "failtype"));
    }
    if (failType.empty()) {
        failType = "warn";   // default
    }

    // see if student's code text matches the regex
    std::string linesStr;
    int matchCount;
    if (patternList) {
        matchCount = stringutils::regexMatchCountWithLines(codeFileText, patternRegex, linesStr);
    } else {
        matchCount = regexMatchCount(codeFileText, patternRegex);
    }
    
    std::string rangeStr = "";
    if (patternMinCount == patternMaxCount) {
        rangeStr = "should occur exactly " + integerToString(patternMinCount) + " times";
    } else if (patternMinCount == 0 && patternMaxCount > 0 && patternMaxCount != DEFAULT_MAX_COUNT) {
        rangeStr = "should occur <= " + integerToString(patternMaxCount) + " times";
    } else if (patternMaxCount == DEFAULT_MAX_COUNT && patternMinCount > 0) {
        rangeStr = "should occur >= " + integerToString(patternMinCount) + " times";
    } else {
        rangeStr = "should be between " + integerToString(patternMinCount) + "-" + integerToString(patternMaxCount) + " times";
    }
    bool pass = true;
    if (patternMinCount > 0 && matchCount <= 0) {
        pass = false;
    } else if (patternMaxCount == 0 && matchCount > 0) {
        pass = false;
    } else if (!(patternMinCount <= matchCount && matchCount <= patternMaxCount)) {
        pass = false;
    }
    
    std::string prefix = "";
    prefix += "[" + codeFileName + "] ";

    if (!pass || !omitOnPass) {
        out << "    STYLE CHECK " << (pass ? "PASSED : " : "WARNING: ") << patternDescription << std::endl;
        if (autograder::isGraphicalUI()) {
            std::string resultStr = pass ? "pass" : failType;
            stanfordcpplib::getPlatform()->autograderunittest_addTest(prefix + patternDescription, prefix + categoryName, /* styleCheck */ true);
            stanfordcpplib::getPlatform()->autograderunittest_setTestResult(prefix + patternDescription, resultStr, /* styleCheck */ true);
            autograder::UnitTestDetails deets;
            deets.message = patternDescription;
            deets.passed = pass;
            deets.expected = rangeStr;
            deets.student = "actually occurs " + integerToString(matchCount) + " time(s)";
            if ((int) linesStr.length() > 0) {
                deets.student += " on line " + linesStr;
            }
            deets.testType = autograder::UnitTestType::TEST_STYLE_CHECK;
            deets.valueType = "T";
            out.str("");
            out << deets;
            stanfordcpplib::getPlatform()->autograderunittest_setTestDetails(prefix + patternDescription, out.str(), /* styleCheck */ true);
            out.str("");
        } else {
            if (showCounts) {
                out << "         " << rangeStr << std::endl;
                out << "         actually occurs " << matchCount << " time(s)";
                if ((int) linesStr.length() > 0) {
                    out << " on line " << linesStr;
                }
                autograder::showOutput(out, /* graphical */ false, /* console */ true);
            }
        }
    }
    
    return pass;
}

/*
 * <stylecheck type="text" filename="life.cpp" omitonpass="true">
 *     <pattern regex="(\/\/.*)|(\/\*([^*]|([*][^\/])\r?\n?)*\*\/)" mincount="18" description="comments" list="false" />
 *      ...
 * </stylecheck>
 */
void styleCheck(std::string codeFileName, std::string styleXmlFileName, bool printWarning) {
    std::string codeFileText = readEntireFile(codeFileName);
    rapidxml::xml_node<>* styleCheckNode = xmlutils::openXmlDocument(styleXmlFileName, "stylecheck");
    bool omitOnPass = xmlutils::getAttributeBool(styleCheckNode, "omitonpass", true);

    std::ostringstream out;
    out << "STYLE CHECK for " << codeFileName << " based on rules in "
        << styleXmlFileName << ":" << std::endl;
    autograder::showOutput(out, /* graphical */ false, /* console */ true);
    if (printWarning) {
        out << "  (Note: These are just heuristics and suggestions, not hard rules." << std::endl;
        out << "         The style checker isn't able to check everything." << std::endl;
        out << "         Please look at the student's code and don't penalize them" << std::endl;
        out << "         unless you actually see a problem with their coding style.)" << std::endl;
        out << std::endl;
        if (omitOnPass) {
            out << "<br><br>(Note: Showing only the output of style checks that fail. Passing checks are omitted.)" << std::endl;
            out << std::endl;
        }
        if (autograder::isGraphicalUI()) {
            stanfordcpplib::getPlatform()->autograderunittest_clearTests(/* styleCheck */ true);
            stanfordcpplib::getPlatform()->autograderunittest_setWindowDescriptionText(out.str(), /* styleCheck */ true);
            out.str("");
        }
    }
    autograder::showOutput(out, /* graphical */ false, /* console */ true);

    if (!fileExists(codeFileName)) {
        out << "*** ERROR: Student code file \"" << codeFileName
            << "\" not found in build folder. Exiting." << std::endl;
        autograder::showOutput(out);
        return;
    }
    if (!fileExists(styleXmlFileName)) {
        out << "*** ERROR: XML style checklist file \"" << styleXmlFileName
            << "\" not found in build folder. Exiting." << std::endl;
        autograder::showOutput(out);
        return;
    }

    // loop over each pattern node
    // handle each pattern node embedded directly within the document element of 'stylecheck'
    int testCount = 0;
    int passCount = 0;
    for (rapidxml::xml_node<>* patternNode : xmlutils::getChildNodes(styleCheckNode, "pattern")) {
        testCount++;
        if (processPatternNode(codeFileName, patternNode, /* categoryName */ "", codeFileText, omitOnPass)) {
            passCount++;
        }
    }

    // handle each pattern node embedded within a 'category' node
    for (rapidxml::xml_node<>* categoryNode : xmlutils::getChildNodes(styleCheckNode, "category")) {
        std::string categoryName = xmlutils::getAttribute(categoryNode, "name");
        for (rapidxml::xml_node<>* patternNode : xmlutils::getChildNodes(categoryNode, "pattern")) {
            testCount++;
            if (processPatternNode(codeFileName, patternNode, categoryName, codeFileText, omitOnPass)) {
                passCount++;
            }
            autograder::setTestCounts(passCount, testCount, /* isStyleCheck */ true);
        }
    }

    out << "    STYLE CHECK: passed " << passCount << " of " << testCount << " checks." << std::endl;
    autograder::showOutput(out, /* graphical */ false, /* console */ true);
}
} // namespace stylecheck
