#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace jobrunner {

/**
 * Codec de frames con prefijo de longitud (4 bytes big-endian).
 */
class FrameCodec {
   public:
    static constexpr std::size_t kMaxFrameSize = 1 << 20;  // 1 MiB, guards RNF-14

    /**
     * Agrega bytes crudos al buffer interno.
     * @param data Puntero al bloque de bytes.
     * @param len Cantidad de bytes del bloque.
     */
    void feed(const char* data, std::size_t len);

    /**
     * Extrae el siguiente frame completo disponible.
     * @return Payload del frame o std::nullopt si faltan bytes.
     */
    std::optional<std::string> try_extract_frame();

    /**
     * Construye un frame serializado.
     * @param payload Contenido a encapsular.
     * @return Bytes del frame completo.
     */
    static std::string encode_frame(const std::string& payload);

   private:
    std::string buffer_;
};

}  // namespace jobrunner