
#pragma once

#include <cstddef>
#include <memory>
#include <string>


namespace dpso::ocr {


class OcrEngine;
class OcrEngineArgs;


struct OcrEngineInfo {
    /**
     * Id.
     *
     * Unique id of the engine consisting of lower-case ASCII
     * alphanumeric characters and underscores to separate words.
     */
    std::string id;

    /**
     * Human-readable engine name.
     *
     * Unlike id, the name doesn't have any restrictions.
     */
    std::string name;

    /**
     * Engine version.
     *
     * If the OCR engine library is linked dynamically, this should
     * be the runtime version, if possible. May be empty if the engine
     * doesn't provide version information.
     */
    std::string version;

    /**
     * What OcrEngineArgs::dataDir the engine prefers.
     */
    enum class DataDirPreference {
        /**
         * The engine doesn't use external data at all.
         *
         * OcrEngineArgs::dataDir is ignored.
         */
        noDataDir,

        /**
         * The engine prefers the default data directory.
         *
         * Usually this means that on the current platform, the data
         * is installed in a system-wide path that is hardcoded into
         * the OCR library at build time.
         */
        preferDefault,

        /**
         * The engine prefers an explicit path to data directory.
         *
         * This is normally the default mode for non-Unix platforms,
         * where the data is installed in an application-specific
         * directory.
         */
        preferExplicit,
    };

    DataDirPreference dataDirPreference;
};


class OcrEngineCreator {
public:
    static const OcrEngineCreator& get(std::size_t idx);
    static std::size_t getCount();

    virtual ~OcrEngineCreator() = default;

    virtual const OcrEngineInfo& getInfo() const = 0;

    /**
     * Create OCR engine.
     *
     * \throws OcrEngineError
     */
    virtual std::unique_ptr<OcrEngine> create(
        const OcrEngineArgs& args) const = 0;
};


}