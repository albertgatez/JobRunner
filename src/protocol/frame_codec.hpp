#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace jobrunner {

// Wire framing: a 4-byte big-endian length prefix followed by that many
// bytes of UTF-8 JSON payload. This is what makes the protocol tolerant of
// partial reads/writes over a stream socket (RF-21, RNF-24) — the caller
// keeps feeding bytes as they arrive and only acts once a full frame is
// available. JobManager and the rest of the domain never see this format;
// only Connection (server side) and the CLI client do.
class FrameCodec {
   public:
    static constexpr std::size_t kMaxFrameSize = 1 << 20;  // 1 MiB, guards RNF-14

    // Appends raw bytes as they're read from the socket.
    void feed(const char* data, std::size_t len);

    // Returns the next complete frame's payload if one is available and
    // removes it from the internal buffer. Returns std::nullopt if more
    // bytes are needed. Throws std::runtime_error if a declared frame
    // length exceeds kMaxFrameSize.
    std::optional<std::string> try_extract_frame();

    static std::string encode_frame(const std::string& payload);

   private:
    std::string buffer_;
};

}  // namespace jobrunner