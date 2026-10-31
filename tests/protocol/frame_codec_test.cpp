// RF-21, RNF-14, RNF-24, TC-012. Framing: [4 bytes big-endian][payload].
#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>

#include "protocol/frame_codec.hpp"

using jobrunner::FrameCodec;

TEST_CASE("encode_frame antepone la longitud en big-endian", "[codec]") {
    std::string frame = FrameCodec::encode_frame("hola");
    REQUIRE(frame.size() == 8);
    REQUIRE(frame.substr(0, 4) == std::string("\x00\x00\x00\x04", 4));
    REQUIRE(frame.substr(4) == "hola");
}

TEST_CASE("un frame completo se extrae de una sola lectura", "[codec]") {
    FrameCodec codec;
    std::string frame = FrameCodec::encode_frame(R"({"op":"list"})");
    codec.feed(frame.data(), frame.size());

    auto payload = codec.try_extract_frame();
    REQUIRE(payload.has_value());
    REQUIRE(*payload == R"({"op":"list"})");
    REQUIRE_FALSE(codec.try_extract_frame().has_value());
}

TEST_CASE("sin bytes suficientes no hay frame (ni siquiera el encabezado)", "[codec]") {
    FrameCodec codec;
    REQUIRE_FALSE(codec.try_extract_frame().has_value());
    codec.feed("\x00\x00", 2);
    REQUIRE_FALSE(codec.try_extract_frame().has_value());
}

TEST_CASE("lecturas parciales: alimentar byte a byte reconstruye el frame", "[codec]") {
    FrameCodec codec;
    std::string frame = FrameCodec::encode_frame("fragmentado");

    for (std::size_t i = 0; i + 1 < frame.size(); ++i) {
        codec.feed(&frame[i], 1);
        REQUIRE_FALSE(codec.try_extract_frame().has_value());
    }
    codec.feed(&frame.back(), 1);

    auto payload = codec.try_extract_frame();
    REQUIRE(payload.has_value());
    REQUIRE(*payload == "fragmentado");
}

TEST_CASE("varios frames en un mismo feed salen en orden", "[codec]") {
    FrameCodec codec;
    std::string bytes = FrameCodec::encode_frame("uno") + FrameCodec::encode_frame("dos") +
                        FrameCodec::encode_frame("tres");
    codec.feed(bytes.data(), bytes.size());

    REQUIRE(*codec.try_extract_frame() == "uno");
    REQUIRE(*codec.try_extract_frame() == "dos");
    REQUIRE(*codec.try_extract_frame() == "tres");
    REQUIRE_FALSE(codec.try_extract_frame().has_value());
}

TEST_CASE("el sobrante de un frame queda para el siguiente", "[codec]") {
    FrameCodec codec;
    std::string first = FrameCodec::encode_frame("a");
    std::string second = FrameCodec::encode_frame("bb");
    std::string bytes = first + second.substr(0, 3);  // frame 2 incompleto
    codec.feed(bytes.data(), bytes.size());

    REQUIRE(*codec.try_extract_frame() == "a");
    REQUIRE_FALSE(codec.try_extract_frame().has_value());

    codec.feed(second.data() + 3, second.size() - 3);
    REQUIRE(*codec.try_extract_frame() == "bb");
}

TEST_CASE("payload vacio es un frame valido", "[codec]") {
    FrameCodec codec;
    std::string frame = FrameCodec::encode_frame("");
    codec.feed(frame.data(), frame.size());
    auto payload = codec.try_extract_frame();
    REQUIRE(payload.has_value());
    REQUIRE(payload->empty());
}

TEST_CASE("una longitud mayor a kMaxFrameSize se rechaza con excepcion", "[codec]") {
    FrameCodec codec;
    // 0x00100001 = kMaxFrameSize + 1
    codec.feed("\x00\x10\x00\x01", 4);
    REQUIRE_THROWS_AS(codec.try_extract_frame(), std::runtime_error);
}

TEST_CASE("una longitud exactamente igual a kMaxFrameSize se acepta y espera datos", "[codec]") {
    FrameCodec codec;
    // 0x00100000 = kMaxFrameSize
    codec.feed("\x00\x10\x00\x00", 4);
    REQUIRE_FALSE(codec.try_extract_frame().has_value());
}

TEST_CASE("encode/decode de un payload grande", "[codec]") {
    FrameCodec codec;
    std::string big(100000, 'x');
    std::string frame = FrameCodec::encode_frame(big);
    codec.feed(frame.data(), frame.size());
    REQUIRE(*codec.try_extract_frame() == big);
}
