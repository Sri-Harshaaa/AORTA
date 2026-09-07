#pragma once

#include "http/HttpRequest.hpp"
#include "http/HttpResponse.hpp"
#include "http/HttpRouter.hpp"
#include "http/StaticFileHandler.hpp"
#include "task/TaskManager.hpp"
#include "server/Metrics.hpp"

#include <memory>

class HttpHandler {

private:
    HttpRouter router;
    StaticFileHandler static_file_handler;
    std::shared_ptr<TaskManager> task_manager;
    const Metrics& metrics;

    void handleGetHello(const HttpRequest& request, HttpResponse& response);
    void handleGetHealth(const HttpRequest& request, HttpResponse& response);
    void handleGetMetrics(const HttpRequest& request, HttpResponse& response);
    void handleGetTasks(const HttpRequest& request, HttpResponse& response);
    void handlePostTask(const HttpRequest& request, HttpResponse& response);
    void handlePutTask(const HttpRequest& request, HttpResponse& response);
    void handleDeleteTask(const HttpRequest& request, HttpResponse& response);
    void handleOptions(const HttpRequest& request, HttpResponse& response);

    void registerRoutes();

    static std::string escapeJson(const std::string& value);
    static std::string urlDecode(const std::string& value);
    static std::string getFormValue(const std::string& body, const std::string& key);

public:
    HttpHandler(
        std::shared_ptr<TaskManager> task_manager,
        const Metrics& metrics
    );

    void handle(const HttpRequest& request, HttpResponse& response);
};