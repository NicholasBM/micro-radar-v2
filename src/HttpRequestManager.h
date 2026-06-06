#pragma once

#include <HTTPClient.h>

class HttpRequestManager
{
private:
    HTTPClient http;

    String BuildQueryString(std::vector<std::pair<String, String>>& params);

public:
    HttpRequestManager() = default;
    ~HttpRequestManager() = default;

    String Get(String url, std::vector<std::pair<String, String>> params = {}, std::vector<std::pair<String, String>> headers = {});
    String Post(String url, String body = "", std::vector<std::pair<String, String>> headers = {});
};