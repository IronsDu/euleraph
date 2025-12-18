#include "service/http/euleraph_http_handle.hpp"
#include "interface/types/types.hpp"
#include "interface/storage/reader.hpp"
#include "interface/algo/algo.hpp"
#include <vector>
#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

void EuleraphHttpHandle::ping(const HttpRequestPtr& req, drogon::AdviceCallback&& callback)
{
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    resp->setContentTypeCode(CT_TEXT_PLAIN);
    resp->setBody("pong");
    callback(resp);
}

std::optional<KHopQueryParams> parse_khop_query_params(const Json::Value&               jsonBody,
                                                       std::shared_ptr<ReaderInterface> reader)
{
    KHopQueryParams params;
    // 1. 提取 node_ids
    if (!jsonBody.isMember("node_ids") || !jsonBody["node_ids"].isArray())
    {
        spdlog::error("Missing or invalid 'node_ids' array");
        return std::nullopt;
    }

    for (const auto& item : jsonBody["node_ids"])
    {
        if (!item.isString())
        {
            spdlog::error("All items in 'node_ids' must be strings");
            return std::nullopt;
        }
        auto node_id = reader->get_vertex_id(0, item.asString());
        if (!node_id)
        {
            spdlog::error("Vertex not found:{}", item.asString());
            return std::nullopt;
        }
        params.vertex_id_list.push_back(*node_id);
    }

    // 2. 提取 k
    if (!jsonBody.isMember("k") || !jsonBody["k"].isNumeric())
    {
        spdlog::error("Missing or invalid 'k'");
        return std::nullopt;
    }
    params.k = jsonBody["k"].asInt();

    // 3. 提取可选 n_labels
    if (!jsonBody.isMember("n_labels") || !jsonBody["n_labels"].isArray())
    {
        spdlog::error("Missing or invalid 'n_labels' array");
        return std::nullopt;
    }
    if (!jsonBody["n_labels"].empty())
    {
        for (const auto& item : jsonBody["n_labels"])
        {
            if (!item.isString())
            {
                spdlog::error("All items in 'n_labels' must be strings");
                return std::nullopt;
            }
            auto node_label_type_id = reader->get_label_type_id(item.asString());
            if (!node_label_type_id)
            {
                spdlog::error("Label label not found:{}", item.asString());
                return std::nullopt;
            }
            params.node_label_type_id_list.push_back(*node_label_type_id);
        }
    }

    // 4.提取可选 r_labels
    if (!jsonBody.isMember("r_labels") || !jsonBody["r_labels"].isArray())
    {
        spdlog::error("Missing or invalid 'r_labels' array");
        return std::nullopt;
    }
    if (!jsonBody["r_labels"].empty())
    {
        for (const auto& item : jsonBody["r_labels"])
        {
            if (!item.isString())
            {
                spdlog::error("All items in 'r_labels' must be strings");
                return std::nullopt;
            }
            auto relation_label_type_id = reader->get_relation_type_id(item.asString());
            if (!relation_label_type_id)
            {
                spdlog::error("Relation label not found:{}", item.asString());
                return std::nullopt;
            }
            params.relation_label_type_id_list.push_back(*relation_label_type_id);
        }
    }

    // 5.提取 direction
    if (!jsonBody.isMember("direction") || !jsonBody["direction"].isNumeric())
    {
        spdlog::error("Missing or invalid 'direction'");
        return std::nullopt;
    }
    params.direction = jsonBody["direction"].asInt();
    return params;
}

