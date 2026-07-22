#include <catch2/catch_test_macros.hpp>

#include "acp/json_rpc.hpp"

#include <sstream>
#include <string>

TEST_CASE("JSON-RPC connection reads newline-delimited messages", "[acp][json_rpc]") {
    std::istringstream input{
        "not-json\n"
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\"}\n"};
    std::ostringstream output;
    swe_agent::acp::JsonRpcConnection connection{input, output};

    const auto invalid = connection.read();
    const auto valid = connection.read();
    const auto eof = connection.read();

    REQUIRE(
        invalid.status ==
        swe_agent::acp::JsonRpcReadResult::Status::ParseError);
    REQUIRE(valid.status == swe_agent::acp::JsonRpcReadResult::Status::Message);
    REQUIRE(valid.message["method"] == "initialize");
    REQUIRE(
        eof.status ==
        swe_agent::acp::JsonRpcReadResult::Status::EndOfStream);
}

TEST_CASE("JSON-RPC connection writes one compact object per line", "[acp][json_rpc]") {
    std::istringstream input;
    std::ostringstream output;
    swe_agent::acp::JsonRpcConnection connection{input, output};

    connection.send_result(1, {{"value", "line one\nline two"}});
    connection.send_notification("session/update", {{"sessionId", "s1"}});
    const auto request_id = connection.send_request(
        "session/request_permission",
        {{"sessionId", "s1"}});

    std::istringstream lines{output.str()};
    std::string line;
    REQUIRE(std::getline(lines, line));
    REQUIRE(swe_agent::acp::Json::parse(line)["result"]["value"] ==
            "line one\nline two");
    REQUIRE(std::getline(lines, line));
    REQUIRE(swe_agent::acp::Json::parse(line)["method"] == "session/update");
    REQUIRE(std::getline(lines, line));
    const auto request = swe_agent::acp::Json::parse(line);
    REQUIRE(request["method"] == "session/request_permission");
    REQUIRE(request["id"] == request_id);
    REQUIRE_FALSE(std::getline(lines, line));
}
