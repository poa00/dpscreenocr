
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "dpso/error.h"
#include "dpso_utils/history.h"

#include "flow.h"
#include "utils.h"


static void cmpFields(
    const char* name, const char* a, const char* b, int line)
{
    if (std::strcmp(a, b) == 0)
        return;

    std::fprintf(
        stderr,
        "line %i: Fields \"%s\" don't match: \"%s\" != \"%s\"\n",
        line,
        name,
        test::utils::escapeStr(a).c_str(),
        test::utils::escapeStr(b).c_str());
    test::failure();
}


static void cmpEntries(
    const DpsoHistoryEntry& a, const DpsoHistoryEntry& b, int line)
{
    #define CMP(name) cmpFields(#name, a.name, b.name, line)

    CMP(text);
    CMP(timestamp);

    #undef CMP
}


#define CMP_ENTRIES(a, b) cmpEntries(a, b, __LINE__)


static void testCount(int expected, int line)
{
    const auto got = dpsoHistoryCount();
    if (got == expected)
        return;

    std::fprintf(
        stderr,
        "line %i: dpsoHistoryCount(): Expected %i, got %i\n",
        line,
        expected,
        got);
    test::failure();
}


#define TEST_COUNT(expected) testCount(expected, __LINE__)


const char* const historyFileName = "test_history.txt";


static void testNormalIO()
{
    static const DpsoHistoryEntry entries[] = {
        {
            " text1 \n\t\r line1\n\t\r line2 \n\t\r ",
            "timestamp1"
        },
        {
            " text2 \n\t\r line1\n\t\r line2 \n\t\r ",
            "timestamp2"
        },
        {
            "",
            ""
        },
        {
            " text4 \n\t\r line1\n\t\r line2 \n\t\r ",
            "timestamp4"
        },
    };

    static const auto numEntries = sizeof(entries) / sizeof(*entries);

    dpsoHistoryClear();
    TEST_COUNT(0);

    for (int i = 0; i < static_cast<int>(numEntries); ++i) {
        const auto& inEntry = entries[i];

        dpsoHistoryAppend(&inEntry);
        TEST_COUNT(i + 1);

        DpsoHistoryEntry outEntry;
        dpsoHistoryGet(i, &outEntry);
        CMP_ENTRIES(inEntry, outEntry);
    }

    if (!dpsoHistorySave(historyFileName)) {
        std::fprintf(
            stderr,
            "testHistory(): dpsoHistorySave(\"%s\") failed: %s\n",
            historyFileName,
            dpsoGetError());
        std::exit(EXIT_FAILURE);
    }

    dpsoHistoryClear();
    TEST_COUNT(0);

    const auto loaded = dpsoHistoryLoad(historyFileName);
    std::remove(historyFileName);

    if (!loaded) {
        std::fprintf(
            stderr,
            "testHistory(): dpsoHistoryLoad(\"%s\") failed: %s\n",
            historyFileName,
            dpsoGetError());
        std::exit(EXIT_FAILURE);
    }

    TEST_COUNT(numEntries);

    for (int i = 0; i < static_cast<int>(numEntries); ++i) {
        const auto& inEntry = entries[i];

        DpsoHistoryEntry outEntry;
        dpsoHistoryGet(i, &outEntry);
        CMP_ENTRIES(inEntry, outEntry);
    }
}


void testInvalidData()
{
    struct Test {
        const char* description;
        const char* data;
    };

    const Test tests[] = {
        {"No timestamp terminator", "a\f\n"},
        {"Invalid timestamp terminator", "a\nb\f\n"},
        {"Truncated text terminator", "a\n\nb\f"},
        {"Invalid text terminator", "a\n\nb\fa"},
    };

    for (const auto& test : tests) {
        auto* fp = std::fopen(historyFileName, "wb");
        if (!fp) {
            std::fprintf(
                stderr,
                "loadInvalidData(): "
                "fopen(\"%s\", \"wb\") failed: %s\n",
                historyFileName,
                std::strerror(errno));
            std::exit(EXIT_FAILURE);
        }

        std::fputs(test.data, fp);
        std::fclose(fp);

        const auto loaded = dpsoHistoryLoad(historyFileName);
        std::remove(historyFileName);

        if (!loaded)
            continue;

        std::fprintf(
            stderr,
            "loadInvalidData(): dpsoHistoryLoad() doesn't fail in "
            "\"%s\" case\n",
            test.description);
        test::failure();
    }
}


static void testHistory()
{
    testNormalIO();
    testInvalidData();
}


REGISTER_TEST(testHistory);
