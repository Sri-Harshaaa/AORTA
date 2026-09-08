#include "http/HttpHandler.hpp"

#include <cctype>
#include <utility>

namespace {

void setErrorResponse(
    HttpResponse& response,
    int status,
    const std::string& reason,
    const std::string& message
) {
    response.setStatus(status, reason);
    response.setBody(message);
    response.setContentType("text/plain");
    response.setConnection("keep-alive");
    response.setContentLength();
}

}


HttpHandler::HttpHandler(
    TaskManager* task_manager,
    const Metrics& metrics
)
    : static_file_handler("./public"),
      task_manager(task_manager),
      metrics(metrics) {
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
        [](const HttpRequest&, HttpResponse&) {
        }
    );
}


HttpHandler::HandleResult HttpHandler::handle(
    const HttpRequest& request,
    HttpResponse& response,
    ResponseCallback callback
) {
    if(
        request.target == "/"
        ||
        request.target.find('.') != std::string::npos
    ) {
        if(static_file_handler.handle(request, response)) {
            return HandleResult::Completed;
        }
    }

    if(request.method == "GET" && request.target == "/tasks") {
        return handleGetTasks(
            request,
            response,
            std::move(callback)
        );
    }

    if(request.method == "POST" && request.target == "/tasks") {
        return handlePostTask(
            request,
            response,
            std::move(callback)
        );
    }

    if(request.method == "PUT" && request.target.starts_with("/tasks/")) {
        return handlePutTask(
            request,
            response,
            std::move(callback)
        );
    }

    if(request.method == "DELETE" && request.target.starts_with("/tasks/")) {
        return handleDeleteTask(
            request,
            response,
            std::move(callback)
        );
    }

    if(request.method == "OPTIONS") {
        handleOptions(
            request,
            response
        );

        return HandleResult::Completed;
    }

    if(request.method == "HEAD") {
        HttpRequest get_request = request;
        get_request.method = "GET";

        if(
            get_request.target == "/tasks"
            ||
            get_request.target.starts_with("/tasks/")
        ) {
            setErrorResponse(
                response,
                405,
                "Method Not Allowed",
                "Method Not Allowed"
            );

            response.setHeader(
                "Allow",
                "GET, HEAD, POST, PUT, DELETE, OPTIONS"
            );

            response.setSendBody(false);
            return HandleResult::Completed;
        }

        router.route(
            get_request,
            response
        );

        response.setSendBody(false);
        return HandleResult::Completed;
    }

    router.route(
        request,
        response
    );

    return HandleResult::Completed;
}


