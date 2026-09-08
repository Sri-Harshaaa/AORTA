#pragma once

#include "http/HttpRequest.hpp"
#include "http/HttpResponse.hpp"
#include "http/HttpRouter.hpp"
#include "http/StaticFileHandler.hpp"
#include "task/TaskManager.hpp"
#include "server/Metrics.hpp"

#include <functional>

class HttpHandler {

public:
    enum class HandleResult {
        Completed,
        Pending
    };

    using ResponseCallback =
        std::function<void(HttpResponse)>;

private:
    HttpRouter router;
    StaticFileHandler static_file_handler;
    TaskManager* task_manager{nullptr};
    const Metrics& metrics;

    HandleResult handleGetTasks(
        const HttpRequest& request,
        HttpResponse& response,
        ResponseCallback callback
    );

    HandleResult handlePostTask(
        const HttpRequest& request,
        HttpResponse& response,
        ResponseCallback callback
    );

    HandleResult handlePutTask(
        const HttpRequest& request,
        HttpResponse& response,
        ResponseCallback callback
    );

    HandleResult handleDeleteTask(
        const HttpRequest& request,
        HttpResponse& response,
        ResponseCallback callback
    );

    void handleGetHello(
        const HttpRequest& request,
        HttpResponse& response
    );

    void handleGetHealth(
        const HttpRequest& request,
        HttpResponse& response
    );

    void handleGetMetrics(
        const HttpRequest& request,
        HttpResponse& response
    );

    void handleOptions(
        const HttpRequest& request,
        HttpResponse& response
    );

    void registerRoutes();

    static std::string escapeJson(
        const std::string& value
    );

    static std::string urlDecode(
        const std::string& value
    );

    static std::string getFormValue(
        const std::string& body,
        const std::string& key
    );

    static bool parseTaskId(
        const std::string& target,
        std::size_t& id
    );

public:
    HttpHandler(
        TaskManager* task_manager,
        const Metrics& metrics
    );

    HandleResult handle(
        const HttpRequest& request,
        HttpResponse& response,
        ResponseCallback callback
    );
};
