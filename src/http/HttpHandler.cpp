#include "http/HttpHandler.hpp"

#include <cctype>
#include <utility>

HttpHandler::HttpHandler(
    std::shared_ptr<TaskStore> task_store,
    std::shared_ptr<MetricsRegistry> metrics_registry,
    const std::string& public_directory
) : static_file_handler(public_directory),
    task_store(std::move(task_store)),
    metrics_registry(std::move(metrics_registry)) {
    registerRoutes();
}

void HttpHandler::registerRoutes() {
    router.get(
        "/hello",
        [this](const HttpRequest& request, HttpResponse& response) {
            handleGetHello(request, response);
        }
    );

    router.get(
        "/health",
        [this](const HttpRequest& request, HttpResponse& response) {
            handleGetHealth(request, response);
        }
    );

    router.get(
        "/metrics",
        [this](const HttpRequest& request, HttpResponse& response) {
            handleGetMetrics(request, response);
        }
    );

    router.get(
        "/tasks",
        [this](const HttpRequest& request, HttpResponse& response) {
            handleGetTasks(request, response);
        }
    );

    router.post(
        "/tasks",
        [this](const HttpRequest& request, HttpResponse& response) {
            handlePostTask(request, response);
        }
    );
}

void HttpHandler::handle(const HttpRequest& request, HttpResponse& response) {
    if(request.target == "/" || request.target.find('.') != std::string::npos) {
        if(static_file_handler.handle(request, response)) {
            return;
        }
    }

    if(request.method == "PUT" && request.target.starts_with("/tasks/")) {
        handlePutTask(request, response);
        return;
    }

    if(request.method == "DELETE" && request.target.starts_with("/tasks/")) {
        handleDeleteTask(request, response);
        return;
    }

    if(request.method == "OPTIONS") {
        handleOptions(request, response);
        return;
    }

    if(request.method == "HEAD") {
        HttpRequest get_request = request;
        get_request.method = "GET";

        router.route(
            get_request,
            response
        );

        response.setSendBody(false);
        return;
    }

    router.route(
        request,
        response
    );
}

void HttpHandler::handleGetHello(const HttpRequest& request, HttpResponse& response) {
    response.setStatus(200, "OK");
    response.setBody("Hello from AORTA");
    response.setContentType("text/plain");
    response.setConnection("keep-alive");
    response.setContentLength();
}

void HttpHandler::handleGetHealth(const HttpRequest& request, HttpResponse& response) {
    response.setStatus(200, "OK");
    response.setBody("OK");
    response.setContentType("text/plain");
    response.setConnection("keep-alive");
    response.setContentLength();
}

void HttpHandler::handleGetMetrics(const HttpRequest& request, HttpResponse& response) {
    response.setStatus(200, "OK");
    response.setBody(
        metrics_registry
            ? metrics_registry->renderPrometheus()
            : std::string()
    );
    response.setContentType("text/plain; version=0.0.4");
    response.setConnection("keep-alive");
    response.setContentLength();
}

void HttpHandler::handleGetTasks(const HttpRequest& request, HttpResponse& response) {
    std::vector<Task> tasks = task_store->getAll();

    std::string body = "[";

    for(std::size_t i = 0; i < tasks.size(); i++) {
        const Task& task = tasks[i];

        body += "{";
        body += "\"id\":" + std::to_string(task.id) + ",";
        body += "\"title\":\"" + escapeJson(task.title) + "\",";
        body += "\"completed\":" + std::string(task.completed ? "true" : "false");
        body += "}";

        if(i + 1 < tasks.size()) {
            body += ",";
        }
    }

    body += "]";

    response.setStatus(200, "OK");
    response.setBody(body);
    response.setContentType("application/json");
    response.setConnection("keep-alive");
    response.setContentLength();
}

void HttpHandler::handlePostTask(const HttpRequest& request, HttpResponse& response) {
    std::string title;

    if(request.getHeader("Content-Type").find("application/json") != std::string::npos) {
        std::size_t key_position = request.body.find("\"title\"");

        if(key_position != std::string::npos) {
            std::size_t colon_position = request.body.find(
                ':',
                key_position
            );

            if(colon_position != std::string::npos) {
                std::size_t quote_start = request.body.find(
                    '"',
                    colon_position + 1
                );

                if(quote_start != std::string::npos) {
                    std::size_t quote_end = request.body.find(
                        '"',
                        quote_start + 1
                    );

                    if(quote_end != std::string::npos) {
                        title = request.body.substr(
                            quote_start + 1,
                            quote_end - quote_start - 1
                        );
                    }
                }
            }
        }
    } else {
        title = getFormValue(
            request.body,
            "title"
        );
    }

    if(title.empty()) {
        response.setStatus(400, "Bad Request");
        response.setBody("Task title is required");
        response.setContentType("text/plain");
        response.setConnection("keep-alive");
        response.setContentLength();
        return;
    }

    Task created_task;

    if(!task_store->create(title, created_task)) {
        response.setStatus(400, "Bad Request");
        response.setBody("Unable to create task");
        response.setContentType("text/plain");
        response.setConnection("keep-alive");
        response.setContentLength();
        return;
    }

    std::string body = "{";
    body += "\"id\":" + std::to_string(created_task.id) + ",";
    body += "\"title\":\"" + escapeJson(created_task.title) + "\",";
    body += "\"completed\":false";
    body += "}";

    response.setStatus(201, "Created");
    response.setBody(body);
    response.setContentType("application/json");
    response.setConnection("keep-alive");
    response.setContentLength();
}

