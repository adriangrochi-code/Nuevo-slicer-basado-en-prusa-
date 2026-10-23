///|/ Tisma Slicer: client of Tisma Cloud.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "TismaCloud.hpp"

#include <chrono>
#include <thread>

#include <boost/filesystem/path.hpp>
#include <boost/nowide/fstream.hpp>
#include <nlohmann/json.hpp>

#include "Http.hpp"

namespace Slic3r {

namespace {

// The largest result downloaded (the server keeps them up to 1 GB, TISMA_MAX_RESULT_MB).
constexpr size_t MAX_RESULT_BYTES = size_t(1) << 30;

// The "detail" of an error of the API (FastAPI), or the HTTP error.
std::string api_error(const std::string &body, const std::string &error, unsigned status)
{
    try {
        const nlohmann::json j = nlohmann::json::parse(body);
        if (j.contains("detail")) {
            const nlohmann::json &d = j["detail"];
            if (d.is_string())
                return d.get<std::string>();
            // Validation errors: a list of {msg, ...}.
            if (d.is_array() && ! d.empty() && d[0].contains("msg"))
                return d[0]["msg"].get<std::string>();
        }
    } catch (const std::exception &) {
    }
    if (! error.empty())
        return error;
    return "HTTP " + std::to_string(status);
}

std::string json_text(const nlohmann::json &v)
{
    return v.is_string() ? v.get<std::string>() : v.dump();
}

void flatten(const nlohmann::json &j, const std::string &prefix, std::map<std::string, std::string> &out)
{
    for (auto it = j.begin(); it != j.end(); ++ it) {
        if (it->is_object())
            flatten(*it, prefix + it.key() + ".", out);
        else if (! it->is_null())
            out[prefix + it.key()] = json_text(*it);
    }
}

void parse_job(const nlohmann::json &j, TismaCloud::Job &job)
{
    job.id              = j.value("id", std::string());
    job.status          = j.value("status", std::string());
    job.progress        = j.value("progress", 0);
    job.error           = j.value("error", std::string());
    job.result_filename = j.value("result_filename", std::string());
    job.seconds         = j.value("seconds", 0.);
    job.summary.clear();
    if (j.contains("summary") && j["summary"].is_object())
        flatten(j["summary"], std::string(), job.summary);
}

} // namespace

TismaCloud::TismaCloud(std::string url, std::string token) : m_url(std::move(url)), m_token(std::move(token))
{
    while (! m_url.empty() && m_url.back() == '/')
        m_url.pop_back();
}

Http& TismaCloud::authorize(Http &http) const
{
    return http.header("Authorization", "Bearer " + m_token).timeout_connect(10);
}

bool TismaCloud::health(std::string &billing_url, std::string &error)
{
    bool ok = false;
    Http::get(m_url + "/v1/health")
        .timeout_connect(10)
        .timeout_max(20)
        .on_complete([&](std::string body, unsigned) {
            try {
                const nlohmann::json j = nlohmann::json::parse(body);
                billing_url = j.value("billing_url", std::string());
                ok = j.value("ok", false);
            } catch (const std::exception &) {
            }
            if (! ok)
                error = "Unexpected answer of the server";
        })
        .on_error([&](std::string body, std::string err, unsigned status) { error = api_error(body, err, status); })
        .perform_sync();
    return ok;
}

bool TismaCloud::login(const std::string &email, const std::string &password, const std::string &device,
                       std::string &token, std::string &error)
{
    bool ok = false;
    const nlohmann::json body = { { "email", email }, { "password", password }, { "device", device } };
    Http::post(m_url + "/v1/auth/login")
        .timeout_connect(10)
        .timeout_max(60)
        .header("Content-Type", "application/json")
        .set_post_body(body.dump())
        .on_complete([&](std::string body, unsigned) {
            try {
                token = nlohmann::json::parse(body).at("token").get<std::string>();
                ok = true;
            } catch (const std::exception &) {
                error = "Unexpected answer of the server";
            }
        })
        .on_error([&](std::string body, std::string err, unsigned status) { error = api_error(body, err, status); })
        .perform_sync();
    if (ok)
        m_token = token;
    return ok;
}

bool TismaCloud::logout(std::string &error)
{
    bool ok = false;
    auto http = Http::post(m_url + "/v1/auth/logout");
    authorize(http)
        .timeout_max(30)
        .on_complete([&](std::string, unsigned) { ok = true; })
        .on_error([&](std::string body, std::string err, unsigned status) { error = api_error(body, err, status); })
        .perform_sync();
    return ok;
}

bool TismaCloud::account(Account &account, std::string &error)
{
    bool ok = false;
    auto http = Http::get(m_url + "/v1/me");
    authorize(http)
        .timeout_max(30)
        .on_complete([&](std::string body, unsigned) {
            try {
                const nlohmann::json j = nlohmann::json::parse(body);
                account.email          = j.value("email", std::string());
                account.plan           = j.value("plan", std::string());
                account.paid_until     = j["paid_until"].is_string() ? j["paid_until"].get<std::string>() : std::string();
                account.paid           = j.value("paid", false);
                account.hours_included = j.value("hours_included", 0.);
                account.hours_used     = j.value("hours_used", 0.);
                account.hours_left     = j.value("hours_left", 0.);
                account.active_jobs    = j.value("active_jobs", 0);
                account.max_concurrent = j.value("max_concurrent", 0);
                ok = true;
            } catch (const std::exception &) {
                error = "Unexpected answer of the server";
            }
        })
        .on_error([&](std::string body, std::string err, unsigned status) { error = api_error(body, err, status); })
        .perform_sync();
    return ok;
}

bool TismaCloud::report(const std::map<std::string, std::string> &fields, int &similar, std::string &error)
{
    bool ok = false;
    nlohmann::json body = nlohmann::json::object();
    for (const auto &[key, value] : fields)
        body[key] = value;
    auto http = Http::post(m_url + "/v1/reports");
    if (! m_token.empty())
        authorize(http);
    http.timeout_connect(10)
        .timeout_max(60)
        .header("Content-Type", "application/json")
        .set_post_body(body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace))
        .on_complete([&](std::string body, unsigned) {
            try {
                similar = nlohmann::json::parse(body).value("similar", 0);
                ok = true;
            } catch (const std::exception &) {
                error = "Unexpected answer of the server";
            }
        })
        .on_error([&](std::string body, std::string err, unsigned status) { error = api_error(body, err, status); })
        .perform_sync();
    return ok;
}

