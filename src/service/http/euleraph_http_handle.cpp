#include "service/http/euleraph_http_handle.hpp"

void EuleraphHttpHandle::ping(const HttpRequestPtr& req, drogon::AdviceCallback&& callback)
{
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(k200OK);
    resp->setContentTypeCode(CT_TEXT_PLAIN);
    resp->setBody("pong");
    callback(resp);
}
