#pragma once

#include <string>
#include <cstddef>

#include <functional>

#include "interface/storage/writer.hpp"
#include "interface/types/types.hpp"
#include "utils/thread_pool.hpp"

using WriterInterfaceFactory = std::function<std::shared_ptr<WriterInterface>()>;

class Importer
{
public:
    void import_data(const std::string&     file_path,
                     ThreadPool::Ptr        thread_pool,
                     int                    max_worker_concurrency_num,
                     WriterInterfaceFactory wirter_interface_generator);
};
