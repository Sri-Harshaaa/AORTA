#pragma once

#include "http/HttpRequest.hpp"
#include "http/HttpResponse.hpp"

#include <functional>
#include <string>
#include <vector>

class HttpRouter {

private:
    struct Route {
        std::string method;
        std::string path;
        std::function<void(const HttpRequest&, HttpResponse&)> handler;
    };

    std::vector<Route> routes;

    static bool methodMatches(const std::string& route_method, const std::string& request_method);
    static bool pathMatches(const std::string& route_path, const std::string& request_path);

public:
    void get(const std::string& path, std::function<void(const HttpRequest&, HttpResponse&)> handler);
    void post(const std::string& path, std::function<void(const HttpRequest&, HttpResponse&)> handler);
    void put(const std::string& path, std::function<void(const HttpRequest&, HttpResponse&)> handler);
    void del(const std::string& path, std::function<void(const HttpRequest&, HttpResponse&)> handler);
    void options(const std::string& path, std::function<void(const HttpRequest&, HttpResponse&)> handler);

    bool route(const HttpRequest& request, HttpResponse& response);
};