#pragma once

#include <drogon/drogon.h>
using namespace drogon;

class EuleraphHttpHandle
{
public:
    static void ping(const HttpRequestPtr& req, drogon::AdviceCallback&& callback);
    void        k_hop_neighbor_query(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
    void common_neighbor_query(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback);
};