HttpHandler::HandleResult HttpHandler::handleGetTasks(
    const HttpRequest& request,
    HttpResponse& response,
    ResponseCallback callback
) {
    (void)request;

    if(task_manager == nullptr) {
        setErrorResponse(
            response,
            503,
            "Service Unavailable",
            "Task storage unavailable"
        );
        return HandleResult::Completed;
    }

    const bool accepted =
        task_manager->getAll(
            [callback = std::move(callback)](
                bool success,
                const std::vector<Task>& tasks
            ) mutable {
                HttpResponse response;

                if(!success) {
                    setErrorResponse(
                        response,
                        503,
                        "Service Unavailable",
                        "Task storage unavailable"
                    );

                    callback(std::move(response));
                    return;
                }

                std::string body = "[";

                for(std::size_t i = 0; i < tasks.size(); ++i) {
                    const Task& task = tasks[i];

                    body += "{";
                    body += "\"id\":" + std::to_string(task.id) + ",";
                    body += "\"title\":\"" + escapeJson(task.title) + "\",";
                    body += "\"completed\":";
                    body += task.completed ? "true" : "false";
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

                callback(std::move(response));
            }
        );

    if(!accepted) {
        setErrorResponse(
            response,
            503,
            "Service Unavailable",
            "Task queue unavailable"
        );
        return HandleResult::Completed;
    }

    return HandleResult::Pending;
}


HttpHandler::HandleResult HttpHandler::handlePostTask(
    const HttpRequest& request,
    HttpResponse& response,
    ResponseCallback callback
) {
    if(task_manager == nullptr) {
        setErrorResponse(
            response,
            503,
            "Service Unavailable",
            "Task storage unavailable"
        );
        return HandleResult::Completed;
    }

    std::string title;

    if(
        request.getHeader("Content-Type").find(
            "application/json"
        ) != std::string::npos
    ) {
        std::size_t key_position =
            request.body.find("\"title\"");

        if(key_position != std::string::npos) {
            std::size_t colon_position =
                request.body.find(
                    ':',
                    key_position
                );

            if(colon_position != std::string::npos) {
                std::size_t quote_start =
                    request.body.find(
                        '"',
                        colon_position + 1
                    );

                if(quote_start != std::string::npos) {
                    std::size_t quote_end =
                        request.body.find(
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
        setErrorResponse(
            response,
            400,
            "Bad Request",
            "Task title is required"
        );
        return HandleResult::Completed;
    }

    const bool accepted =
        task_manager->create(
            title,
            [callback = std::move(callback)](
                bool success,
                const Task& task
            ) mutable {
                HttpResponse response;

                if(!success) {
                    setErrorResponse(
                        response,
                        503,
                        "Service Unavailable",
                        "Task storage unavailable"
                    );

                    callback(std::move(response));
                    return;
                }

                std::string body = "{";
                body += "\"id\":" + std::to_string(task.id) + ",";
                body += "\"title\":\"" + escapeJson(task.title) + "\",";
                body += "\"completed\":false";
                body += "}";

                response.setStatus(201, "Created");
                response.setBody(body);
                response.setContentType("application/json");
                response.setConnection("keep-alive");
                response.setContentLength();

                callback(std::move(response));
            }
        );

    if(!accepted) {
        setErrorResponse(
            response,
            503,
            "Service Unavailable",
            "Task queue unavailable"
        );
        return HandleResult::Completed;
    }

    return HandleResult::Pending;
}


HttpHandler::HandleResult HttpHandler::handlePutTask(
    const HttpRequest& request,
    HttpResponse& response,
    ResponseCallback callback
) {
    if(task_manager == nullptr) {
        setErrorResponse(
            response,
            503,
            "Service Unavailable",
            "Task storage unavailable"
        );
        return HandleResult::Completed;
    }

    std::size_t id{0};

    if(!parseTaskId(request.target, id)) {
        setErrorResponse(
            response,
            400,
            "Bad Request",
            "Invalid task ID"
        );
        return HandleResult::Completed;
    }

    std::string title =
        getFormValue(
            request.body,
            "title"
        );

    std::string completed_value =
        getFormValue(
            request.body,
            "completed"
        );

    if(
        completed_value != "true"
        &&
        completed_value != "false"
    ) {
        setErrorResponse(
            response,
            400,
            "Bad Request",
            "completed must be true or false"
        );
        return HandleResult::Completed;
    }

    const bool completed =
        completed_value == "true";

    const bool accepted =
        task_manager->update(
            id,
            title,
            completed,
            [callback = std::move(callback)](
                bool success,
                const Task& task
            ) mutable {
                HttpResponse response;

                if(!success) {
                    setErrorResponse(
                        response,
                        404,
                        "Not Found",
                        "Task Not Found"
                    );

                    callback(std::move(response));
                    return;
                }

                std::string body = "{";
                body += "\"id\":" + std::to_string(task.id) + ",";
                body += "\"title\":\"" + escapeJson(task.title) + "\",";
                body += "\"completed\":";
                body += task.completed ? "true" : "false";
                body += "}";

                response.setStatus(200, "OK");
                response.setBody(body);
                response.setContentType("application/json");
                response.setConnection("keep-alive");
                response.setContentLength();

                callback(std::move(response));
            }
        );

    if(!accepted) {
        setErrorResponse(
            response,
            503,
            "Service Unavailable",
            "Task queue unavailable"
        );
        return HandleResult::Completed;
    }

    return HandleResult::Pending;
}


HttpHandler::HandleResult HttpHandler::handleDeleteTask(
    const HttpRequest& request,
    HttpResponse& response,
    ResponseCallback callback
) {
    if(task_manager == nullptr) {
        setErrorResponse(
            response,
            503,
            "Service Unavailable",
            "Task storage unavailable"
        );
        return HandleResult::Completed;
    }

    std::size_t id{0};

    if(!parseTaskId(request.target, id)) {
        setErrorResponse(
            response,
            400,
            "Bad Request",
            "Invalid task ID"
        );
        return HandleResult::Completed;
    }

    const bool accepted =
        task_manager->remove(
            id,
            [callback = std::move(callback)](
                bool success,
                bool removed
            ) mutable {
                HttpResponse response;

                if(!success || !removed) {
                    setErrorResponse(
                        response,
                        404,
                        "Not Found",
                        "Task Not Found"
                    );

                    callback(std::move(response));
                    return;
                }

                response.setStatus(204, "No Content");
                response.setBody("");
                response.setConnection("keep-alive");
                response.setContentLength();

                callback(std::move(response));
            }
        );

    if(!accepted) {
        setErrorResponse(
            response,
            503,
            "Service Unavailable",
            "Task queue unavailable"
        );
        return HandleResult::Completed;
    }

    return HandleResult::Pending;
}


void HttpHandler::handleGetHello(
    const HttpRequest& request,
    HttpResponse& response
) {
    (void)request;

    response.setStatus(200, "OK");
    response.setBody("Hello from AORTA");
    response.setContentType("text/plain");
    response.setConnection("keep-alive");
    response.setContentLength();
}


void HttpHandler::handleGetHealth(
    const HttpRequest& request,
    HttpResponse& response
) {
    (void)request;

    response.setStatus(200, "OK");
    response.setBody("OK");
    response.setContentType("text/plain");
    response.setConnection("keep-alive");
    response.setContentLength();
}


void HttpHandler::handleGetMetrics(
    const HttpRequest& request,
    HttpResponse& response
) {
    (void)request;

    response.setStatus(200, "OK");
    response.setBody(metrics.serialize());
    response.setContentType("text/plain; version=0.0.4");
    response.setConnection("keep-alive");
    response.setContentLength();
}


void HttpHandler::handleOptions(
    const HttpRequest& request,
    HttpResponse& response
) {
    (void)request;

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


bool HttpHandler::parseTaskId(
    const std::string& target,
    std::size_t& id
) {
    if(!target.starts_with("/tasks/")) {
        return false;
    }

    const std::string id_string =
        target.substr(7);

    if(id_string.empty()) {
        return false;
    }

    id = 0;

    for(char character : id_string) {
        if(!std::isdigit(static_cast<unsigned char>(character))) {
            return false;
        }

        const std::size_t digit =
            static_cast<std::size_t>(character - '0');

        if(
            id >
            (static_cast<std::size_t>(-1) - digit) / 10
        ) {
            return false;
        }

        id = id * 10 + digit;
    }

    return true;
}


std::string HttpHandler::escapeJson(
    const std::string& value
) {
    std::string result;
    result.reserve(value.size());

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


std::string HttpHandler::urlDecode(
    const std::string& value
) {
    std::string result;

    for(std::size_t i = 0; i < value.size(); ++i) {
        if(value[i] == '+') {
            result += ' ';
            continue;
        }

        if(value[i] == '%' && i + 2 < value.size()) {
            const char first = value[i + 1];
            const char second = value[i + 2];

            if(
                std::isxdigit(static_cast<unsigned char>(first))
                &&
                std::isxdigit(static_cast<unsigned char>(second))
            ) {
                const int high =
                    std::isdigit(static_cast<unsigned char>(first))
                        ? first - '0'
                        : std::tolower(static_cast<unsigned char>(first)) - 'a' + 10;

                const int low =
                    std::isdigit(static_cast<unsigned char>(second))
                        ? second - '0'
                        : std::tolower(static_cast<unsigned char>(second)) - 'a' + 10;

                result +=
                    static_cast<char>(high * 16 + low);

                i += 2;
                continue;
            }
        }

        result += value[i];
    }

    return result;
}


std::string HttpHandler::getFormValue(
    const std::string& body,
    const std::string& key
) {
    std::size_t start = 0;

    while(start < body.size()) {
        std::size_t end =
            body.find('&', start);

        if(end == std::string::npos) {
            end = body.size();
        }

        std::string pair =
            body.substr(
                start,
                end - start
            );

        const std::size_t separator =
            pair.find('=');

        if(separator != std::string::npos) {
            const std::string current_key =
                pair.substr(
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
