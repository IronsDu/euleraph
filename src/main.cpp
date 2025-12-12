#include <iostream>

#include <drogon/drogon.h>

#include "importer/importer.hpp"
#include "service/http/euleraph_http_handle.hpp"

using namespace drogon;

int main(int argc, char** argv)
{
    const std::string log = R"(
             .__                             .__     
  ____  __ __|  |   ________________  ______ |  |__  
_/ __ \|  |  \  | _/ __ \_  __ \__  \ \____ \|  |  \ 
\  ___/|  |  /  |_\  ___/|  | \// __ \|  |_> >   Y  \
 \___  >____/|____/\___  >__|  (____  /   __/|___|  /
     \/                \/           \/|__|        \/ 
)";
    std::cout << log << std::endl;

    if (true)
    {
        try
        {
            Importer importer;
            importer.import_data("data/sample_data.xlsx");
        }
        catch (const std::exception& e)
        {
            std::cerr << "Data import failed: " << e.what() << std::endl;
        }
    }

    app().setLogPath("./").setLogLevel(trantor::Logger::kWarn).addListener("0.0.0.0", 10020).setThreadNum(16);
    app().registerHandler("/ping",
                          [](const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
                              EuleraphHttpHandle::ping(req, std::move(callback));
                          },
                          {Get});

    app().run();

    return 0;
}