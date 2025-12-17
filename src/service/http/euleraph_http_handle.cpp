#include "service/http/euleraph_http_handle.hpp"
#include "interface/types/types.hpp"
#include "interface/storage/reader.hpp"
#include "interface/algo/algo.hpp"
#include <vector>
#include <drogon/drogon.h>

void EuleraphHttpHandle::ping(const HttpRequestPtr& req, drogon::AdviceCallback&& callback)
{
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    resp->setContentTypeCode(CT_TEXT_PLAIN);
    resp->setBody("pong");
    callback(resp);
}

KHopQueryParams parse_khop_query_params(const Json::Value& jsonBody)
{
    KHopQueryParams params;
    // 1. 提取 node_ids
    if (!jsonBody.isMember("node_ids") || !jsonBody["node_ids"].isArray())
    {
        throw std::runtime_error("Missing or invalid 'node_ids' array");
    }

    for (const auto& item : jsonBody["node_ids"])
    {
        if (!item.isString())
            throw std::runtime_error("All items in 'node_ids' must be strings");
        auto node_id = get_vertex_id(item.asString());
        if (!node_id)
        {
            throw std::runtime_error("Vertex not found: " + item.asString());
        }
        params.vertex_id_list.push_back(*node_id);
    }

    // 2. 提取 k
    if (!jsonBody.isMember("k") || !jsonBody["k"].isNumeric())
    {
        throw std::runtime_error("Missing or invalid 'k'");
    }
    params.k = jsonBody["k"].asInt();

    // 3. 提取可选 n_labels
    if (!jsonBody.isMember("n_labels") || !jsonBody["n_labels"].isArray())
    {
        throw std::runtime_error("Missing or invalid 'n_labels' array");
    }
    if (!n_labels.is_null())
    {
        for (const auto& item : jsonBody["n_labels"])
        {
            if (!item.isString())
                throw std::runtime_error("All items in 'n_labels' must be strings");
            auto node_label_type_id = ReaderInterface::get_label_type_id(item);
            if (!node_label_type_id)
            {
                throw std::runtime_error("Label label not found: " + item);
            }
            params.node_label_type_id_list.push_back(*node_label_type_id);
        }
    }

    // 4.提取可选 r_labels
    if (!jsonBody.isMember("r_labels") || !jsonBody["r_labels"].isArray())
    {
        throw std::runtime_error("Missing or invalid 'r_labels' array");
    }
    if (!r_labels.is_null())
    {
        for (const auto& item : jsonBody["r_labels"])
        {
            if (!item.isString())
                throw std::runtime_error("All items in 'r_labels' must be strings");
            auto relation_label_type_id = ReaderInterface::get_relation_type_id(item);
            if (!relation_label_type_id)
            {
                throw std::runtime_error("Relation label not found: " + item);
            }
            params.relation_label_type_id_list.push_back(*relation_label_type_id);
        }
    }

    // 5.提取 direction
    if (!jsonBody.isMember("direction") || !jsonBody["direction"].isNumeric())
    {
        throw std::runtime_error("Missing or invalid 'direction'");
    }
    params.direction = jsonBody["direction"].asInt();
    return params;
}

void EuleraphHttpHandle::k_hop_neighbor_query(const HttpRequestPtr&                         req,
                                              std::function<void(const HttpResponsePtr&)>&& callback)
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
        // 3.解析json获取算法需要的参数
        KHopQueryParams params = parse_khop_query_params(*jsonBody)

            // 4. 业务逻辑：调用算法接口计算k度邻居总数
            int count = AlgoInterface::get_k_hop_neighbor_count(const params);

        // 5. 构造 JSON 响应
        Json::Value jsonResponse;
        jsonResponse["code"] = 0;
        jsonResponse["data"] = count;

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

CommonNeighborQueryParams parse_common_neighbor_query_params(const Json::Value& jsonBody)
{
    CommonNeighborQueryParams params;
    // 1. 提取 node_ids
    if (!jsonBody.isMember("node_ids") || !jsonBody["node_ids"].isArray())
    {
        throw std::runtime_error("Missing or invalid 'node_ids' array");
    }

    for (const auto& item : jsonBody["node_ids"])
    {
        if (!item.isString())
            throw std::runtime_error("All items in 'node_ids' must be strings");
        auto node_id = get_vertex_id(item.asString());
        if (!node_id)
        {
            throw std::runtime_error("Vertex not found: " + item.asString());
        }
        params.vertex_id_list.push_back(*node_id);
    }

    // 2.提取可选 r_labels
    if (!jsonBody.isMember("r_labels") || !jsonBody["r_labels"].isArray())
    {
        throw std::runtime_error("Missing or invalid 'r_labels' array");
    }
    if (!r_labels.is_null())
    {
        for (const auto& item : jsonBody["r_labels"])
        {
            if (!item.isString())
                throw std::runtime_error("All items in 'r_labels' must be strings");
            auto relation_label_type_id = ReaderInterface::get_relation_type_id(item);
            if (!relation_label_type_id)
            {
                throw std::runtime_error("Relation label not found: " + item);
            }
            params.relation_label_type_id_list.push_back(*relation_label_type_id);
        }
    }
    return params;
}

void EuleraphHttpHandle::common_neighbor_query(const HttpRequestPtr&                         req,
                                               std::function<void(const HttpResponsePtr&)>&& callback)
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
        // 3.解析json获取算法需要的参数
        CommonNeighborQueryParams params = parse_common_neighbor_query_params(*jsonBody)

            // 4. 业务逻辑：调用算法接口计算k度邻居总数
            int count = AlgoInterface::get_common_neighbor_count(const params);

        // 5. 构造 JSON 响应
        Json::Value jsonResponse;
        jsonResponse["code"] = 0;
        jsonResponse["data"] = count;

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