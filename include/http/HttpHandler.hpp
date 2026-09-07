#pragma once

#include "http/HttpRequest.hpp"
#include "http/HttpResponse.hpp"
#include "http/HttpRouter.hpp"
#include "http/StaticFileHandler.hpp"
#include "server/MetricsRegistry.hpp"
#include "task/TaskStore.hpp"

#include <memory>
#include <string>

/*
 * Routing and request handling.
 *
 * After construction this object is immutable: the route table is fixed, the
 * static file handler is stateless, and both the task store and the metrics
 * registry are internally synchronized. That makes it safe for the threaded
 * server to share one instance across every connection thread, and for each
 * reactor in the epoll server to own its own.
 */
class HttpHandler {

private:
    HttpRouter router;
    StaticFileHandler static_file_handler;

    std::shared_ptr<TaskStore> task_store;
    std::shared_ptr<MetricsRegistry> metrics_registry;

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
        std::shared_ptr<TaskStore> task_store,
        std::shared_ptr<MetricsRegistry> metrics_registry,
        const std::string& public_directory
    );

    void handle(const HttpRequest& request, HttpResponse& response);
};