bool TismaCloud::submit(const std::string &kind, const std::string &params_json, const std::string &project_path, Job &job,
                        std::string &error)
{
    bool ok = false;
    auto http = Http::post(m_url + "/v1/jobs");
    authorize(http)
        .timeout_max(1800)
        .form_add("kind", kind)
        .form_add("params", params_json)
        .form_add_file("file", boost::filesystem::path(project_path), "project.3mf")
        .on_complete([&](std::string body, unsigned) {
            try {
                parse_job(nlohmann::json::parse(body), job);
                ok = ! job.id.empty();
            } catch (const std::exception &) {
            }
            if (! ok)
                error = "Unexpected answer of the server";
        })
        .on_error([&](std::string body, std::string err, unsigned status) { error = api_error(body, err, status); })
        .perform_sync();
    return ok;
}

bool TismaCloud::status(const std::string &job_id, Job &job, std::string &error)
{
    bool ok = false;
    auto http = Http::get(m_url + "/v1/jobs/" + job_id);
    authorize(http)
        .timeout_max(30)
        .on_complete([&](std::string body, unsigned) {
            try {
                parse_job(nlohmann::json::parse(body), job);
                ok = true;
            } catch (const std::exception &) {
                error = "Unexpected answer of the server";
            }
        })
        .on_error([&](std::string body, std::string err, unsigned status) { error = api_error(body, err, status); })
        .perform_sync();
    return ok;
}

bool TismaCloud::cancel(const std::string &job_id, std::string &error)
{
    bool ok = false;
    auto http = Http::post(m_url + "/v1/jobs/" + job_id + "/cancel");
    authorize(http)
        .timeout_max(30)
        .on_complete([&](std::string, unsigned) { ok = true; })
        .on_error([&](std::string body, std::string err, unsigned status) { error = api_error(body, err, status); })
        .perform_sync();
    return ok;
}

bool TismaCloud::download(const std::string &job_id, const std::string &path, std::string &error)
{
    bool ok = false;
    auto http = Http::get(m_url + "/v1/jobs/" + job_id + "/result");
    authorize(http)
        .timeout_max(1800)
        .size_limit(MAX_RESULT_BYTES)
        // G-code travels compressed (several times smaller) when the server compresses it.
        .accept_compressed()
        .on_complete([&](std::string body, unsigned) {
            boost::nowide::ofstream out(path, std::ios::binary | std::ios::trunc);
            out.write(body.data(), std::streamsize(body.size()));
            ok = bool(out.flush());
            if (! ok)
                error = "Cannot write " + path;
        })
        .on_error([&](std::string body, std::string err, unsigned status) { error = api_error(body, err, status); })
        .perform_sync();
    return ok;
}

bool TismaCloud::run(const std::string &kind, const std::string &params_json, const std::string &project_path,
                     const std::string &result_path, Job &job, std::string &error, std::function<void(const Job&)> update,
                     const std::atomic<bool> &cancel)
{
    if (! submit(kind, params_json, project_path, job, error))
        return false;
    if (update)
        update(job);
    // Wait: the status every second at first, then every 3 s (the server limits the requests per minute).
    int polls = 0;
    int failures = 0;
    while (! job.finished()) {
        for (int i = 0; i < (polls < 30 ? 10 : 30) && ! cancel; ++ i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (cancel) {
            std::string ignored;
            this->cancel(job.id, ignored);
            error = "Cancelled";
            return false;
        }
        ++ polls;
        std::string err;
        if (! status(job.id, job, err)) {
            // A few lost answers (network) are tolerated.
            if (++ failures > 10) {
                error = err;
                return false;
            }
            continue;
        }
        failures = 0;
        if (update)
            update(job);
    }
    if (job.status != "done") {
        error = job.status == "cancelled" ? std::string("Cancelled") : job.error;
        return false;
    }
    return download(job.id, result_path, error);
}

} // namespace Slic3r
