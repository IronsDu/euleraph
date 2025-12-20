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

// request json:
// "start_vertex_pk": 起点的标识, 必选项.
// "direction": 方向(0:出, 1:出, 2:双向, 非必填. 未指定时为默认值:2.
// "relation": 关系类型的名称, 非必填, 此时表示不过滤关系类型.
void EuleraphHttpHandle::get_one_hop_neighbors(const HttpRequestPtr&                         req,
                                               std::function<void(const HttpResponsePtr&)>&& callback,
                                               std::shared_ptr<ReaderInterface>              reader)
{
    const auto reply_error_msg = [&](std::string error_msg, int code = 500) {
        spdlog::error(error_msg);

        Json::Value json;
        json["code"]    = code;
        json["message"] = error_msg;
        callback(HttpResponse::newHttpJsonResponse(json));
    };

    const auto& jsonBody              = req->getJsonObject();
    const auto  start_vertex_pk_param = (*jsonBody)["start_vertex_pk"];
    if (!start_vertex_pk_param)
    {
        reply_error_msg("param:start_vertex_pk is missing", 400);
        return;
    }

    std::string   start_vertex_pk = start_vertex_pk_param.asString();
    EdgeDirection direction       = EdgeDirection::UNDIRECTED;
    const auto    direction_param = (*jsonBody)["direction"];
    if (direction_param)
    {
        const auto direction_value = direction_param.asInt();
        if (direction_value != static_cast<int>(EdgeDirection::OUTGOING) &&
            direction_value != static_cast<int>(EdgeDirection::INCOMING) &&
            direction_value != static_cast<int>(EdgeDirection::UNDIRECTED))
        {
            reply_error_msg("direction is incorrect", 400);
            return;
        }
        direction = static_cast<EdgeDirection>(direction_value);
    }

    std::optional<RelationTypeId> relation_type_id;
    const auto                    relation_param = (*jsonBody)["relation"];
    if (relation_param)
    {
        const std::string relation_name = relation_param.asString();
        if (!relation_name.empty())
        {
            relation_type_id = reader->get_relation_type_id(relation_name);
            if (!relation_type_id)
            {
                reply_error_msg(fmt::format("not found relation:{}", relation_name));
                return;
            }
        }
    }

    // TODO::label type id无需指定
    const auto dummy_start_label_id = 1;
    auto       start_vertex_id      = reader->get_vertex_id(dummy_start_label_id, start_vertex_pk);
    if (!start_vertex_id)
    {
        reply_error_msg(fmt::format("not found vertex:{}", start_vertex_pk));
        return;
    }

    Json::Value data_json;

    // 实际需要查询的方向数组
    const std::vector<EdgeDirection> real_direction_array = [&]() -> std::vector<EdgeDirection> {
        switch (direction)
        {
        case EdgeDirection::OUTGOING:
            return {EdgeDirection::OUTGOING};
        case EdgeDirection::INCOMING:
            return {EdgeDirection::INCOMING};
        case EdgeDirection::UNDIRECTED:
            return {EdgeDirection::OUTGOING, EdgeDirection::INCOMING};
        default:
            return {};
        }
    }();

    for (const auto& direction : real_direction_array)
    {
        const auto neighbors_edges = reader->get_neighbors_by_start_vertex(start_vertex_id.value(),
                                                                           dummy_start_label_id,
                                                                           static_cast<EdgeDirection>(direction),
                                                                           relation_type_id);

        for (const auto& edge : neighbors_edges)
        {
            auto start_label_type = reader->get_label_type_by_id(edge.start_label_type_id);
            auto end_label_type   = reader->get_label_type_by_id(edge.end_label_type_id);

            auto relation_type = reader->get_relation_type_by_id(edge.relation_type_id);

            auto start_vertex_pk = reader->get_vertex_pk_by_id(edge.start_vertex_id);
            auto end_vertex_pk   = reader->get_vertex_pk_by_id(edge.end_vertex_id);

            if (edge.direction == EdgeDirection::OUTGOING)
            {
                data_json.append(fmt::format("{}:{} -> [{}] -> {}:{}",
                                             start_vertex_pk.value(),
                                             start_label_type.value(),
                                             relation_type.value(),
                                             end_vertex_pk.value(),
                                             end_label_type.value()));
            }
            else
            {
                data_json.append(fmt::format("{}:{} -> [{}] -> {}:{}",
                                             end_vertex_pk.value(),
                                             end_label_type.value(),
                                             relation_type.value(),
                                             start_vertex_pk.value(),
                                             start_label_type.value()));
            }
        }
    }

    Json::Value resp_json;
    resp_json["data"] = data_json;
    callback(HttpResponse::newHttpJsonResponse(resp_json));
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
    if (jsonBody.isMember("n_labels") && !jsonBody["n_labels"].empty())
    {
        if (!jsonBody["n_labels"].isArray())
        {
            spdlog::error("Missing or invalid 'n_labels' array");
            return std::nullopt;
        }
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
    if (jsonBody.isMember("r_labels") && !jsonBody["r_labels"].empty())
    {
        if (!jsonBody["r_labels"].isArray())
        {
            spdlog::error("Missing or invalid 'r_labels' array");
            return std::nullopt;
        }
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
                                              std::shared_ptr<ReaderInterface>              reader,
                                              WT_CONNECTION*                                conn)
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
            auto algo             = create_algo();
            auto count            = algo->get_k_hop_neighbor_count(*params, reader, conn);
            jsonResponse["code"]  = 0;
            jsonResponse["count"] = count;
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

    // 2.提取可选 r_labels
    if (jsonBody.isMember("r_labels") && !jsonBody["r_labels"].empty())
    {
        if (!jsonBody["r_labels"].isArray())
        {
            spdlog::error("Missing or invalid 'r_labels' array");
            return std::nullopt;
        }
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
            auto algo             = create_algo();
            int  count            = algo->get_common_neighbor_count(*params, reader);
            jsonResponse["code"]  = 0;
            jsonResponse["count"] = count;
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

std::optional<WCCParams> parse_wcc_params(const Json::Value& jsonBody, std::shared_ptr<ReaderInterface> reader)
{
    WCCParams params;
    // 1. 提取 n_labels
    if (jsonBody.isMember("n_labels") && !jsonBody["n_labels"].empty())
    {
        if (!jsonBody["n_labels"].isArray())
        {
            spdlog::error("Missing or invalid 'n_labels' array");
            return std::nullopt;
        }
        for (const auto& item : jsonBody["n_labels"])
        {
            if (!item.isString())
            {
                spdlog::error("All items in 'n_labels' must be strings");
                return std::nullopt;
            }
            auto label_type_id = reader->get_label_type_id(item.asString());
            if (!label_type_id)
            {
                spdlog::error("Relation label not found:{}", item.asString());
                return std::nullopt;
            }
            params.label_type_id_list.push_back(*label_type_id);
        }
    }

    // 2.提取可选 r_labels
    if (jsonBody.isMember("r_labels") && !jsonBody["r_labels"].empty())
    {
        if (!jsonBody["r_labels"].isArray())
        {
            spdlog::error("Missing or invalid 'r_labels' array");
            return std::nullopt;
        }
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

void EuleraphHttpHandle::wcc_query(const HttpRequestPtr&                         req,
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
        auto params = parse_wcc_params(*jsonBody, reader);
        if (params)
        {
            // 4. 业务逻辑：调用算法接口计算共同邻居总数
            auto algo                       = create_algo();
            int  count                      = algo->get_wcc_count(*params, reader);
            jsonResponse["code"]            = 0;
            jsonResponse["component_count"] = count;
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

EdgeDirection get_direction(std::string direction)
{
    if (direction == "IN") {
        return EdgeDirection::INCOMING;
    } else if (direction == "OUT") {
        return EdgeDirection::OUTGOING;
    } else {
        spdlog::info("direction must be 'IN' or 'OUT'");
        return EdgeDirection::UNDIRECTED;
    }
}
std::optional<SubgraphMatchingParams> parse_subgraph_matching_params(const Json::Value&               jsonBody,
                                                                     std::shared_ptr<ReaderInterface> reader)
{
    SubgraphMatchingParams params;
    // 1. 提取 nodes中的id
    if (!jsonBody.isMember("nodes") || !jsonBody["nodes"].isArray())
    {
        spdlog::error("Missing or invalid 'nodes' array");
        return std::nullopt;
    }

    for (const auto& node : jsonBody["nodes"])
    {
        std::vector<LabelTypeId> labels_type_id_list;
        for (const auto& label_type : node["labels"])
        {
            auto label_type_id = reader->get_label_type_id(label_type.asString());
            if (!label_type_id)
            {
                spdlog::error("label type not found:{}", label_type.asString());
                return std::nullopt;
            }
            labels_type_id_list.push_back(*label_type_id);
        }
        params.nodes_pattern_map[node["var"].asString()] = labels_type_id_list;
    }

    // 2.提取 edges_pattern
    if (!jsonBody.isMember("edges") || !jsonBody["edges"].isArray())
    {
        spdlog::error("Missing or invalid 'edges' array");
        return std::nullopt;
    }
    for (const auto& edge : jsonBody["edges"])
    {
        PatternEdge edge_pattern;
        for (const auto& relation_label_type : edge["labels"])
        {
            auto relation_label_type_id = reader->get_relation_type_id(relation_label_type.asString());
            if (!relation_label_type_id)
            {
                spdlog::error("Relation label not found:{}", relation_label_type.asString());
                return std::nullopt;
            }
            edge_pattern.relation_type_id_list.push_back(*relation_label_type_id);
        }
        edge_pattern.source_node = edge["source"].asString();
        edge_pattern.target_node = edge["target"].asString();
        edge_pattern.direction = get_direction(edge["direction"].asString());
        params.edges_pattern_list.push_back(edge_pattern);
    }
    return params;
}

void EuleraphHttpHandle::subgraph_matching_query(const HttpRequestPtr&                         req,
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
        auto params = parse_subgraph_matching_params(*jsonBody, reader);
        if (params)
        {
            // 4. 业务逻辑：调用算法接口计算子图匹配(子图同态)总数
            auto algo             = create_algo();
            int  count            = algo->get_subgraph_matching_count(*params, reader);
            jsonResponse["code"]  = 0;
            jsonResponse["count"] = count;
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