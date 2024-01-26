
#include "cfg.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <fmt/core.h>

#include "dpso_utils/error_set.h"
#include "dpso_utils/os.h"
#include "dpso_utils/str.h"


using namespace dpso;


struct DpsoCfg {
    struct KeyValue {
        std::string key;
        std::string value;
    };

    std::vector<KeyValue> keyValues;
};


DpsoCfg* dpsoCfgCreate(void)
{
    return new DpsoCfg{};
}


void dpsoCfgDelete(DpsoCfg* cfg)
{
    delete cfg;
}


static int cmpKeys(const char* a, const char* b)
{
    return str::cmp(a, b, str::cmpIgnoreCase);
}


template<typename T>
static auto getLowerBound(T& keyValues, const char* key)
{
    return std::lower_bound(
        keyValues.begin(), keyValues.end(), key,
        [](const DpsoCfg::KeyValue& kv, const char* key)
        {
            return cmpKeys(kv.key.c_str(), key) < 0;
        });
}


static void parseKeyValue(const char* str, DpsoCfg::KeyValue& kv)
{
    const auto* keyBegin = str;
    while (str::isBlank(*keyBegin))
        ++keyBegin;

    const auto* keyEnd = keyBegin;
    while (*keyEnd && !str::isBlank(*keyEnd))
        ++keyEnd;

    kv.key.assign(keyBegin, keyEnd);

    const auto* valueBegin = keyEnd;
    while (str::isBlank(*valueBegin))
        ++valueBegin;

    kv.value.clear();

    // We will trim any unescaped trailing blanks. \ at the end is
    // ignored, which allows to explicitly mark the end of the value
    // instead of escaping its last space.
    const auto* blanksBegin = valueBegin;
    const auto* blanksEnd = blanksBegin;

    for (const auto* s = valueBegin; *s;) {
        if (str::isBlank(*s)) {
            if (s != blanksEnd)
                blanksBegin = s;

            blanksEnd = ++s;
            continue;
        }

        if (s == blanksEnd)
            kv.value.append(blanksBegin, blanksEnd);

        if (const auto c = *s++; c != '\\') {
            kv.value += c;
            continue;
        }

        if (!*s)
            break;

        switch (const auto c = *s++) {
        case 'n':
            kv.value += '\n';
            break;
        case 'r':
            kv.value += '\r';
            break;
        case 't':
            kv.value += '\t';
            break;
        default:
            kv.value += c;
            break;
        }
    }
}


// Our file format allows using any byte values, so we don't use the
// text mode (i.e. fopen() without the "b" flag) for IO - neither for
// reading nor for writing - to avoid surprises when we actually need
// to handle "binary" data (for example, the text mode on Windows
// treats 0x1a as EOF when reading). We will still write CRLF line
// endings on Windows to make Notepad users happy.


bool dpsoCfgLoad(DpsoCfg* cfg, const char* filePath)
{
    if (!cfg) {
        setError("cfg is null");
        return false;
    }

    cfg->keyValues.clear();

    os::StdFileUPtr fp{os::fopen(filePath, "rb")};
    if (!fp) {
        if (errno == ENOENT)
            return true;

        setError(
            "os::fopen(..., \"rb\"): {}", os::getErrnoMsg(errno));
        return false;
    }

    std::string line;
    DpsoCfg::KeyValue kv;
    while (true) {
        try {
            if (!os::readLine(fp.get(), line))
                break;
        } catch (os::Error& e) {
            setError("os::readLine(): {}", e.what());
            cfg->keyValues.clear();
            return false;
        }

        parseKeyValue(line.c_str(), kv);
        if (!kv.key.empty())
            dpsoCfgSetStr(cfg, kv.key.c_str(), kv.value.c_str());
    }

    return true;
}


