#include "http/HttpRouter.hpp"

bool HttpRouter::methodMatches(const std::string& route_method, const std::string& request_method) {
    return route_method == request_method;
}


bool HttpRouter::pathMatches(const std::string& route_path, const std::string& request_path) {
    return route_path == request_path;
}


void HttpRouter::get(const std::string& path, std::function<void(const HttpRequest&, HttpResponse&)> handler) {
    routes.push_back({"GET", path, handler});
}


void HttpRouter::post(const std::string& path, std::function<void(const HttpRequest&, HttpResponse&)> handler) {
    routes.push_back({"POST", path, handler});
}


void HttpRouter::put(const std::string& path, std::function<void(const HttpRequest&, HttpResponse&)> handler) {
    routes.push_back({"PUT", path, handler});
}


void HttpRouter::del(const std::string& path, std::function<void(const HttpRequest&, HttpResponse&)> handler) {
    routes.push_back({"DELETE", path, handler});
}


void HttpRouter::options(const std::string& path, std::function<void(const HttpRequest&, HttpResponse&)> handler) {
    routes.push_back({"OPTIONS", path, handler});
}


bool HttpRouter::route(const HttpRequest& request, HttpResponse& response) {
    bool path_found = false;

    for(const auto& route : routes) {
        if(!pathMatches(route.path, request.target)) {
            continue;
        }

        path_found = true;

        if(!methodMatches(route.method, request.method)) {
            continue;
        }

        route.handler(request, response);
        return true;
    }

    if(path_found) {
        response.setStatus(405, "Method Not Allowed");
        response.setBody("Method Not Allowed");
        response.setContentType("text/plain");
        response.setHeader("Allow", "GET, HEAD, POST, PUT, DELETE, OPTIONS");
        response.setConnection("keep-alive");
        response.setContentLength();
        return false;
    }

    response.setStatus(404, "Not Found");
    response.setBody("Not Found");
    response.setContentType("text/plain");
    response.setConnection("keep-alive");
    response.setContentLength();

    return false;
}