///|/ Copyright (c) Tisma Slicer contributors
///|/
///|/ Tisma Slicer is released under the terms of the AGPLv3 or higher
///|/
#include "BambuLanClient.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <regex>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#else
#include <sys/select.h>
#endif

#include <boost/algorithm/hex.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/uuid/detail/md5.hpp>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "libslic3r/Utils.hpp"
#include "libslic3r/miniz_extension.hpp"

namespace fs = boost::filesystem;

namespace Slic3r {

// ------------------------------------------------------------------------------------------------------------------
// MQTT 3.1.1 packets (only what the printer needs: connect, subscribe, QoS 0 publish, ping).
// ------------------------------------------------------------------------------------------------------------------
namespace BambuMqtt {

static void put_u16(std::string &out, uint16_t v)
{
    out += char(v >> 8);
    out += char(v & 0xff);
}

static void put_string(std::string &out, const std::string &s)
{
    put_u16(out, uint16_t(s.size()));
    out += s;
}

static std::string packet(uint8_t type, uint8_t flags, const std::string &body)
{
    std::string out;
    out += char((type << 4) | (flags & 0x0f));
    // Remaining length, 7 bits per byte.
    size_t len = body.size();
    do {
        uint8_t b = uint8_t(len % 128);
        len /= 128;
        if (len > 0)
            b |= 0x80;
        out += char(b);
    } while (len > 0);
    return out + body;
}

std::string connect_packet(const std::string &client_id, const std::string &user, const std::string &password, uint16_t keep_alive_s)
{
    std::string body;
    put_string(body, "MQTT");
    body += char(4);                                   // protocol level 3.1.1
    body += char(0x80 | 0x40 | 0x02);                  // user name, password, clean session
    put_u16(body, keep_alive_s);
    put_string(body, client_id);
    put_string(body, user);
    put_string(body, password);
    return packet(CONNECT, 0, body);
}

std::string subscribe_packet(uint16_t packet_id, const std::string &topic)
{
    std::string body;
    put_u16(body, packet_id);
    put_string(body, topic);
    body += char(0);                                   // QoS 0
    return packet(SUBSCRIBE, 0x02, body);
}

std::string publish_packet(const std::string &topic, const std::string &payload)
{
    std::string body;
    put_string(body, topic);
    body += payload;
    return packet(PUBLISH, 0, body);
}

std::string pingreq_packet() { return packet(PINGREQ, 0, {}); }
std::string disconnect_packet() { return packet(DISCONNECT, 0, {}); }

size_t parse_packet(const std::string &buf, Packet &out)
{
    if (buf.size() < 2)
        return 0;
    size_t len = 0;
    size_t multiplier = 1;
    size_t pos = 1;
    for (;; ++pos) {
        if (pos >= buf.size())
            return 0;
        if (pos > 4)
            throw std::runtime_error("MQTT: malformed remaining length");
        const uint8_t b = uint8_t(buf[pos]);
        len += size_t(b & 0x7f) * multiplier;
        multiplier *= 128;
        if ((b & 0x80) == 0)
            break;
    }
    const size_t header = pos + 1;
    if (buf.size() < header + len)
        return 0;
    out.type  = uint8_t(buf[0]) >> 4;
    out.flags = uint8_t(buf[0]) & 0x0f;
    out.body  = buf.substr(header, len);
    return header + len;
}

bool parse_publish(const Packet &packet, std::string &topic, std::string &payload)
{
    if (packet.type != PUBLISH || packet.body.size() < 2)
        return false;
    const size_t topic_len = (size_t(uint8_t(packet.body[0])) << 8) | uint8_t(packet.body[1]);
    size_t pos = 2 + topic_len;
    if (pos > packet.body.size())
        return false;
    topic = packet.body.substr(2, topic_len);
    // QoS 1 and 2 carry a packet identifier.
    if (((packet.flags >> 1) & 3) != 0)
        pos += 2;
    if (pos > packet.body.size())
        return false;
    payload = packet.body.substr(pos);
    return true;
}

} // namespace BambuMqtt

bool BambuStatus::update_from_report(const std::string &json)
{
    try {
        const nlohmann::json j = nlohmann::json::parse(json);
        if (!j.contains("print") || !j["print"].is_object())
            return false;
        const nlohmann::json &p = j["print"];
        auto get_number = [&p](const char *key, double &value) {
            if (p.contains(key) && p[key].is_number())
                value = p[key].get<double>();
        };
        if (p.contains("gcode_state") && p["gcode_state"].is_string())
            gcode_state = p["gcode_state"].get<std::string>();
        double v = -1.;
        get_number("mc_percent", v);
        if (v >= 0.)
            percent = int(v);
        v = -1.;
        get_number("mc_remaining_time", v);
        if (v >= 0.)
            remaining_min = int(v);
        get_number("nozzle_temper", nozzle_temp);
        get_number("bed_temper", bed_temp);
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

// ------------------------------------------------------------------------------------------------------------------
// The job file.
// ------------------------------------------------------------------------------------------------------------------
std::string file_md5_upper_hex(const fs::path &path)
{
    using boost::uuids::detail::md5;
    md5 hash;
    boost::nowide::ifstream in(path.string(), std::ios::binary);
    std::vector<char> buffer(1 << 16);
    while (in) {
        in.read(buffer.data(), std::streamsize(buffer.size()));
        if (const std::streamsize n = in.gcount(); n > 0)
            hash.process_bytes(buffer.data(), size_t(n));
    }
    md5::digest_type digest{};
    hash.get_digest(digest);
    std::string bytes;
    if constexpr (sizeof(digest[0]) == 1) {
        // Boost >= 1.86: the 16 bytes of the hash.
        bytes.assign(reinterpret_cast<const char *>(&digest[0]), sizeof(digest));
    } else {
        // Older Boost: four 32 bit words whose most significant byte comes first in the hash.
        for (const auto word : digest)
            for (int shift = 24; shift >= 0; shift -= 8)
                bytes += char((uint32_t(word) >> shift) & 0xff);
    }
    std::string hex;
    boost::algorithm::hex(bytes.begin(), bytes.end(), std::back_inserter(hex));
    return hex;
}

bool write_bambu_gcode_3mf(const fs::path &gcode, const fs::path &out_3mf, std::string &error)
{
    static const char *content_types =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">\n"
        " <Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>\n"
        " <Default Extension=\"model\" ContentType=\"application/vnd.ms-package.3dmanufacturing-3dmodel+xml\"/>\n"
        " <Default Extension=\"png\" ContentType=\"image/png\"/>\n"
        " <Default Extension=\"gcode\" ContentType=\"text/x.gcode\"/>\n"
        "</Types>\n";
    static const char *rels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
        " <Relationship Target=\"/3D/3dmodel.model\" Id=\"rel-1\" Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\"/>\n"
        "</Relationships>\n";
    static const char *model =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<model unit=\"millimeter\" xml:lang=\"en-US\" xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\">\n"
        " <metadata name=\"Application\">TismaSlicer</metadata>\n"
        " <resources>\n </resources>\n <build/>\n</model>\n";
    static const char *slice_info =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<config>\n  <plate>\n    <metadata key=\"index\" value=\"1\"/>\n  </plate>\n</config>\n";

    if (!fs::exists(gcode)) {
        error = "G-code file not found: " + gcode.string();
        return false;
    }
    const std::string md5 = file_md5_upper_hex(gcode);

    mz_zip_archive archive;
    mz_zip_zero_struct(&archive);
    if (!open_zip_writer(&archive, out_3mf.string())) {
        error = "Cannot create " + out_3mf.string();
        return false;
    }
    auto add = [&archive](const char *name, const std::string &data) {
        return mz_zip_writer_add_mem(&archive, name, data.data(), data.size(), MZ_DEFAULT_COMPRESSION) != 0;
    };
    bool ok = add("[Content_Types].xml", content_types) && add("_rels/.rels", rels) && add("3D/3dmodel.model", model) &&
              add("Metadata/slice_info.config", slice_info) && add("Metadata/plate_1.gcode.md5", md5);
    if (ok) {
        // The G-code can be large: stream it from the file.
        boost::nowide::ifstream in(gcode.string(), std::ios::binary);
        std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        ok = add("Metadata/plate_1.gcode", data);
    }
    ok = mz_zip_writer_finalize_archive(&archive) != 0 && ok;
    close_zip_writer(&archive);
    if (!ok) {
        error = "Cannot write " + out_3mf.string();
        fs::remove(out_3mf);
    }
    return ok;
}

std::string bambu_project_file_command(const std::string &file_name, bool use_ams, int sequence_id)
{
    nlohmann::json print = {
        { "sequence_id", std::to_string(sequence_id) },
        { "command", "project_file" },
        { "param", "Metadata/plate_1.gcode" },
        { "subtask_name", file_name },
        { "url", "ftp://" + file_name },
        { "timelapse", false },
        { "bed_leveling", true },
        { "flow_cali", false },
        { "vibration_cali", false },
        { "layer_inspect", false },
        { "use_ams", use_ams },
        { "profile_id", "0" },
        { "project_id", "0" },
        { "subtask_id", "0" },
        { "task_id", "0" },
    };
    return nlohmann::json{ { "print", print } }.dump();
}

// ------------------------------------------------------------------------------------------------------------------
// TLS stream through libcurl (OpenSSL on Linux, Schannel on Windows), so no other TLS library is needed.
// ------------------------------------------------------------------------------------------------------------------
namespace {

class TlsStream
{
public:
    ~TlsStream() { if (m_curl) curl_easy_cleanup(m_curl); }

    bool open(const std::string &host, int port, const std::string &cafile, std::string &error)
    {
        m_curl = curl_easy_init();
        if (m_curl == nullptr) {
            error = "curl_easy_init failed";
            return false;
        }
        const std::string url = "https://" + host + ":" + std::to_string(port) + "/";
        curl_easy_setopt(m_curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(m_curl, CURLOPT_CONNECT_ONLY, 1L);
        curl_easy_setopt(m_curl, CURLOPT_CONNECTTIMEOUT, 10L);
        curl_easy_setopt(m_curl, CURLOPT_NOSIGNAL, 1L);
        // The certificate must be issued by Bambu Lab; its name is the serial number, not the IP address.
        curl_easy_setopt(m_curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(m_curl, CURLOPT_SSL_VERIFYHOST, 0L);
        if (!cafile.empty())
            curl_easy_setopt(m_curl, CURLOPT_CAINFO, cafile.c_str());
        curl_easy_setopt(m_curl, CURLOPT_CERTINFO, 1L);
        char errbuf[CURL_ERROR_SIZE] = "";
        curl_easy_setopt(m_curl, CURLOPT_ERRORBUFFER, errbuf);
        const CURLcode res = curl_easy_perform(m_curl);
        if (res != CURLE_OK) {
            error = errbuf[0] ? errbuf : curl_easy_strerror(res);
            return false;
        }
        curl_easy_setopt(m_curl, CURLOPT_ERRORBUFFER, nullptr);
        return true;
    }

    // Common name of the server certificate (the serial number of the printer).
    std::string peer_common_name() const
    {
        struct curl_certinfo *info = nullptr;
        if (curl_easy_getinfo(m_curl, CURLINFO_CERTINFO, &info) != CURLE_OK || info == nullptr || info->num_of_certs < 1)
            return {};
        static const std::regex cn_re(R"(CN\s*=\s*([^,/\s]+))");
        for (curl_slist *s = info->certinfo[0]; s != nullptr; s = s->next) {
            const std::string line = s->data;
            if (line.rfind("Subject:", 0) == 0) {
                std::smatch m;
                if (std::regex_search(line, m, cn_re))
                    return m[1];
            }
        }
        return {};
    }

    bool send_all(const std::string &data, std::string &error)
    {
        size_t sent_total = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (sent_total < data.size()) {
            size_t sent = 0;
            const CURLcode res = curl_easy_send(m_curl, data.data() + sent_total, data.size() - sent_total, &sent);
            if (res == CURLE_AGAIN) {
                if (std::chrono::steady_clock::now() > deadline) {
                    error = "send timeout";
                    return false;
                }
                wait(false, 100);
                continue;
            }
            if (res != CURLE_OK) {
                error = curl_easy_strerror(res);
                return false;
            }
            sent_total += sent;
        }
        return true;
    }

    // Appends what is available within timeout_ms; false on a closed or broken connection.
    bool recv_some(std::string &buf, int timeout_ms, std::string &error)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        char tmp[16384];
        for (;;) {
            size_t n = 0;
            const CURLcode res = curl_easy_recv(m_curl, tmp, sizeof(tmp), &n);
            if (res == CURLE_OK) {
                if (n == 0) {
                    error = "connection closed by the printer";
                    return false;
                }
                buf.append(tmp, n);
                return true;
            }
            if (res != CURLE_AGAIN) {
                error = curl_easy_strerror(res);
                return false;
            }
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline)
                return true;
            wait(true, int(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count()));
        }
    }

private:
    CURL *m_curl{ nullptr };

    void wait(bool for_read, int timeout_ms)
    {
        curl_socket_t sock = CURL_SOCKET_BAD;
        if (curl_easy_getinfo(m_curl, CURLINFO_ACTIVESOCKET, &sock) != CURLE_OK || sock == CURL_SOCKET_BAD) {
            std::this_thread::sleep_for(std::chrono::milliseconds(std::min(timeout_ms, 50)));
            return;
        }
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        timeval tv{ timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
        select(int(sock) + 1, for_read ? &fds : nullptr, for_read ? nullptr : &fds, nullptr, &tv);
    }
};

// One MQTT session with the printer.
class MqttSession
{
public:
    bool open(const std::string &host, int port, const std::string &access_code, const std::string &cafile, std::string &error)
    {
        if (!m_tls.open(host, port, cafile, error)) {
            error = "TLS: " + error;
            return false;
        }
        m_serial = m_tls.peer_common_name();
        std::mt19937 rng{ std::random_device{}() };
        const std::string client_id = "tisma_" + std::to_string(rng() % 1000000);
        if (!m_tls.send_all(BambuMqtt::connect_packet(client_id, "bblp", access_code, 60), error))
            return false;
        BambuMqtt::Packet p;
        if (!read_packet(p, 10000, error))
            return false;
        if (p.type != BambuMqtt::CONNACK || p.body.size() < 2) {
            error = "MQTT: unexpected answer to CONNECT";
            return false;
        }
        const int rc = uint8_t(p.body[1]);
        if (rc == 4 || rc == 5) {
            error = "access code";
            m_bad_access_code = true;
            return false;
        }
        if (rc != 0) {
            error = "MQTT: connection refused (" + std::to_string(rc) + ")";
            return false;
        }
        // Without the serial number in the certificate, take it from the first report of any printer.
        if (!m_tls.send_all(BambuMqtt::subscribe_packet(1, m_serial.empty() ? "device/+/report" : "device/" + m_serial + "/report"), error))
            return false;
        return true;
    }

    bool publish(const std::string &json, std::string &error)
    {
        return m_tls.send_all(BambuMqtt::publish_packet("device/" + m_serial + "/request", json), error);
    }

    // Waits for a report of the printer; returns its payload.
    bool wait_report(std::string &payload, int timeout_ms, std::string &error)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        for (;;) {
            const int left = int(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count());
            if (left <= 0) {
                error = "no answer from the printer";
                return false;
            }
            BambuMqtt::Packet p;
            if (!read_packet(p, left, error))
                return false;
            std::string topic;
            if (BambuMqtt::parse_publish(p, topic, payload)) {
                static const std::regex topic_re(R"(device/([^/]+)/report)");
                std::smatch m;
                if (std::regex_match(topic, m, topic_re)) {
                    if (m_serial.empty())
                        m_serial = m[1];
                    return true;
                }
            }
        }
    }

    void close()
    {
        std::string ignored;
        m_tls.send_all(BambuMqtt::disconnect_packet(), ignored);
    }

    const std::string &serial() const { return m_serial; }
    bool bad_access_code() const { return m_bad_access_code; }

private:
    TlsStream   m_tls;
    std::string m_buf;
    std::string m_serial;
    bool        m_bad_access_code{ false };

    bool read_packet(BambuMqtt::Packet &p, int timeout_ms, std::string &error)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        for (;;) {
            if (const size_t used = BambuMqtt::parse_packet(m_buf, p); used > 0) {
                m_buf.erase(0, used);
                return true;
            }
            const int left = int(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count());
            if (left <= 0) {
                error = "timeout";
                return false;
            }
            if (!m_tls.recv_some(m_buf, left, error))
                return false;
        }
    }
};

size_t read_callback(char *buffer, size_t size, size_t nitems, void *userdata)
{
    return fread(buffer, size, nitems, static_cast<FILE *>(userdata));
}

int xferinfo_callback(void *userdata, curl_off_t, curl_off_t, curl_off_t ultotal, curl_off_t ulnow)
{
    auto *fn = static_cast<const BambuLanClient::ProgressFn *>(userdata);
    // Returning non zero cancels the transfer.
    return fn && *fn && !(*fn)(size_t(ulnow), size_t(ultotal)) ? 1 : 0;
}

} // namespace

// ------------------------------------------------------------------------------------------------------------------
// Client.
// ------------------------------------------------------------------------------------------------------------------
static const char *PUSHALL = R"({"pushing":{"sequence_id":"1","command":"pushall","version":1,"push_target":1}})";

bool BambuLanClient::query_status(BambuStatus &status, std::string &error, int timeout_s) const
{
    MqttSession session;
    if (!session.open(host, mqtt_port, access_code, cafile, error))
        return false;
    // "pushall" makes the printer send its full state.
    bool pushed = false;
    if (!session.serial().empty()) {
        if (!session.publish(PUSHALL, error))
            return false;
        pushed = true;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_s);
    for (;;) {
        const int left = int(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count());
        std::string payload;
        if (left <= 0 || !session.wait_report(payload, std::max(left, 1), error)) {
            if (left <= 0)
                error = "no answer from the printer";
            session.close();
            return false;
        }
        if (!pushed) {
            // The serial number came with the first report: now ask for the full state.
            if (!session.publish(PUSHALL, error))
                return false;
            pushed = true;
        }
        if (status.update_from_report(payload) && !status.gcode_state.empty()) {
            status.serial = session.serial();
            session.close();
            return true;
        }
    }
}

bool BambuLanClient::send_command(const std::string &json, std::string &error) const
{
    MqttSession session;
    if (!session.open(host, mqtt_port, access_code, cafile, error))
        return false;
    if (session.serial().empty()) {
        std::string payload;
        if (!session.wait_report(payload, 10000, error))
            return false;
    }
    const bool ok = session.publish(json, error);
    // Let the printer read the command before closing.
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    session.close();
    return ok;
}

bool BambuLanClient::upload_file(const fs::path &file, const std::string &name, const ProgressFn &progress, std::string &error) const
{
    FILE *f = boost::nowide::fopen(file.string().c_str(), "rb");
    if (f == nullptr) {
        error = "Cannot read " + file.string();
        return false;
    }
    CURL *curl = curl_easy_init();
    const std::string url = "ftps://" + host + ":" + std::to_string(ftps_port) + "/" + name;
    char errbuf[CURL_ERROR_SIZE] = "";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERNAME, "bblp");
    curl_easy_setopt(curl, CURLOPT_PASSWORD, access_code.c_str());
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);
    curl_easy_setopt(curl, CURLOPT_READDATA, f);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, curl_off_t(fs::file_size(file)));
    curl_easy_setopt(curl, CURLOPT_FTP_USE_EPSV, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    if (!cafile.empty())
        curl_easy_setopt(curl, CURLOPT_CAINFO, cafile.c_str());
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferinfo_callback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    const CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    fclose(f);
    if (res == CURLE_OK)
        return true;
    if (res == CURLE_ABORTED_BY_CALLBACK)
        error = "cancelled";
    else if (res == CURLE_LOGIN_DENIED)
        error = "access code";
    else
        error = errbuf[0] ? errbuf : curl_easy_strerror(res);
    return false;
}