static void writeKeyValue(
    std::FILE* fp, const DpsoCfg::KeyValue& kv, std::size_t maxKeyLen)
{
    os::write(fp, fmt::format("{:{}} ", kv.key, maxKeyLen));

    if (!kv.value.empty() && kv.value.front() == ' ')
        os::write(fp, '\\');

    for (const auto* s = kv.value.c_str(); *s; ++s)
        switch (const auto c = *s) {
        case '\n':
            os::write(fp, "\\n");
            break;
        case '\r':
            os::write(fp, "\\r");
            break;
        case '\t':
            os::write(fp, "\\t");
            break;
        case '\\':
            os::write(fp, "\\\\");
            break;
        default:
            os::write(fp, c);
            break;
        }

    // If we have a single space, it's already escaped, but we still
    // append \ to make the end visible.
    if (!kv.value.empty() && kv.value.back() == ' ')
        os::write(fp, '\\');

    // Use CRLF on Windows to make Notepad users happy.
    #ifdef _WIN32
    os::write(fp, '\r');
    #endif

    os::write(fp, '\n');
}


bool dpsoCfgSave(const DpsoCfg* cfg, const char* filePath)
{
    if (!cfg) {
        setError("cfg is null");
        return false;
    }

    os::StdFileUPtr fp{os::fopen(filePath, "wb")};
    if (!fp) {
        setError(
            "os::fopen(..., \"wb\"): {}", os::getErrnoMsg(errno));
        return false;
    }

    std::size_t maxKeyLen{};
    for (const auto& kv : cfg->keyValues)
        maxKeyLen = std::max(maxKeyLen, kv.key.size());

    try {
        for (const auto& kv : cfg->keyValues)
            writeKeyValue(fp.get(), kv, maxKeyLen);
    } catch (os::Error& e) {
        setError("{}", e.what());
        return false;
    }

    return true;
}


void dpsoCfgClear(DpsoCfg* cfg)
{
    if (cfg)
        cfg->keyValues.clear();
}


bool dpsoCfgKeyExists(const DpsoCfg* cfg, const char* key)
{
    return dpsoCfgGetStr(cfg, key, nullptr) != nullptr;
}


const char* dpsoCfgGetStr(
    const DpsoCfg* cfg, const char* key, const char* defaultVal)
{
    if (!cfg)
        return defaultVal;

    const auto iter = getLowerBound(cfg->keyValues, key);
    if (iter != cfg->keyValues.end()
            && cmpKeys(iter->key.c_str(), key) == 0)
        return iter->value.c_str();

    return defaultVal;
}


static bool isValidKey(const char* key)
{
    return *key && !std::strpbrk(key, " \t\r\n");
}


void dpsoCfgSetStr(DpsoCfg* cfg, const char* key, const char* val)
{
    if (!cfg || !isValidKey(key))
        return;

    const auto iter = getLowerBound(cfg->keyValues, key);
    if (iter != cfg->keyValues.end() &&
            cmpKeys(iter->key.c_str(), key) == 0)
        iter->value = val;
    else
        cfg->keyValues.insert(iter, {key, val});
}


int dpsoCfgGetInt(const DpsoCfg* cfg, const char* key, int defaultVal)
{
    const auto* str = dpsoCfgGetStr(cfg, key, nullptr);
    if (!str)
        return defaultVal;

    // There's no sense to allow leading or trailing whitespace around
    // numbers, since it can only occur when explicitly included,
    // implying that the user probably really wanted a string.

    int result{};

    const auto* strEnd = str + std::strlen(str);
    const auto [ptr, ec] = std::from_chars(str, strEnd, result, 10);
    if (ec == std::errc{} && ptr == strEnd)
        return result;

    return defaultVal;
}


void dpsoCfgSetInt(DpsoCfg* cfg, const char* key, int val)
{
    dpsoCfgSetStr(cfg, key, dpso::str::toStr(val).c_str());
}


static const char* boolToStr(bool b)
{
    return b ? "true" : "false";
}


bool dpsoCfgGetBool(
    const DpsoCfg* cfg, const char* key, bool defaultVal)
{
    const auto* str = dpsoCfgGetStr(cfg, key, nullptr);
    if (!str)
        return defaultVal;

    for (int i = 0; i < 2; ++i)
        if (str::cmp(str, boolToStr(i), str::cmpIgnoreCase) == 0)
            return i;

    return defaultVal;
}


void dpsoCfgSetBool(DpsoCfg* cfg, const char* key, bool val)
{
    dpsoCfgSetStr(cfg, key, boolToStr(val));
}
