///|/ Copyright (c) Tisma Slicer contributors
///|/
///|/ Tisma Slicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_BambuLanClient_hpp_
#define slic3r_BambuLanClient_hpp_

#include <cstdint>
#include <functional>
#include <string>

#include <boost/filesystem/path.hpp>

namespace Slic3r {

// Bambu Lab printers in "LAN only" + "Developer mode", without Bambu's closed network plugin. Written from the
// protocol notes of OpenBambuAPI (https://github.com/Doridian/OpenBambuAPI):
// - MQTT 3.1.1 over TLS on port 8883, user "bblp", password = LAN access code, topics device/<serial>/report and
//   device/<serial>/request. The serial number is the common name of the printer's certificate.
// - Implicit FTPS on port 990 with the same credentials to upload the job.
// - The job is a .gcode.3mf with the G-code in Metadata/plate_1.gcode, started by the "project_file" command.
namespace BambuMqtt {
    enum PacketType : uint8_t { CONNECT = 1, CONNACK = 2, PUBLISH = 3, SUBSCRIBE = 8, SUBACK = 9, PINGREQ = 12, PINGRESP = 13, DISCONNECT = 14 };

    std::string connect_packet(const std::string &client_id, const std::string &user, const std::string &password, uint16_t keep_alive_s);
    std::string subscribe_packet(uint16_t packet_id, const std::string &topic);
    // QoS 0.
    std::string publish_packet(const std::string &topic, const std::string &payload);
    std::string pingreq_packet();
    std::string disconnect_packet();

    struct Packet {
        uint8_t     type{ 0 };
        uint8_t     flags{ 0 };
        std::string body;
    };
    // Parses the packet at the start of buf. Returns the bytes it takes, 0 when buf does not hold a whole packet yet.
    // Throws std::runtime_error on a malformed length.
    size_t parse_packet(const std::string &buf, Packet &out);
    // Topic and payload of a PUBLISH packet (any QoS).
    bool   parse_publish(const Packet &packet, std::string &topic, std::string &payload);
} // namespace BambuMqtt

struct BambuStatus
{
    std::string serial;
    std::string gcode_state;   // IDLE, PREPARE, RUNNING, PAUSE, FINISH, FAILED
    int         percent{ -1 };
    int         remaining_min{ -1 };
    double      nozzle_temp{ -1. };
    double      bed_temp{ -1. };
    // Fills the fields present in a report payload; returns false when it is not a "print" report.
    bool update_from_report(const std::string &json);
};

// Packs a G-code file as the .gcode.3mf that Bambu printers start with "project_file".
bool write_bambu_gcode_3mf(const boost::filesystem::path &gcode, const boost::filesystem::path &out_3mf, std::string &error);
// MD5 of a file in upper case hex, as in Metadata/plate_1.gcode.md5.
std::string file_md5_upper_hex(const boost::filesystem::path &path);
// JSON of the "project_file" command that prints <file_name> uploaded to the root of the SD card.
std::string bambu_project_file_command(const std::string &file_name, bool use_ams, int sequence_id);

// Connection to one printer (no GUI code, used by the print host and the tests).
struct BambuLanClient
{
    std::string host;         // IP address
    std::string access_code;  // LAN access code shown on the printer
    std::string cafile;       // CA of the printer certificates
    int         mqtt_port{ 8883 };
    int         ftps_port{ 990 };

    // (bytes sent, total); returns false to cancel.
    using ProgressFn = std::function<bool(size_t, size_t)>;

    // Connects, asks for the full status and waits for it. Errors: "access code" when the printer rejects it,
    // otherwise a description.
    bool query_status(BambuStatus &status, std::string &error, int timeout_s = 10) const;
    // Publishes a command on device/<serial>/request.
    bool send_command(const std::string &json, std::string &error) const;
    // Uploads a file to the root of the SD card. Errors: "cancelled", "access code" or a description.
    bool upload_file(const boost::filesystem::path &file, const std::string &name, const ProgressFn &progress, std::string &error) const;
    // Packs the G-code as <name>.gcode.3mf, uploads it and starts it.
    bool print(const boost::filesystem::path &gcode, const std::string &upload_name, bool start, bool use_ams,
               const ProgressFn &progress, std::string &error) const;
    // Name on the SD card for an upload name ("my part.gcode" -> "my_part.gcode.3mf").
    static std::string job_name(const std::string &upload_name);
};

} // namespace Slic3r

#endif // slic3r_BambuLanClient_hpp_