void EuleraphHttpHandle::k_hop_neighbor_query(const HttpRequestPtr&                         req,
                                              std::function<void(const HttpResponsePtr&)>&& callback,
                                              std::shared_ptr<ReaderInterface>              reader)
{
    // 1. 检查 Content-Type 是否为 JSON
    if (req->contentType() != CT_APPLICATION_JSON)
    {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k400BadRequest);
        resp->setContentTypeCode(CT_TEXT_PLAIN);
        resp->setBody("Content-Type must be application/json");
        callback(resp);
        return;
    }

    // 2. 解析 JSON 请求体
    const auto& jsonBody = req->getJsonObject();
    if (!jsonBody)
    {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k400BadRequest);
        resp->setContentTypeCode(CT_TEXT_PLAIN);
        resp->setBody("Invalid JSON");
        callback(resp);
        return;
    }

    try
    {
        Json::Value jsonResponse;
        // 3.解析json获取算法需要的参数
        auto params = parse_khop_query_params(*jsonBody, reader);
        if (params)
        {
            // 4. 业务逻辑：调用算法接口计算k度邻居总数
            auto algo            = create_algo();
            int  count           = algo->get_k_hop_neighbor_count(*params, reader);
            jsonResponse["code"] = 0;
            jsonResponse["data"] = count;
        }
        else
        {
            jsonResponse["code"] = -1;
        }

        auto resp = HttpResponse::newHttpJsonResponse(jsonResponse);
        resp->setStatusCode(k200OK);
        callback(resp);
    }
    catch (const std::exception& e)
    {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k400BadRequest);
        resp->setContentTypeCode(CT_TEXT_PLAIN);
        resp->setBody(std::string("Error: ") + e.what());
        callback(resp);
    }
}

std::optional<CommonNeighborQueryParams> parse_common_neighbor_query_params(const Json::Value&               jsonBody,
                                                                            std::shared_ptr<ReaderInterface> reader)
{
    CommonNeighborQueryParams params;
    // 1. 提取可选 node_ids
    if (!jsonBody.isMember("node_ids") || !jsonBody["node_ids"].isArray())
    {
        spdlog::error("Missing or invalid 'node_ids' array");
        return std::nullopt;
    }

    for (const auto& item : jsonBody["node_ids"])
    {
        if (!item.isString())
        {
            spdlog::error("All items in 'node_ids' must be strings");
            return std::nullopt;
        }
        auto node_id = reader->get_vertex_id(0, item.asString());
        if (!node_id)
        {
            spdlog::error("Vertex not found:{}", item.asString());
            return std::nullopt;
        }
        params.vertex_id_list.push_back(*node_id);
    }

    // 2.提取可选 r_labels
    if (!jsonBody.isMember("r_labels") || !jsonBody["r_labels"].isArray())
    {
        spdlog::error("Missing or invalid 'r_labels' array");
        return std::nullopt;
    }
    if (!jsonBody["r_labels"].empty())
    {
        for (const auto& item : jsonBody["r_labels"])
        {
            if (!item.isString())
            {
                spdlog::error("All items in 'r_labels' must be strings");
                return std::nullopt;
            }
            auto relation_label_type_id = reader->get_relation_type_id(item.asString());
            if (!relation_label_type_id)
            {
                spdlog::error("Relation label not found:{}", item.asString());
                return std::nullopt;
            }
            params.relation_label_type_id_list.push_back(*relation_label_type_id);
        }
    }
    return params;
}

void EuleraphHttpHandle::common_neighbor_query(const HttpRequestPtr&                         req,
                                               std::function<void(const HttpResponsePtr&)>&& callback,
                                               std::shared_ptr<ReaderInterface>              reader)
{
    // 1. 检查 Content-Type 是否为 JSON
    if (req->contentType() != CT_APPLICATION_JSON)
    {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k400BadRequest);
        resp->setContentTypeCode(CT_TEXT_PLAIN);
        resp->setBody("Content-Type must be application/json");
        callback(resp);
        return;
    }

    // 2. 解析 JSON 请求体
    const auto& jsonBody = req->getJsonObject();
    if (!jsonBody)
    {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k400BadRequest);
        resp->setContentTypeCode(CT_TEXT_PLAIN);
        resp->setBody("Invalid JSON");
        callback(resp);
        return;
    }

    try
    {
        Json::Value jsonResponse;
        // 3.解析json获取算法需要的参数
        auto params = parse_common_neighbor_query_params(*jsonBody, reader);
        if (params)
        {
            // 4. 业务逻辑：调用算法接口计算共同邻居总数
            auto algo            = create_algo();
            int  count           = algo->get_common_neighbor_count(*params, reader);
            jsonResponse["code"] = 0;
            jsonResponse["data"] = count;
        }
        else
        {
            jsonResponse["code"] = -1;
        }

        auto resp = HttpResponse::newHttpJsonResponse(jsonResponse);
        resp->setStatusCode(k200OK);
        callback(resp);
    }
    catch (const std::exception& e)
    {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k400BadRequest);
        resp->setContentTypeCode(CT_TEXT_PLAIN);
        resp->setBody(std::string("Error: ") + e.what());
        callback(resp);
    }
}