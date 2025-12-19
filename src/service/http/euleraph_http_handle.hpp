#pragma once

#include <drogon/drogon.h>
#include "interface/storage/reader.hpp"
#include <memory>
using namespace drogon;

class EuleraphHttpHandle
{
public:
    static void ping(const HttpRequestPtr& req, drogon::AdviceCallback&& callback);
    static void get_one_hop_neighbors(const HttpRequestPtr&                         req,
                                      std::function<void(const HttpResponsePtr&)>&& callback,
                                      std::shared_ptr<ReaderInterface>              reader);

    static void k_hop_neighbor_query(const HttpRequestPtr&                         req,
                                     std::function<void(const HttpResponsePtr&)>&& callback,
                                     std::shared_ptr<ReaderInterface>              reader);
    static void common_neighbor_query(const HttpRequestPtr&                         req,
                                      std::function<void(const HttpResponsePtr&)>&& callback,
                                      std::shared_ptr<ReaderInterface>              reader);
};