#include <stdexcept>
#include <filesystem>
#include <fstream>

#include "helpers.h"

void* read_entire_file(std::string_view path)
{
    size_t file_size = std::filesystem::file_size(path);
    std::ifstream file(path.data(), std::ios::binary);

    if (!file)
        throw std::runtime_error(
            std::string("failed to open file ") + path.data()
        );

    auto* buf = malloc(file_size);
    file.read(reinterpret_cast<char*>(buf), file_size);

    if (!file) {
        free(buf);
        throw std::runtime_error(
            std::string("failed to read entire file ") + path.data()
        );
    }

    return buf;
}
