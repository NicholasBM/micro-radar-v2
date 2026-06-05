#include "HttpRequestManager.h"

#include <sstream>

String HttpRequestManager::PrepareQueryParameters(std::vector<std::pair<String, String>>& params)
{
    if (params.empty())
        return "";

    std::stringstream queryStream;
    queryStream << "?";

    bool first = true;
    for (const auto& [key, value] : params)
    {
        if (!first)
            queryStream << "&";

        queryStream << key.c_str() << "=" << value.c_str();
        first = false;
    }

    return queryStream.str().c_str();
}

String HttpRequestManager::Get(String url, std::vector<std::pair<String, String>> params, std::pair<String, String> basicAuthentication) {
    String queryParams = PrepareQueryParameters(params); // create query params string

    http.begin(url + queryParams);

    // use basic authentication if provided
    if (basicAuthentication.first.length() && basicAuthentication.second.length()) {
        http.setAuthorization(basicAuthentication.first.c_str(), basicAuthentication.second.c_str());
    }

    // send request and handle response 
    int responseCode = http.GET();
    String response = "";

    if (responseCode > 0) {
        Serial.print("Response code: ");
        Serial.println(responseCode);
        response = http.getString();
    }
    else {
        Serial.print("Error: ");
        Serial.println(http.errorToString(responseCode));
    }

    http.end();
    return response;
}
