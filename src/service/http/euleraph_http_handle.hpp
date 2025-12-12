#pragma once

#include <drogon/drogon.h>
using namespace drogon;

class EuleraphHttpHandle
{
public:
    static void ping(const HttpRequestPtr& req, drogon::AdviceCallback&& callback);
};