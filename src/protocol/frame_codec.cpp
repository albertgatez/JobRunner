#include "./frame_codec.hpp"

#include <arpa/inet.h>

#include <cstring>
#include <stdexcept>

namespace jobrunner {

// Agrega bytes crudos al buffer interno.
// data: puntero al bloque de entrada.
// len: cantidad de bytes a anexar.
void FrameCodec::feed(const char* data, std::size_t len) { buffer_.append(data, len); }

// Intenta extraer un frame completo desde el buffer.
std::optional<std::string> FrameCodec::try_extract_frame() {
    if (buffer_.size() < sizeof(std::uint32_t)) return std::nullopt;

    std::uint32_t net_len;
    std::memcpy(&net_len, buffer_.data(), sizeof(net_len));
    std::uint32_t len = ntohl(net_len);

    if (len > kMaxFrameSize) {
        throw std::runtime_error("frame excede el tamano maximo permitido");
    }

    if (buffer_.size() < sizeof(std::uint32_t) + len) return std::nullopt;

    std::string payload = buffer_.substr(sizeof(std::uint32_t), len);
    buffer_.erase(0, sizeof(std::uint32_t) + len);
    return payload;
}

// Serializa payload a formato framed.
// payload: contenido JSON serializado.
std::string FrameCodec::encode_frame(const std::string& payload) {
    std::uint32_t len = htonl(static_cast<std::uint32_t>(payload.size()));
    std::string out;
    out.resize(sizeof(len));
    std::memcpy(out.data(), &len, sizeof(len));
    out += payload;
    return out;
}

}  // namespace jobrunner