void HttpHandler::handlePutTask(const HttpRequest& request, HttpResponse& response) {
    std::string id_string = request.target.substr(7);

    if(id_string.empty()) {
        response.setStatus(400, "Bad Request");
        response.setBody("Task ID is required");
        response.setContentType("text/plain");
        response.setConnection("keep-alive");
        response.setContentLength();
        return;
    }

    std::size_t id = 0;

    for(char character : id_string) {
        if(!std::isdigit(static_cast<unsigned char>(character))) {
            response.setStatus(400, "Bad Request");
            response.setBody("Invalid task ID");
            response.setContentType("text/plain");
            response.setConnection("keep-alive");
            response.setContentLength();
            return;
        }

        id = id * 10 + static_cast<std::size_t>(character - '0');
    }

    std::string title = getFormValue(
        request.body,
        "title"
    );

    std::string completed_value = getFormValue(
        request.body,
        "completed"
    );

    if(completed_value != "true" && completed_value != "false") {
        response.setStatus(400, "Bad Request");
        response.setBody("completed must be true or false");
        response.setContentType("text/plain");
        response.setConnection("keep-alive");
        response.setContentLength();
        return;
    }

    bool completed = completed_value == "true";

    Task updated_task;

    if(!task_store->update(
        id,
        title,
        completed,
        updated_task
    )) {
        response.setStatus(404, "Not Found");
        response.setBody("Task Not Found");
        response.setContentType("text/plain");
        response.setConnection("keep-alive");
        response.setContentLength();
        return;
    }

    std::string body = "{";
    body += "\"id\":" + std::to_string(updated_task.id) + ",";
    body += "\"title\":\"" + escapeJson(updated_task.title) + "\",";
    body += "\"completed\":" + std::string(updated_task.completed ? "true" : "false");
    body += "}";

    response.setStatus(200, "OK");
    response.setBody(body);
    response.setContentType("application/json");
    response.setConnection("keep-alive");
    response.setContentLength();
}

void HttpHandler::handleDeleteTask(const HttpRequest& request, HttpResponse& response) {
    std::string id_string = request.target.substr(7);

    if(id_string.empty()) {
        response.setStatus(400, "Bad Request");
        response.setBody("Task ID is required");
        response.setContentType("text/plain");
        response.setConnection("keep-alive");
        response.setContentLength();
        return;
    }

    std::size_t id = 0;

    for(char character : id_string) {
        if(!std::isdigit(static_cast<unsigned char>(character))) {
            response.setStatus(400, "Bad Request");
            response.setBody("Invalid task ID");
            response.setContentType("text/plain");
            response.setConnection("keep-alive");
            response.setContentLength();
            return;
        }

        id = id * 10 + static_cast<std::size_t>(character - '0');
    }

    if(!task_store->remove(id)) {
        response.setStatus(404, "Not Found");
        response.setBody("Task Not Found");
        response.setContentType("text/plain");
        response.setConnection("keep-alive");
        response.setContentLength();
        return;
    }

    response.setStatus(204, "No Content");
    response.setBody("");
    response.setConnection("keep-alive");
    response.setContentLength();
}

void HttpHandler::handleOptions(const HttpRequest& request, HttpResponse& response) {
    response.setStatus(204, "No Content");
    response.setBody("");
    response.setContentType("text/plain");
    response.setConnection("keep-alive");
    response.setHeader(
        "Allow",
        "GET, HEAD, POST, PUT, DELETE, OPTIONS"
    );
    response.setContentLength();
}

std::string HttpHandler::escapeJson(const std::string& value) {
    std::string result;

    for(char character : value) {
        if(character == '\\') {
            result += "\\\\";
        } else if(character == '"') {
            result += "\\\"";
        } else if(character == '\n') {
            result += "\\n";
        } else if(character == '\r') {
            result += "\\r";
        } else if(character == '\t') {
            result += "\\t";
        } else {
            result += character;
        }
    }

    return result;
}

std::string HttpHandler::urlDecode(const std::string& value) {
    std::string result;

    for(std::size_t i = 0; i < value.size(); i++) {
        if(value[i] == '+') {
            result += ' ';
            continue;
        }

        if(value[i] == '%' && i + 2 < value.size()) {
            char first = value[i + 1];
            char second = value[i + 2];

            if(std::isxdigit(static_cast<unsigned char>(first)) && std::isxdigit(static_cast<unsigned char>(second))) {
                int high = std::isdigit(static_cast<unsigned char>(first)) ? first - '0' : std::tolower(static_cast<unsigned char>(first)) - 'a' + 10;
                int low = std::isdigit(static_cast<unsigned char>(second)) ? second - '0' : std::tolower(static_cast<unsigned char>(second)) - 'a' + 10;

                result += static_cast<char>(high * 16 + low);
                i += 2;
                continue;
            }
        }

        result += value[i];
    }

    return result;
}

std::string HttpHandler::getFormValue(const std::string& body, const std::string& key) {
    std::size_t start = 0;

    while(start < body.size()) {
        std::size_t end = body.find(
            '&',
            start
        );

        if(end == std::string::npos) {
            end = body.size();
        }

        std::string pair = body.substr(
            start,
            end - start
        );

        std::size_t separator = pair.find('=');

        if(separator != std::string::npos) {
            std::string current_key = pair.substr(
                0,
                separator
            );

            if(current_key == key) {
                return urlDecode(
                    pair.substr(separator + 1)
                );
            }
        }

        start = end + 1;
    }

    return "";
}