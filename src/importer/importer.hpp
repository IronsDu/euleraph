#pragma once

#include <string>
#include <cstddef>

#include <optional>
#include <functional>

#include "interface/storage/reader.hpp"
#include "interface/storage/writer.hpp"
#include "interface/types/types.hpp"
#include "utils/thread_pool.hpp"

using WriterInterfaceFactory = std::function<WriterInterface::Ptr()>;
using ReaderInterfaceFactory = std::function<ReaderInterface::Ptr()>;

class Importer
{
public:
    void import_data(const std::string&     file_path,
                     int                    write_edge_thread_pool_concurrency_num,
                     int                    batch_size,
                     WriterInterfaceFactory wirter_interface_generator,
                     ReaderInterfaceFactory reader_interface_factory,
                     std::optional<int64_t> csv_row_num);
};
