#ifndef FAST_PARSER_H
#define FAST_PARSER_H

#include <string>
#include "../instance/instance.hpp"

/*
 * Streaming reader for the ROADEF2020 instance format.
 *
 * The reference Parser builds a full nlohmann::json DOM, which needs ~10x the
 * file size in RAM (C_15, 1.5 GB, aborts with std::bad_alloc under the 16 GB
 * limit) and costs 35-45 s of the solve budget on the largest instances.
 *
 * FastParser mmaps the file and fills the Instance directly in two sequential
 * passes (one to discover section offsets and cardinalities, one to fill the
 * arrays), so extra memory is O(1) and the whole large-instance parse becomes
 * I/O bound. It reproduces Parser::process semantics exactly.
 */
class FastParser {
    public:
        explicit FastParser(std::string path) : path_to_file(std::move(path)) {}
        void load_and_process(Instance *instance);
        static void process_avg_properties(Instance *instance);
    private:
        const std::string path_to_file;
};

#endif
