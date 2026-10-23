///|/ Tisma Slicer: client of Tisma Cloud, the computing service (slicing and structural analysis on the servers, see
///|/ docs/CLOUD_ARCHITECTURE.md). The calls block: run them outside the GUI thread.
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_TismaCloud_hpp_
#define slic3r_TismaCloud_hpp_

#include <atomic>
#include <functional>
#include <map>
#include <string>

namespace Slic3r {

class Http;

class TismaCloud
{
public:
    // The account state (GET /v1/me).
    struct Account
    {
        std::string email;
        std::string plan;
        std::string paid_until;   // ISO 8601, empty: never paid
        bool        paid { false };
        double      hours_included { 0. };
        double      hours_used { 0. };
        double      hours_left { 0. };
        int         active_jobs { 0 };
        int         max_concurrent { 0 };
    };

    struct Job
    {
        std::string id;
        std::string status;       // queued, running, done, failed, cancelled
        int         progress { 0 };
        std::string error;
        std::string result_filename;
        double      seconds { 0. };
        // The summary of the node, flattened: "print_time", "result.verdict", ...
        std::map<std::string, std::string> summary;

        bool finished() const { return status == "done" || status == "failed" || status == "cancelled"; }
    };

    TismaCloud(std::string url, std::string token);

    const std::string& url() const { return m_url; }

    // GET /v1/health: the server answers; billing_url is the page of the web site to create an account and pay the plan
    // (empty when the server has none).
    bool health(std::string &billing_url, std::string &error);
    // POST /v1/auth/login: the token of this device.
    bool login(const std::string &email, const std::string &password, const std::string &device, std::string &token,
               std::string &error);
    bool logout(std::string &error);
    bool account(Account &account, std::string &error);

    // POST /v1/reports: a problem found by the user (fields of the server's ReportBody: app, kind, title, description,
    // steps, expected, actual, app_version, os, log), with the token of the account when there is one. similar: how
    // many reports of the same problem the server has besides this one.
    bool report(const std::map<std::string, std::string> &fields, int &similar, std::string &error);

    // Sends a job (kind "slice" or "fea", params a JSON object) with the project (3MF).
    bool submit(const std::string &kind, const std::string &params_json, const std::string &project_path, Job &job,
                std::string &error);
    bool status(const std::string &job_id, Job &job, std::string &error);
    bool cancel(const std::string &job_id, std::string &error);
    bool download(const std::string &job_id, const std::string &path, std::string &error);

    // Sends the job, waits for it (update receives its state: queued or running and the progress) and downloads the
    // result to result_path. cancel is polled: the job is cancelled on the server too. On failure error tells why (the
    // message of the server when there is one).
    bool run(const std::string &kind, const std::string &params_json, const std::string &project_path,
             const std::string &result_path, Job &job, std::string &error, std::function<void(const Job&)> update,
             const std::atomic<bool> &cancel);

private:
    Http& authorize(Http &http) const;

    std::string m_url;
    std::string m_token;
};

} // namespace Slic3r

#endif // slic3r_TismaCloud_hpp_