std::string BambuLanClient::job_name(const std::string &upload_name)
{
    std::string name = fs::path(upload_name).filename().string();
    for (const char *suffix : { ".gcode", ".gco", ".g" })
        if (boost::algorithm::iends_with(name, suffix)) {
            name.erase(name.size() - strlen(suffix));
            break;
        }
    for (char &c : name)
        if (c == ' ' || c == '#' || c == '?' || c == '%')
            c = '_';
    return name + ".gcode.3mf";
}

bool BambuLanClient::print(const fs::path &gcode, const std::string &upload_name, bool start, bool use_ams,
                           const ProgressFn &progress, std::string &error) const
{
    const std::string name = job_name(upload_name);
    const fs::path job = fs::temp_directory_path() / fs::unique_path("tisma-bambu-%%%%%%%%.gcode.3mf");
    if (!write_bambu_gcode_3mf(gcode, job, error))
        return false;
    const bool uploaded = upload_file(job, name, progress, error);
    boost::system::error_code ec;
    fs::remove(job, ec);
    if (!uploaded)
        return false;
    BOOST_LOG_TRIVIAL(info) << "Bambu Lab: uploaded " << name << " to " << host;
    if (start && !send_command(bambu_project_file_command(name, use_ams, 2), error)) {
        error = "start: " + error;
        return false;
    }
    return true;
}

} // namespace Slic3r
