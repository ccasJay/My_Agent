#include <catch2/catch_test_macros.hpp>
#include "config/dotenv_loader.hpp"
#include "config/agent_loader.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

namespace fs = std::filesystem;
using swe_agent::config::AgentConfig;
using swe_agent::config::EnvMap;
using swe_agent::config::get_required;
using swe_agent::config::load_agent;
using swe_agent::config::load_env;

// RAII temp file: write content at construction, remove on destruction.
class TempFile {
public:
    TempFile(std::string content, const std::string& suffix)
        : path_(fs::temp_directory_path() /
                ("swe_agent_cfg_" + std::to_string(++seq_) + suffix)) {
        std::ofstream out(path_);
        if (!out) {
            throw std::runtime_error{"failed to create temp file: " + path_.string()};
        }
        out << content;
    }

    ~TempFile() {
        std::error_code ec;
        fs::remove(path_, ec);
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    std::string path() const { return path_.string(); }

private:
    fs::path path_;
    static inline int seq_ = 0;
};

}  // namespace

// ---------------------------------------------------------------------------
// load_env / get_required
// ---------------------------------------------------------------------------

TEST_CASE("load_env parses valid KEY=VALUE pairs", "[config][dotenv]") {
    TempFile file(
        "OPENAI_API_KEY=sk-fake-test-key-001\n"
        "OPENAI_MODEL=gpt-test-model\n"
        "OPENAI_BASE_URL=https://example.test/v1\n",
        ".env");

    const EnvMap env = load_env(file.path());

    REQUIRE(env.at("OPENAI_API_KEY") == "sk-fake-test-key-001");
    REQUIRE(env.at("OPENAI_MODEL") == "gpt-test-model");
    REQUIRE(env.at("OPENAI_BASE_URL") == "https://example.test/v1");
    REQUIRE(env.size() == 3);
}

TEST_CASE("load_env ignores comments and blank lines", "[config][dotenv]") {
    TempFile file(
        "# this is a comment\n"
        "\n"
        "   \n"
        "FOO=bar\n"
        "# another comment\n"
        "\n"
        "BAZ=qux\n",
        ".env");

    const EnvMap env = load_env(file.path());

    REQUIRE(env.size() == 2);
    REQUIRE(env.at("FOO") == "bar");
    REQUIRE(env.at("BAZ") == "qux");
}

TEST_CASE("load_env strips matching double and single quotes", "[config][dotenv]") {
    TempFile file(
        "DOUBLE=\"hello world\"\n"
        "SINGLE='single value'\n"
        "MIXED=\"still quoted\"\n"
        "UNQUOTED=plain\n",
        ".env");

    const EnvMap env = load_env(file.path());

    REQUIRE(env.at("DOUBLE") == "hello world");
    REQUIRE(env.at("SINGLE") == "single value");
    REQUIRE(env.at("MIXED") == "still quoted");
    REQUIRE(env.at("UNQUOTED") == "plain");
}

TEST_CASE("load_env trims keys and values", "[config][dotenv]") {
    TempFile file(
        "  KEY_WITH_SPACES  =  value_with_spaces  \n"
        "\tTAB_KEY\t=\t tab_value \t\n",
        ".env");

    const EnvMap env = load_env(file.path());

    REQUIRE(env.at("KEY_WITH_SPACES") == "value_with_spaces");
    REQUIRE(env.at("TAB_KEY") == "tab_value");
}

TEST_CASE("load_env throws when file is missing", "[config][dotenv]") {
    const std::string missing =
        (fs::temp_directory_path() / "swe_agent_definitely_missing_env_file.env")
            .string();
    REQUIRE_THROWS_AS(load_env(missing), std::runtime_error);
}

TEST_CASE("get_required returns present non-empty value", "[config][dotenv]") {
    EnvMap env;
    env["OPENAI_API_KEY"] = "sk-fake-present";
    env["OPENAI_MODEL"] = "fake-model";

    REQUIRE(get_required(env, "OPENAI_API_KEY") == "sk-fake-present");
    REQUIRE(get_required(env, "OPENAI_MODEL") == "fake-model");
}

TEST_CASE("get_required throws when key is missing", "[config][dotenv]") {
    const EnvMap env;  // empty
    REQUIRE_THROWS_AS(get_required(env, "OPENAI_API_KEY"), std::runtime_error);
}

TEST_CASE("get_required throws when value is empty", "[config][dotenv]") {
    EnvMap env;
    env["OPENAI_API_KEY"] = "";
    REQUIRE_THROWS_AS(get_required(env, "OPENAI_API_KEY"), std::runtime_error);
}

// ---------------------------------------------------------------------------
// load_agent
// ---------------------------------------------------------------------------

TEST_CASE("load_agent reads full yaml with system, user, step_limit, model_name",
          "[config][agent]") {
    TempFile file(
        "agent:\n"
        "  system: |\n"
        "    You are a test agent.\n"
        "  user: |\n"
        "    Do the test task.\n"
        "  step_limit: 5\n"
        "model:\n"
        "  model_name: fake-test-model\n",
        ".yaml");

    const AgentConfig cfg = load_agent(file.path());

    REQUIRE(cfg.system_prompt.find("You are a test agent.") != std::string::npos);
    REQUIRE(cfg.user_prompt.find("Do the test task.") != std::string::npos);
    REQUIRE(cfg.step_limit == 5);
    REQUIRE(cfg.model_name.has_value());
    REQUIRE(*cfg.model_name == "fake-test-model");
}

TEST_CASE("load_agent defaults step_limit to 1 when missing", "[config][agent]") {
    TempFile file(
        "agent:\n"
        "  system: sys\n"
        "  user: do something\n",
        ".yaml");

    const AgentConfig cfg = load_agent(file.path());

    REQUIRE(cfg.step_limit == 1);
    REQUIRE(cfg.user_prompt == "do something");
    REQUIRE(cfg.system_prompt == "sys");
}

TEST_CASE("load_agent throws when user is missing", "[config][agent]") {
    TempFile file(
        "agent:\n"
        "  system: only system present\n"
        "  step_limit: 3\n",
        ".yaml");

    REQUIRE_THROWS_AS(load_agent(file.path()), std::runtime_error);
}

TEST_CASE("load_agent allows step_limit of 0", "[config][agent]") {
    TempFile file(
        "agent:\n"
        "  system: s\n"
        "  user: u\n"
        "  step_limit: 0\n",
        ".yaml");

    const AgentConfig cfg = load_agent(file.path());

    REQUIRE(cfg.step_limit == 0);
    REQUIRE(cfg.user_prompt == "u");
}

TEST_CASE("load_agent leaves model_name as nullopt when absent", "[config][agent]") {
    TempFile file(
        "agent:\n"
        "  system: s\n"
        "  user: u\n"
        "  step_limit: 2\n"
        "model:\n"
        "  # model_name intentionally omitted\n",
        ".yaml");

    const AgentConfig cfg = load_agent(file.path());

    REQUIRE_FALSE(cfg.model_name.has_value());
    REQUIRE(cfg.step_limit == 2);
}

TEST_CASE("load_agent throws on missing file or invalid path", "[config][agent]") {
    const std::string missing =
        (fs::temp_directory_path() / "swe_agent_definitely_missing_agent.yaml")
            .string();

    // YAML::BadFile or runtime_error depending on yaml-cpp / wrapper — catch broadly.
    REQUIRE_THROWS(load_agent(missing));
}
