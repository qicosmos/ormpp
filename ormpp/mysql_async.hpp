#pragma once

#ifdef ORMPP_ENABLE_MYSQL_ASYNC

#include <asio.hpp>
#include <asio/steady_timer.hpp>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "async_traits.hpp"
#include "entity.hpp"
#include "query.hpp"
#include "type_mapping.hpp"

namespace ormpp {

namespace detail::mysql_async {

using byte = std::uint8_t;
using bytes = std::vector<byte>;

constexpr std::uint32_t max_packet_chunk = 0x00ffffffu;
constexpr std::uint32_t status_no_backslash_escapes = 0x0200u;

constexpr std::uint32_t client_long_password = 0x00000001u;
constexpr std::uint32_t client_found_rows = 0x00000002u;
constexpr std::uint32_t client_long_flag = 0x00000004u;
constexpr std::uint32_t client_connect_with_db = 0x00000008u;
constexpr std::uint32_t client_protocol_41 = 0x00000200u;
constexpr std::uint32_t client_transactions = 0x00002000u;
constexpr std::uint32_t client_secure_connection = 0x00008000u;
constexpr std::uint32_t client_multi_statements = 0x00010000u;
constexpr std::uint32_t client_multi_results = 0x00020000u;
constexpr std::uint32_t client_plugin_auth = 0x00080000u;
constexpr std::uint32_t client_connect_attrs = 0x00100000u;
constexpr std::uint32_t client_plugin_auth_lenenc_data = 0x00200000u;
constexpr std::uint32_t client_deprecate_eof = 0x01000000u;
constexpr std::uint32_t client_optional_resultset_metadata = 0x02000000u;

constexpr std::string_view mysql_native_password = "mysql_native_password";
constexpr std::string_view caching_sha2_password = "caching_sha2_password";

struct packet_header {
  std::uint32_t payload_size{};
  byte sequence_id{};
};

struct ok_packet {
  std::uint64_t affected_rows{};
  std::uint64_t last_insert_id{};
  std::uint16_t status_flags{};
  std::uint16_t warnings{};
  std::string info;
};

struct error_packet {
  std::uint16_t code{};
  std::string sql_state;
  std::string message;
};

struct column_definition {
  std::string schema;
  std::string table;
  std::string org_table;
  std::string name;
  std::string org_name;
  std::uint16_t character_set{};
  std::uint32_t column_length{};
  byte type{};
  std::uint16_t flags{};
  byte decimals{};
};

struct server_handshake {
  byte protocol_version{};
  std::string server_version;
  std::uint32_t connection_id{};
  std::array<byte, 20> scramble{};
  std::uint32_t capabilities{};
  byte character_set{};
  std::uint16_t status_flags{};
  std::string auth_plugin_name;
};

struct query_result {
  bool has_resultset = false;
  ok_packet ok{};
  std::vector<column_definition> columns;
  std::vector<std::vector<std::optional<std::string>>> rows;
};

inline std::uint16_t read_le16(const byte* data) {
  return static_cast<std::uint16_t>(data[0]) |
         (static_cast<std::uint16_t>(data[1]) << 8);
}

inline std::uint32_t read_le24(const byte* data) {
  return static_cast<std::uint32_t>(data[0]) |
         (static_cast<std::uint32_t>(data[1]) << 8) |
         (static_cast<std::uint32_t>(data[2]) << 16);
}

inline std::uint32_t read_le32(const byte* data) {
  return static_cast<std::uint32_t>(data[0]) |
         (static_cast<std::uint32_t>(data[1]) << 8) |
         (static_cast<std::uint32_t>(data[2]) << 16) |
         (static_cast<std::uint32_t>(data[3]) << 24);
}

inline std::uint64_t read_le64(const byte* data) {
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value |= static_cast<std::uint64_t>(data[i]) << (i * 8);
  }
  return value;
}

inline void append_le16(bytes& out, std::uint16_t value) {
  out.push_back(static_cast<byte>(value & 0xff));
  out.push_back(static_cast<byte>((value >> 8) & 0xff));
}

inline void append_le24(bytes& out, std::uint32_t value) {
  out.push_back(static_cast<byte>(value & 0xff));
  out.push_back(static_cast<byte>((value >> 8) & 0xff));
  out.push_back(static_cast<byte>((value >> 16) & 0xff));
}

inline void append_le32(bytes& out, std::uint32_t value) {
  out.push_back(static_cast<byte>(value & 0xff));
  out.push_back(static_cast<byte>((value >> 8) & 0xff));
  out.push_back(static_cast<byte>((value >> 16) & 0xff));
  out.push_back(static_cast<byte>((value >> 24) & 0xff));
}

inline void append_le64(bytes& out, std::uint64_t value) {
  for (int i = 0; i < 8; ++i) {
    out.push_back(static_cast<byte>((value >> (i * 8)) & 0xff));
  }
}

inline std::runtime_error make_protocol_error(const std::string& msg) {
  return std::runtime_error("mysql_async protocol error: " + msg);
}

struct packet_reader {
  const byte* first{};
  const byte* cur{};
  const byte* last{};

  explicit packet_reader(const bytes& data)
      : first(data.data()), cur(data.data()), last(data.data() + data.size()) {}

  packet_reader(const byte* begin, const byte* end)
      : first(begin), cur(begin), last(end) {}

  std::size_t remaining() const {
    return static_cast<std::size_t>(last - cur);
  }

  bool empty() const { return cur == last; }

  void require(std::size_t size) const {
    if (remaining() < size) {
      throw make_protocol_error("truncated packet");
    }
  }

  byte read_byte() {
    require(1);
    return *cur++;
  }

  std::uint16_t read_u16() {
    require(2);
    auto v = read_le16(cur);
    cur += 2;
    return v;
  }

  std::uint32_t read_u24() {
    require(3);
    auto v = read_le24(cur);
    cur += 3;
    return v;
  }

  std::uint32_t read_u32() {
    require(4);
    auto v = read_le32(cur);
    cur += 4;
    return v;
  }

  std::uint64_t read_u64() {
    require(8);
    auto v = read_le64(cur);
    cur += 8;
    return v;
  }

  void skip(std::size_t size) {
    require(size);
    cur += size;
  }

  std::string read_string(std::size_t size) {
    require(size);
    std::string out(reinterpret_cast<const char*>(cur), size);
    cur += size;
    return out;
  }

  std::string read_null_terminated_string() {
    auto pos = std::find(cur, last, static_cast<byte>(0));
    if (pos == last) {
      throw make_protocol_error("missing null terminator");
    }
    std::string out(reinterpret_cast<const char*>(cur),
                    static_cast<std::size_t>(pos - cur));
    cur = pos + 1;
    return out;
  }

  std::optional<std::string> read_lenenc_string_optional() {
    auto first_byte = read_byte();
    if (first_byte == 0xfb) {
      return std::nullopt;
    }
    std::uint64_t len = 0;
    if (first_byte < 0xfb) {
      len = first_byte;
    }
    else if (first_byte == 0xfc) {
      len = read_u16();
    }
    else if (first_byte == 0xfd) {
      len = read_u24();
    }
    else if (first_byte == 0xfe) {
      len = read_u64();
    }
    else {
      throw make_protocol_error("invalid lenenc string");
    }
    if (len > remaining()) {
      throw make_protocol_error("lenenc string overflow");
    }
    return read_string(static_cast<std::size_t>(len));
  }

  std::string read_lenenc_string() {
    auto value = read_lenenc_string_optional();
    return value ? std::move(*value) : std::string{};
  }

  std::uint64_t read_lenenc_int() {
    auto first_byte = read_byte();
    if (first_byte < 0xfb) {
      return first_byte;
    }
    if (first_byte == 0xfc) {
      return read_u16();
    }
    if (first_byte == 0xfd) {
      return read_u24();
    }
    if (first_byte == 0xfe) {
      return read_u64();
    }
    throw make_protocol_error("invalid lenenc integer");
  }
};

inline void append_lenenc_int(bytes& out, std::uint64_t value) {
  if (value < 0xfb) {
    out.push_back(static_cast<byte>(value));
  }
  else if (value <= 0xffff) {
    out.push_back(0xfc);
    append_le16(out, static_cast<std::uint16_t>(value));
  }
  else if (value <= 0xffffff) {
    out.push_back(0xfd);
    append_le24(out, static_cast<std::uint32_t>(value));
  }
  else {
    out.push_back(0xfe);
    append_le64(out, value);
  }
}

inline void append_lenenc_string(bytes& out, std::string_view value) {
  append_lenenc_int(out, value.size());
  out.insert(out.end(), value.begin(), value.end());
}

inline bool is_err_packet(const bytes& payload) {
  return !payload.empty() && payload[0] == 0xff;
}

inline bool is_ok_packet(const bytes& payload) {
  return !payload.empty() && payload[0] == 0x00;
}

inline bool is_auth_switch_request(const bytes& payload) {
  return !payload.empty() && payload[0] == 0xfe && payload.size() > 1;
}

inline bool is_auth_more_data(const bytes& payload) {
  return !payload.empty() && payload[0] == 0x01;
}

inline bool looks_like_eof_packet(const bytes& payload) {
  return payload.size() < 9 && !payload.empty() && payload[0] == 0xfe;
}

inline error_packet parse_error_packet(const bytes& payload) {
  packet_reader rd(payload);
  if (rd.read_byte() != 0xff) {
    throw make_protocol_error("not an error packet");
  }
  error_packet err;
  err.code = rd.read_u16();
  if (rd.remaining() >= 1 && *rd.cur == '#') {
    rd.skip(1);
    err.sql_state = rd.read_string(5);
  }
  err.message = rd.read_string(rd.remaining());
  return err;
}

inline ok_packet parse_ok_packet(const bytes& payload) {
  packet_reader rd(payload);
  auto header = rd.read_byte();
  if (header != 0x00 && header != 0xfe) {
    throw make_protocol_error("not an ok packet");
  }

  ok_packet ok;
  ok.affected_rows = rd.read_lenenc_int();
  ok.last_insert_id = rd.read_lenenc_int();
  if (rd.remaining() >= 4) {
    ok.status_flags = rd.read_u16();
    ok.warnings = rd.read_u16();
  }
  if (!rd.empty()) {
    ok.info = rd.read_string(rd.remaining());
  }
  return ok;
}

inline server_handshake parse_server_handshake(const bytes& payload) {
  packet_reader rd(payload);
  server_handshake hs;
  hs.protocol_version = rd.read_byte();
  hs.server_version = rd.read_null_terminated_string();
  hs.connection_id = rd.read_u32();
  auto scramble1 = rd.read_string(8);
  rd.skip(1);

  std::uint32_t capabilities = rd.read_u16();
  if (rd.empty()) {
    throw make_protocol_error("old mysql protocol is not supported");
  }

  hs.character_set = rd.read_byte();
  hs.status_flags = rd.read_u16();
  capabilities |= static_cast<std::uint32_t>(rd.read_u16()) << 16;

  byte auth_plugin_len = 0;
  if (capabilities & client_plugin_auth) {
    auth_plugin_len = rd.read_byte();
  }
  else {
    rd.skip(1);
  }

  rd.skip(10);

  std::string scramble = scramble1;
  if (capabilities & client_secure_connection) {
    auto extra_len = std::max<int>(13, static_cast<int>(auth_plugin_len) - 8);
    auto part2 = rd.read_string(static_cast<std::size_t>(extra_len));
    if (!part2.empty() && part2.back() == '\0') {
      part2.pop_back();
    }
    scramble += part2;
  }

  while (scramble.size() < hs.scramble.size()) {
    scramble.push_back('\0');
  }
  std::memcpy(hs.scramble.data(), scramble.data(), hs.scramble.size());

  if ((capabilities & client_plugin_auth) && !rd.empty()) {
    hs.auth_plugin_name = rd.read_null_terminated_string();
  }
  else {
    hs.auth_plugin_name = std::string(mysql_native_password);
  }

  hs.capabilities = capabilities;
  return hs;
}

inline bytes scramble_mysql_native_password(
    std::string_view password,
    std::span<const byte, 20> scramble) {
  if (password.empty()) {
    return {};
  }

  std::array<byte, SHA_DIGEST_LENGTH> stage1{};
  std::array<byte, SHA_DIGEST_LENGTH> stage2{};
  std::array<byte, 20 + SHA_DIGEST_LENGTH> stage3_input{};
  std::array<byte, SHA_DIGEST_LENGTH> stage3{};

  SHA1(reinterpret_cast<const unsigned char*>(password.data()), password.size(),
       stage1.data());
  SHA1(stage1.data(), stage1.size(), stage2.data());

  std::memcpy(stage3_input.data(), scramble.data(), 20);
  std::memcpy(stage3_input.data() + 20, stage2.data(), stage2.size());
  SHA1(stage3_input.data(), stage3_input.size(), stage3.data());

  bytes out(stage1.size());
  for (std::size_t i = 0; i < stage1.size(); ++i) {
    out[i] = static_cast<byte>(stage1[i] ^ stage3[i]);
  }
  return out;
}

inline bytes scramble_caching_sha2_password(
    std::string_view password,
    std::span<const byte, 20> scramble) {
  if (password.empty()) {
    return {};
  }

  std::array<byte, SHA256_DIGEST_LENGTH> stage1{};
  std::array<byte, SHA256_DIGEST_LENGTH> stage2{};
  std::array<byte, SHA256_DIGEST_LENGTH + 20> stage3_input{};
  std::array<byte, SHA256_DIGEST_LENGTH> stage3{};

  SHA256(reinterpret_cast<const unsigned char*>(password.data()), password.size(),
         stage1.data());
  SHA256(stage1.data(), stage1.size(), stage2.data());

  std::memcpy(stage3_input.data(), stage2.data(), stage2.size());
  std::memcpy(stage3_input.data() + stage2.size(), scramble.data(), 20);
  SHA256(stage3_input.data(), stage3_input.size(), stage3.data());

  bytes out(stage1.size());
  for (std::size_t i = 0; i < stage1.size(); ++i) {
    out[i] = static_cast<byte>(stage1[i] ^ stage3[i]);
  }
  return out;
}

inline bytes salt_caching_sha2_password(std::string_view password,
                                        std::span<const byte, 20> scramble) {
  bytes result(password.size() + 1, 0);
  for (std::size_t i = 0; i < password.size(); ++i) {
    result[i] = static_cast<byte>(password[i]) ^ scramble[i % scramble.size()];
  }
  result[password.size()] = scramble[password.size() % scramble.size()];
  return result;
}

inline bytes rsa_encrypt_password(std::string_view password,
                                  std::span<const byte, 20> scramble,
                                  std::span<const byte> public_key) {
  struct bio_deleter {
    void operator()(BIO* bio) const noexcept { BIO_free(bio); }
  };
  struct evp_pkey_deleter {
    void operator()(EVP_PKEY* key) const noexcept { EVP_PKEY_free(key); }
  };
  struct evp_pkey_ctx_deleter {
    void operator()(EVP_PKEY_CTX* ctx) const noexcept { EVP_PKEY_CTX_free(ctx); }
  };

  std::unique_ptr<BIO, bio_deleter> bio(
      BIO_new_mem_buf(public_key.data(), static_cast<int>(public_key.size())));
  if (!bio) {
    throw std::runtime_error("mysql_async openssl: BIO_new_mem_buf failed");
  }

  std::unique_ptr<EVP_PKEY, evp_pkey_deleter> key(
      PEM_read_bio_PUBKEY(bio.get(), nullptr, nullptr, nullptr));
  if (!key) {
    throw std::runtime_error("mysql_async openssl: PEM_read_bio_PUBKEY failed");
  }

  auto salted = salt_caching_sha2_password(password, scramble);

  std::unique_ptr<EVP_PKEY_CTX, evp_pkey_ctx_deleter> ctx(
      EVP_PKEY_CTX_new(key.get(), nullptr));
  if (!ctx) {
    throw std::runtime_error("mysql_async openssl: EVP_PKEY_CTX_new failed");
  }
  if (EVP_PKEY_encrypt_init(ctx.get()) <= 0) {
    throw std::runtime_error(
        "mysql_async openssl: EVP_PKEY_encrypt_init failed");
  }
  if (EVP_PKEY_CTX_set_rsa_padding(ctx.get(), RSA_PKCS1_OAEP_PADDING) <= 0) {
    throw std::runtime_error(
        "mysql_async openssl: EVP_PKEY_CTX_set_rsa_padding failed");
  }

  auto key_size = EVP_PKEY_size(key.get());
  if (key_size <= 0) {
    throw std::runtime_error("mysql_async openssl: EVP_PKEY_size failed");
  }

  bytes encrypted(static_cast<std::size_t>(key_size));
  std::size_t out_len = encrypted.size();
  if (EVP_PKEY_encrypt(ctx.get(), encrypted.data(), &out_len, salted.data(),
                       salted.size()) <= 0) {
    throw std::runtime_error("mysql_async openssl: EVP_PKEY_encrypt failed");
  }
  encrypted.resize(out_len);
  return encrypted;
}

inline std::string escape_identifier(std::string_view input) {
  std::string out;
  out.reserve(input.size() + 2);
  out.push_back('`');
  for (char ch : input) {
    if (ch == '`') {
      out.push_back('`');
      out.push_back('`');
    }
    else {
      out.push_back(ch);
    }
  }
  out.push_back('`');
  return out;
}

inline std::string escape_mysql_string(std::string_view input,
                                       bool no_backslash_escapes) {
  std::string result;
  result.reserve(input.size() * 2 + 2);
  result.push_back('\'');
  for (unsigned char ch : input) {
    if (no_backslash_escapes) {
      if (ch == '\'') {
        result.push_back('\'');
        result.push_back('\'');
      }
      else {
        result.push_back(static_cast<char>(ch));
      }
      continue;
    }

    switch (ch) {
      case 0:
        result += "\\0";
        break;
      case '\n':
        result += "\\n";
        break;
      case '\r':
        result += "\\r";
        break;
      case '\\':
        result += "\\\\";
        break;
      case '\'':
        result += "\\'";
        break;
      case '"':
        result += "\\\"";
        break;
      case '\032':
        result += "\\Z";
        break;
      default:
        result.push_back(static_cast<char>(ch));
        break;
    }
  }
  result.push_back('\'');
  return result;
}

inline thread_local std::deque<std::string> string_view_storage;

inline void clear_string_view_storage() { string_view_storage.clear(); }

inline std::string_view store_string_view(std::string_view value) {
  string_view_storage.emplace_back(value);
  return string_view_storage.back();
}

template <typename T>
inline std::string to_query_arg_impl(const T& value, bool no_backslash_escapes);

template <typename T>
inline std::string arithmetic_to_string(T value) {
  if constexpr (std::is_same_v<T, bool>) {
    return value ? "1" : "0";
  }
  else if constexpr (std::is_integral_v<T>) {
    char buffer[64];
    auto [ptr, ec] = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (ec != std::errc{}) {
      throw std::runtime_error("mysql_async: integer to_chars failed");
    }
    return std::string(buffer, ptr);
  }
  else {
    return std::to_string(value);
  }
}

template <typename T>
inline std::string blob_to_hex(const T& value) {
  static constexpr char hex[] = "0123456789ABCDEF";
  std::string out = "0x";
  out.reserve(2 + value.size() * 2);
  for (unsigned char c : value) {
    out.push_back(hex[(c >> 4) & 0x0f]);
    out.push_back(hex[c & 0x0f]);
  }
  return out;
}

template <typename T>
inline std::string to_query_arg_impl(const T& value, bool no_backslash_escapes) {
  using U = std::decay_t<T>;
  if constexpr (is_optional_v<U>::value) {
    if (!value.has_value()) {
      return "NULL";
    }
    return to_query_arg_impl(*value, no_backslash_escapes);
  }
  else if constexpr (std::is_enum_v<U>) {
    using underlying = std::underlying_type_t<U>;
    return arithmetic_to_string(static_cast<underlying>(value));
  }
  else if constexpr (std::is_arithmetic_v<U>) {
    return arithmetic_to_string(value);
  }
  else if constexpr (std::is_same_v<U, std::string> ||
                     std::is_same_v<U, std::string_view>) {
    return escape_mysql_string(value, no_backslash_escapes);
  }
  else if constexpr (iguana::array_v<U>) {
    return escape_mysql_string(
        std::string_view(value.data(),
                         std::find(value.data(), value.data() + value.size(), '\0') -
                             value.data()),
        no_backslash_escapes);
  }
  else if constexpr (iguana::c_array_v<U>) {
    return escape_mysql_string(
        std::string_view(value, std::find(value, value + sizeof(U), '\0') - value),
        no_backslash_escapes);
  }
  else if constexpr (std::is_same_v<U, const char*> ||
                     std::is_same_v<U, char*>) {
    return escape_mysql_string(value ? std::string_view(value) : std::string_view{},
                               no_backslash_escapes);
  }
  else if constexpr (std::is_same_v<U, blob>) {
    return blob_to_hex(value);
  }
#ifdef ORMPP_WITH_CSTRING
  else if constexpr (std::is_same_v<U, CString>) {
    return escape_mysql_string(
        std::string_view(value.GetString(),
                         static_cast<std::size_t>(value.GetLength())),
        no_backslash_escapes);
  }
#endif
  else {
    static_assert(!sizeof(U), "mysql_async: unsupported SQL argument type");
  }
}

inline void replace_first_placeholder(std::string& sql, std::string value) {
  auto pos = sql.find('?');
  if (pos == std::string::npos) {
    throw std::runtime_error("mysql_async: placeholder count mismatch");
  }
  sql.replace(pos, 1, value);
}

template <typename... Args>
inline std::string format_query_args(std::string sql, bool no_backslash_escapes,
                                     Args&&... args) {
  (replace_first_placeholder(
       sql, to_query_arg_impl(std::forward<Args>(args), no_backslash_escapes)),
   ...);
  if (sql.find('?') != std::string::npos) {
    throw std::runtime_error("mysql_async: placeholder count mismatch");
  }
  return sql;
}

template <typename T>
inline void assign_text_value(T& value, const std::optional<std::string>& field);

template <typename T>
inline void assign_integral(T& value, const std::string& s) {
  if constexpr (std::is_same_v<T, bool>) {
    value = !(s == "0" || s == "false" || s == "FALSE");
  }
  else if constexpr (std::is_same_v<T, char>) {
    int temp = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), temp);
    if (ec != std::errc{} || ptr != s.data() + s.size()) {
      throw std::runtime_error("mysql_async: invalid char integral");
    }
    value = static_cast<char>(temp);
  }
  else {
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc{} || ptr != s.data() + s.size()) {
      throw std::runtime_error("mysql_async: invalid integral");
    }
  }
}

template <typename T>
inline void assign_text_value(T& value, const std::optional<std::string>& field) {
  using U = std::decay_t<T>;
  if (!field.has_value()) {
    value = U{};
    return;
  }

  const auto& text = *field;
  if constexpr (is_optional_v<U>::value) {
    using value_type = typename U::value_type;
    value_type temp{};
    assign_text_value(temp, field);
    value = std::move(temp);
  }
  else if constexpr (std::is_enum_v<U>) {
    std::underlying_type_t<U> temp{};
    assign_integral(temp, text);
    value = static_cast<U>(temp);
  }
  else if constexpr (std::is_integral_v<U>) {
    assign_integral(value, text);
  }
  else if constexpr (std::is_floating_point_v<U>) {
    value = static_cast<U>(std::stod(text));
  }
  else if constexpr (std::is_same_v<U, std::string>) {
    value = text;
  }
  else if constexpr (std::is_same_v<U, std::string_view>) {
    value = store_string_view(text);
  }
  else if constexpr (iguana::array_v<U>) {
    std::memset(value.data(), 0, value.size());
    std::memcpy(value.data(), text.data(), std::min(value.size(), text.size()));
  }
  else if constexpr (iguana::c_array_v<U>) {
    std::memset(value, 0, sizeof(U));
    std::memcpy(value, text.data(), std::min(sizeof(U), text.size()));
  }
  else if constexpr (std::is_same_v<U, blob>) {
    value.assign(text.begin(), text.end());
  }
#ifdef ORMPP_WITH_CSTRING
  else if constexpr (std::is_same_v<U, CString>) {
    value = text.c_str();
  }
#endif
  else {
    static_assert(!sizeof(U), "mysql_async: unsupported result field type");
  }
}

template <typename T>
inline T map_reflectable_row(const std::vector<std::optional<std::string>>& row) {
  T value{};
  std::size_t index = 0;
  ylt::reflection::for_each(
      value, [&](auto& field, auto /*name*/, auto /*idx*/) {
        if (index >= row.size()) {
          throw std::runtime_error("mysql_async: row column count mismatch");
        }
        assign_text_value(field, row[index++]);
      });
  return value;
}

template <typename Tuple, std::size_t... I>
inline Tuple map_tuple_row_impl(
    const std::vector<std::optional<std::string>>& row,
    std::index_sequence<I...>) {
  Tuple result{};
  std::size_t col = 0;
  (
      [&] {
        using item_type = std::tuple_element_t<I, Tuple>;
        auto& item = std::get<I>(result);
        if constexpr (iguana::ylt_refletable_v<item_type>) {
          item = map_reflectable_row<item_type>(
              std::vector<std::optional<std::string>>(
                  row.begin() + static_cast<std::ptrdiff_t>(col),
                  row.begin() + static_cast<std::ptrdiff_t>(
                                   col + ylt::reflection::members_count_v<
                                             item_type>)));
          col += ylt::reflection::members_count_v<item_type>;
        }
        else {
          if (col >= row.size()) {
            throw std::runtime_error("mysql_async: tuple column count mismatch");
          }
          assign_text_value(item, row[col++]);
        }
      }(),
      ...);
  if (col != row.size()) {
    throw std::runtime_error("mysql_async: tuple column count mismatch");
  }
  return result;
}

template <typename T>
inline T map_row(const std::vector<std::optional<std::string>>& row) {
  if constexpr (iguana::ylt_refletable_v<T>) {
    return map_reflectable_row<T>(row);
  }
  else {
    static_assert(iguana::is_tuple<T>::value,
                  "mysql_async::map_row only supports reflectable or tuple");
    return map_tuple_row_impl<T>(row, std::make_index_sequence<std::tuple_size_v<T>>{});
  }
}

template <typename T, typename... Args>
std::string generate_create_table_sql(DBType db_type, bool append_mysql_charset,
                                      const std::tuple<Args...>& args) {
  std::set<std::string> not_null;
  std::set<std::string> unique;
  std::set<std::string> auto_primary_key;
  std::set<std::string> primary_keys;

  std::string_view auto_key = get_auto_key<T>();
  if (!auto_key.empty()) {
    auto_primary_key.insert(std::string(auto_key));
  }

  auto pks = get_conflict_keys<T>(db_type);
  if (!pks.empty()) {
    for (auto& key : pks) {
      primary_keys.insert(key);
    }
  }

  if constexpr (sizeof...(Args) > 0) {
    ormpp::for_each(
        args,
        [&](auto& item, auto /*index*/) {
          using U = std::decay_t<decltype(item)>;
          if constexpr (std::is_same_v<ormpp_auto_key, U>) {
            auto_primary_key.insert(item.fields);
          }
          else if constexpr (std::is_same_v<ormpp_key, U>) {
            if (pks.empty()) {
              primary_keys.insert(item.fields);
            }
          }
          else if constexpr (std::is_same_v<ormpp_not_null, U>) {
            for (auto& name : item.fields) {
              not_null.insert(name);
            }
          }
          else if constexpr (std::is_same_v<ormpp_unique, U>) {
            if (item.fields.size() > 1) {
              std::string names;
              for (auto& name : item.fields) {
                names.append(name).append(",");
              }
              names.pop_back();
              unique.insert(std::move(names));
            }
            else if (!item.fields.empty()) {
              unique.insert(*item.fields.begin());
            }
          }
        },
        std::index_sequence_for<Args...>{});
  }

  auto table_name = std::string(get_short_struct_name<T>());
  const auto type_name_arr = get_type_names<T>(db_type);

  std::string sql = "CREATE TABLE IF NOT EXISTS ";
  sql.append(table_name).append("(");

  T sample{};
  ylt::reflection::for_each(sample, [&](auto& /*field*/, auto name, size_t index) {
    sql.append(name).append(" ").append(type_name_arr[index]);
    std::string str_name(name);

    if (!auto_primary_key.empty() &&
        auto_primary_key.find(str_name) != auto_primary_key.end()) {
      sql.append(" AUTO_INCREMENT ");
      auto_key = name;
      auto_primary_key.clear();
    }
    else if (!not_null.empty() && not_null.find(str_name) != not_null.end()) {
      sql.append(" NOT NULL");
      not_null.erase(str_name);
    }
    sql.push_back(',');
  });

  if (!auto_key.empty()) {
    sql.append("PRIMARY KEY (").append(auto_key).append("),");
  }
  else if (!primary_keys.empty()) {
    sql.append("PRIMARY KEY (");
    for (const auto& key : primary_keys) {
      sql.append(key).append(",");
    }
    sql.back() = ')';
    sql.push_back(',');
  }

  for (const auto& name : unique) {
    sql.append("UNIQUE (").append(name).append("),");
  }
  sql.back() = ')';

  if (append_mysql_charset) {
    sql.append(" DEFAULT CHARSET=utf8mb4");
  }
  return sql;
}

}  // namespace detail::mysql_async

class mysql_async {
 public:
  using executor_type = asio::any_io_executor;
  template <typename T>
  using awaitable = asio::awaitable<T, executor_type>;

  static constexpr DBType db_type_v = DBType::mysql;

  explicit mysql_async(executor_type executor)
      : executor_(std::move(executor)),
        resolver_(executor_),
        timer_(executor_) {}

  explicit mysql_async(asio::io_context& ctx)
      : mysql_async(ctx.get_executor()) {}

  mysql_async(const mysql_async&) = delete;
  mysql_async& operator=(const mysql_async&) = delete;
  mysql_async(mysql_async&&) = default;
  mysql_async& operator=(mysql_async&&) = default;

  bool has_error() const { return has_error_; }

  static void reset_error() {
    has_error_ = false;
    last_error_.clear();
  }

  static void set_last_error(std::string error) {
    has_error_ = true;
    last_error_ = std::move(error);
#ifdef ORMPP_ENABLE_LOG
    std::cout << last_error_ << std::endl;
#endif
  }

  std::string get_last_error() const { return last_error_; }

  int get_last_affect_rows() const { return last_affect_rows_; }

  void set_enable_transaction(bool enable) { transaction_ = enable; }

  awaitable<bool> connect(
      const std::tuple<std::string, std::string, std::string, std::string,
                       std::optional<int>, std::optional<int>>& tp) {
    co_return co_await connect(std::get<0>(tp), std::get<1>(tp), std::get<2>(tp),
                               std::get<3>(tp), std::get<4>(tp), std::get<5>(tp));
  }

  awaitable<bool> connect(const std::string& host, const std::string& user = "",
                          const std::string& passwd = "",
                          const std::string& db = "",
                          const std::optional<int>& timeout = {},
                          const std::optional<int>& port = {}) {
    reset_error();
    last_affect_rows_ = 0;
    last_insert_id_ = 0;
    status_flags_ = 0;
    backslash_escapes_ = true;

    try {
      socket_.emplace(executor_);
      host_ = host;
      user_ = user;
      password_ = passwd;
      database_ = db;
      port_ = port.value_or(3306);
      timeout_seconds_ = timeout.value_or(0);
      sequence_id_ = 0;

      auto endpoints = co_await resolver_.async_resolve(
          host_, std::to_string(port_), asio::use_awaitable);
      co_await asio::async_connect(*socket_, endpoints, asio::use_awaitable);

      auto handshake_payload = co_await read_packet();
      auto handshake = detail::mysql_async::parse_server_handshake(handshake_payload);

      server_capabilities_ = handshake.capabilities;
      auth_plugin_name_ = handshake.auth_plugin_name.empty()
                              ? std::string(detail::mysql_async::mysql_native_password)
                              : handshake.auth_plugin_name;
      scramble_ = handshake.scramble;

      std::uint32_t client_caps =
          detail::mysql_async::client_long_password |
          detail::mysql_async::client_found_rows |
          detail::mysql_async::client_long_flag |
          detail::mysql_async::client_protocol_41 |
          detail::mysql_async::client_transactions |
          detail::mysql_async::client_secure_connection |
          detail::mysql_async::client_multi_statements |
          detail::mysql_async::client_multi_results |
          detail::mysql_async::client_plugin_auth |
          detail::mysql_async::client_plugin_auth_lenenc_data |
          detail::mysql_async::client_connect_attrs |
          detail::mysql_async::client_deprecate_eof;
      if (!database_.empty()) {
        client_caps |= detail::mysql_async::client_connect_with_db;
      }
      negotiated_capabilities_ = client_caps & server_capabilities_;

      co_await write_handshake_response();
      auto auth_response = co_await read_packet();
      bool ok = co_await handle_auth_response(std::move(auth_response));
      if (!ok) {
        co_return false;
      }

      connected_ = true;
      co_return true;
    }
    catch (const std::exception& e) {
      set_last_error(e.what());
      if (socket_ && socket_->is_open()) {
        std::error_code ec;
        socket_->close(ec);
      }
      socket_.reset();
      connected_ = false;
      co_return false;
    }
  }

  awaitable<bool> disconnect() {
    if (socket_ && socket_->is_open()) {
      std::error_code ec;
      socket_->shutdown(asio::ip::tcp::socket::shutdown_both, ec);
      socket_->close(ec);
    }
    socket_.reset();
    connected_ = false;
    co_return true;
  }

  awaitable<bool> ping() {
    try {
      auto result = co_await command_simple(0x0e, {});
      co_return !result.has_resultset;
    }
    catch (const std::exception& e) {
      set_last_error(e.what());
      co_return false;
    }
  }

  template <typename T, typename... Args>
  awaitable<bool> create_datatable(Args&&... args) {
    try {
      auto sql = detail::mysql_async::generate_create_table_sql<T>(
          db_type_v, true, std::make_tuple(std::forward<Args>(args)...));
#ifdef ORMPP_ENABLE_LOG
      std::cout << sql << std::endl;
#endif
      co_return co_await execute(sql);
    }
    catch (const std::exception& e) {
      set_last_error(e.what());
      co_return false;
    }
  }

  template <typename T, typename... Args>
  awaitable<int> insert(const T& t, Args&&... /*args*/) {
    co_return co_await insert_impl(OptType::insert, t);
  }

  template <typename T, typename... Args>
  awaitable<int> insert(const std::vector<T>& v, Args&&... /*args*/) {
    co_return co_await insert_impl(OptType::insert, v);
  }

  template <typename T, typename... Args>
  awaitable<int> replace(const T& t, Args&&... /*args*/) {
    co_return co_await insert_impl(OptType::replace, t);
  }

  template <typename T, typename... Args>
  awaitable<int> replace(const std::vector<T>& v, Args&&... /*args*/) {
    co_return co_await insert_impl(OptType::replace, v);
  }

  template <auto... members, typename T, typename... Args>
  awaitable<int> update(const T& t, Args&&... args) {
    try {
      auto sql = generate_update_sql<T, members...>(db_type_v,
                                                    std::forward<Args>(args)...);
      auto formatted = format_struct_sql<t_is_vector_false>(sql, t, OptType::update,
                                                            std::forward<Args>(args)...);
      auto ok = co_await execute(formatted);
      co_return ok ? last_affect_rows_ : std::numeric_limits<int>::min();
    }
    catch (const std::exception& e) {
      set_last_error(e.what());
      co_return std::numeric_limits<int>::min();
    }
  }

  template <auto... members, typename T, typename... Args>
  awaitable<int> update(const std::vector<T>& v, Args&&... args) {
    try {
      auto sql = generate_update_sql<T, members...>(db_type_v,
                                                    std::forward<Args>(args)...);
      int affected = 0;
      if (transaction_ && !v.empty()) {
        if (!(co_await begin())) {
          co_return std::numeric_limits<int>::min();
        }
      }

      for (const auto& item : v) {
        auto formatted =
            format_struct_sql<t_is_vector_false>(sql, item, OptType::update,
                                                 std::forward<Args>(args)...);
        if (!(co_await execute(formatted))) {
          if (transaction_ && !v.empty()) {
            co_await rollback();
          }
          co_return std::numeric_limits<int>::min();
        }
        affected += last_affect_rows_;
      }

      if (transaction_ && !v.empty()) {
        if (!(co_await commit())) {
          co_return std::numeric_limits<int>::min();
        }
      }
      co_return affected;
    }
    catch (const std::exception& e) {
      set_last_error(e.what());
      co_return std::numeric_limits<int>::min();
    }
  }

  template <typename T, typename... Args>
  awaitable<std::uint64_t> get_insert_id_after_insert(const T& t,
                                                      Args&&... /*args*/) {
    auto count = co_await insert_impl(OptType::insert, t);
    co_return count == std::numeric_limits<int>::min() ? 0 : last_insert_id_;
  }

  template <typename T, typename... Args>
  awaitable<std::uint64_t> get_insert_id_after_insert(const std::vector<T>& v,
                                                      Args&&... /*args*/) {
    auto count = co_await insert_impl(OptType::insert, v);
    co_return count == std::numeric_limits<int>::min() ? 0 : last_insert_id_;
  }

  template <typename T, typename... Args>
  awaitable<std::uint64_t> delete_records_s(const std::string& str = "",
                                            Args&&... args) {
    try {
      auto sql = generate_delete_sql<T>(db_type_v, str);
      auto params = std::make_tuple(std::forward<Args>(args)...);
      if constexpr (sizeof...(Args) > 0) {
        sql = std::apply(
            [&](auto&&... unpacked) {
              return detail::mysql_async::format_query_args(
                  std::move(sql), !backslash_escapes_,
                  std::forward<decltype(unpacked)>(unpacked)...);
            },
            params);
      }
#ifdef ORMPP_ENABLE_LOG
      std::cout << sql << std::endl;
#endif
      co_return (co_await execute(sql)) ? static_cast<std::uint64_t>(last_affect_rows_)
                                        : 0;
    }
    catch (const std::exception& e) {
      set_last_error(e.what());
      co_return 0;
    }
  }

  template <typename T, typename... Args>
  awaitable<std::enable_if_t<iguana::ylt_refletable_v<T>, std::vector<T>>> query_s(
      const std::string& str = "", Args&&... args) {
    try {
      std::string sql =
          (contains_select(str) ? str : generate_query_sql<T>(db_type_v, str));
      auto params = std::make_tuple(std::forward<Args>(args)...);
      if constexpr (sizeof...(Args) > 0) {
        sql = std::apply(
            [&](auto&&... unpacked) {
              return detail::mysql_async::format_query_args(
                  std::move(sql), !backslash_escapes_,
                  std::forward<decltype(unpacked)>(unpacked)...);
            },
            params);
      }
#ifdef ORMPP_ENABLE_LOG
      std::cout << sql << std::endl;
#endif
      auto result = co_await query_text(sql);
      detail::mysql_async::clear_string_view_storage();
      std::vector<T> rows;
      rows.reserve(result.rows.size());
      for (const auto& row : result.rows) {
        rows.push_back(detail::mysql_async::map_row<T>(row));
      }
      co_return rows;
    }
    catch (const std::exception& e) {
      set_last_error(e.what());
      co_return std::vector<T>{};
    }
  }

  template <typename T, typename... Args>
  awaitable<std::enable_if_t<iguana::non_ylt_refletable_v<T>, std::vector<T>>>
  query_s(const std::string& str = "", Args&&... args) {
    static_assert(iguana::is_tuple<T>::value);
    try {
      std::string sql = str;
      auto params = std::make_tuple(std::forward<Args>(args)...);
      if constexpr (sizeof...(Args) > 0) {
        sql = std::apply(
            [&](auto&&... unpacked) {
              return detail::mysql_async::format_query_args(
                  std::move(sql), !backslash_escapes_,
                  std::forward<decltype(unpacked)>(unpacked)...);
            },
            params);
      }
#ifdef ORMPP_ENABLE_LOG
      std::cout << sql << std::endl;
#endif
      auto result = co_await query_text(sql);
      detail::mysql_async::clear_string_view_storage();
      std::vector<T> rows;
      rows.reserve(result.rows.size());
      for (const auto& row : result.rows) {
        rows.push_back(detail::mysql_async::map_row<T>(row));
      }
      co_return rows;
    }
    catch (const std::exception& e) {
      set_last_error(e.what());
      co_return std::vector<T>{};
    }
  }

  template <typename T, typename... Args>
  awaitable<bool> delete_records(Args&&... where_condition) {
    auto sql = generate_delete_sql<T>(db_type_v,
                                      std::forward<Args>(where_condition)...);
    co_return co_await execute(sql);
  }

  template <typename T, typename... Args>
  awaitable<std::vector<T>> query(Args&&... args) {
    static_assert(sizeof...(Args) > 0);
    auto sql = generate_query_sql<T>(db_type_v, std::forward<Args>(args)...);
    co_return co_await query_s<T>(sql);
  }

  template <typename... Args>
  auto select(Args... args) {
    return ormpp::select(this, args...);
  }

  auto select(all_t) { return ormpp::select_all(this); }

  auto select_all() { return ormpp::select_all(this); }

  template <typename T>
  auto make_update() {
    return ormpp::make_update_builder<T>(this);
  }

  template <typename T>
  auto make_delete() {
    return ormpp::make_delete_builder<T>(this);
  }

  template <typename T>
  auto make_create_table() {
    return ormpp::make_create_table_builder<T>(this);
  }

  template <typename T>
  auto make_alter_table() {
    return ormpp::make_alter_table_builder<T>(this);
  }

  awaitable<bool> execute(const std::string& sql) {
    try {
#ifdef ORMPP_ENABLE_LOG
      std::cout << sql << std::endl;
#endif
      auto result = co_await query_text(sql);
      if (result.has_resultset) {
        last_affect_rows_ = 0;
      }
      co_return true;
    }
    catch (const std::exception& e) {
      set_last_error(e.what());
      co_return false;
    }
  }

  awaitable<bool> begin() { co_return co_await execute("BEGIN"); }
  awaitable<bool> commit() { co_return co_await execute("COMMIT"); }
  awaitable<bool> rollback() { co_return co_await execute("ROLLBACK"); }

 private:
  template <typename CompletionToken>
  auto async_wait_for_timeout(CompletionToken&& token) {
    if (timeout_seconds_ <= 0) {
      timer_.expires_at(asio::steady_timer::time_point::max());
    }
    else {
      timer_.expires_after(std::chrono::seconds(timeout_seconds_));
    }
    return timer_.async_wait(std::forward<CompletionToken>(token));
  }

  awaitable<detail::mysql_async::bytes> read_packet() {
    if (!socket_ || !socket_->is_open()) {
      throw std::runtime_error("mysql_async: socket is not connected");
    }

    detail::mysql_async::bytes payload;
    for (;;) {
      std::array<detail::mysql_async::byte, 4> header_buf{};
      co_await asio::async_read(*socket_, asio::buffer(header_buf),
                                asio::use_awaitable);

      detail::mysql_async::packet_header header;
      header.payload_size = detail::mysql_async::read_le24(header_buf.data());
      header.sequence_id = header_buf[3];
      sequence_id_ = static_cast<detail::mysql_async::byte>(header.sequence_id + 1);

      detail::mysql_async::bytes chunk(header.payload_size);
      if (header.payload_size > 0) {
        co_await asio::async_read(*socket_, asio::buffer(chunk),
                                  asio::use_awaitable);
      }
      payload.insert(payload.end(), chunk.begin(), chunk.end());

      if (header.payload_size != detail::mysql_async::max_packet_chunk) {
        break;
      }
    }
    co_return payload;
  }

  awaitable<void> write_packet(detail::mysql_async::bytes payload) {
    if (!socket_ || !socket_->is_open()) {
      throw std::runtime_error("mysql_async: socket is not connected");
    }

    std::size_t offset = 0;
    do {
      auto remaining = payload.size() - offset;
      auto chunk_size = std::min<std::size_t>(remaining,
                                              detail::mysql_async::max_packet_chunk);

      detail::mysql_async::bytes frame;
      frame.reserve(chunk_size + 4);
      detail::mysql_async::append_le24(frame,
                                       static_cast<std::uint32_t>(chunk_size));
      frame.push_back(sequence_id_++);
      frame.insert(frame.end(), payload.begin() + static_cast<std::ptrdiff_t>(offset),
                   payload.begin() + static_cast<std::ptrdiff_t>(offset + chunk_size));
      co_await asio::async_write(*socket_, asio::buffer(frame),
                                 asio::use_awaitable);

      offset += chunk_size;
      if (chunk_size == detail::mysql_async::max_packet_chunk &&
          offset == payload.size()) {
        detail::mysql_async::bytes empty_frame = {0, 0, 0, sequence_id_++};
        co_await asio::async_write(*socket_, asio::buffer(empty_frame),
                                   asio::use_awaitable);
      }
    } while (offset < payload.size());
  }

  awaitable<void> write_handshake_response() {
    detail::mysql_async::bytes response;
    detail::mysql_async::append_le32(response, negotiated_capabilities_);
    detail::mysql_async::append_le32(response, max_packet_size_);
    response.push_back(collation_id_);
    response.insert(response.end(), 23, 0);

    response.insert(response.end(), user_.begin(), user_.end());
    response.push_back(0);

    auto auth_data = build_auth_response(auth_plugin_name_);
    detail::mysql_async::append_lenenc_int(response, auth_data.size());
    response.insert(response.end(), auth_data.begin(), auth_data.end());

    if (negotiated_capabilities_ & detail::mysql_async::client_connect_with_db) {
      response.insert(response.end(), database_.begin(), database_.end());
      response.push_back(0);
    }

    response.insert(response.end(), auth_plugin_name_.begin(), auth_plugin_name_.end());
    response.push_back(0);

    detail::mysql_async::bytes attrs;
    append_connect_attr(attrs, "_client_name", "ormpp");
    append_connect_attr(attrs, "_client_version", "async");
    append_connect_attr(attrs, "_os", "linux");
    detail::mysql_async::append_lenenc_int(response, attrs.size());
    response.insert(response.end(), attrs.begin(), attrs.end());

    sequence_id_ = 1;
    co_await write_packet(std::move(response));
  }

  awaitable<bool> handle_auth_response(detail::mysql_async::bytes payload) {
    for (;;) {
      if (detail::mysql_async::is_err_packet(payload)) {
        auto err = detail::mysql_async::parse_error_packet(payload);
        throw std::runtime_error("mysql_async auth failed [" +
                                 std::to_string(err.code) + "] " + err.message);
      }

      if (detail::mysql_async::is_ok_packet(payload) &&
          !detail::mysql_async::is_auth_switch_request(payload)) {
        apply_ok(detail::mysql_async::parse_ok_packet(payload));
        co_return true;
      }

      if (detail::mysql_async::is_auth_switch_request(payload)) {
        detail::mysql_async::packet_reader rd(payload);
        rd.read_byte();
        auto plugin_name = rd.read_null_terminated_string();
        auto plugin_data = rd.read_string(rd.remaining());
        if (!plugin_data.empty() && plugin_data.back() == '\0') {
          plugin_data.pop_back();
        }
        if (plugin_data.size() < scramble_.size()) {
          plugin_data.resize(scramble_.size(), 0);
        }
        std::memcpy(scramble_.data(), plugin_data.data(), scramble_.size());
        auth_plugin_name_ = plugin_name;

        co_await write_packet(build_auth_response(auth_plugin_name_));
        payload = co_await read_packet();
        continue;
      }

      if (detail::mysql_async::is_auth_more_data(payload)) {
        detail::mysql_async::packet_reader rd(payload);
        rd.read_byte();
        auto data = rd.read_string(rd.remaining());
        if (auth_plugin_name_ == detail::mysql_async::caching_sha2_password) {
          auto fast_auth_code = data.empty() ? 0 : static_cast<unsigned char>(data[0]);
          if (data.size() == 1 && fast_auth_code == 3) {
            payload = co_await read_packet();
            continue;
          }
          if (data.size() == 1 && fast_auth_code == 4) {
            if (is_secure_channel()) {
              detail::mysql_async::bytes plain(password_.begin(), password_.end());
              plain.push_back(0);
              co_await write_packet(std::move(plain));
            }
            else {
              co_await write_packet({2});
              auto public_key_packet = co_await read_packet();
              if (detail::mysql_async::is_err_packet(public_key_packet)) {
                auto err = detail::mysql_async::parse_error_packet(public_key_packet);
                throw std::runtime_error("mysql_async auth public key failed [" +
                                         std::to_string(err.code) + "] " +
                                         err.message);
              }
              auto encrypted = detail::mysql_async::rsa_encrypt_password(
                  password_, scramble_,
                  std::span<const detail::mysql_async::byte>(
                      public_key_packet.data(), public_key_packet.size()));
              co_await write_packet(std::move(encrypted));
            }
            payload = co_await read_packet();
            continue;
          }
        }
        throw std::runtime_error("mysql_async: unsupported auth more data");
      }

      throw std::runtime_error("mysql_async: unsupported auth response packet");
    }
  }

  detail::mysql_async::bytes build_auth_response(std::string_view plugin) const {
    if (plugin == detail::mysql_async::mysql_native_password) {
      return detail::mysql_async::scramble_mysql_native_password(password_,
                                                                 scramble_);
    }
    if (plugin == detail::mysql_async::caching_sha2_password) {
      return detail::mysql_async::scramble_caching_sha2_password(password_,
                                                                 scramble_);
    }
    throw std::runtime_error("mysql_async: unsupported auth plugin " +
                             std::string(plugin));
  }

  bool is_secure_channel() const { return false; }

  void append_connect_attr(detail::mysql_async::bytes& out, std::string_view key,
                           std::string_view value) {
    detail::mysql_async::append_lenenc_string(out, key);
    detail::mysql_async::append_lenenc_string(out, value);
  }

  void apply_ok(const detail::mysql_async::ok_packet& ok) {
    last_affect_rows_ = static_cast<int>(ok.affected_rows);
    last_insert_id_ = ok.last_insert_id;
    status_flags_ = ok.status_flags;
    backslash_escapes_ =
        (status_flags_ & detail::mysql_async::status_no_backslash_escapes) == 0;
  }

  awaitable<detail::mysql_async::query_result> command_simple(
      detail::mysql_async::byte command, std::string_view sql) {
    detail::mysql_async::bytes request;
    request.push_back(command);
    request.insert(request.end(), sql.begin(), sql.end());
    sequence_id_ = 0;
    co_await write_packet(std::move(request));
    co_return co_await read_query_response();
  }

  awaitable<detail::mysql_async::query_result> query_text(const std::string& sql) {
    co_return co_await command_simple(0x03, sql);
  }

  awaitable<detail::mysql_async::query_result> read_query_response() {
    auto payload = co_await read_packet();
    if (detail::mysql_async::is_err_packet(payload)) {
      auto err = detail::mysql_async::parse_error_packet(payload);
      throw std::runtime_error("mysql_async query failed [" +
                               std::to_string(err.code) + "] " + err.message);
    }

    detail::mysql_async::query_result result;
    if (detail::mysql_async::is_ok_packet(payload)) {
      result.ok = detail::mysql_async::parse_ok_packet(payload);
      apply_ok(result.ok);
      co_return result;
    }

    detail::mysql_async::packet_reader rd(payload);
    auto column_count = rd.read_lenenc_int();
    result.has_resultset = true;
    result.columns.reserve(static_cast<std::size_t>(column_count));

    for (std::uint64_t i = 0; i < column_count; ++i) {
      result.columns.push_back(parse_column_definition(co_await read_packet()));
    }

    for (;;) {
      auto row_payload = co_await read_packet();
      if (detail::mysql_async::is_err_packet(row_payload)) {
        auto err = detail::mysql_async::parse_error_packet(row_payload);
        throw std::runtime_error("mysql_async row fetch failed [" +
                                 std::to_string(err.code) + "] " + err.message);
      }

      if (detail::mysql_async::looks_like_eof_packet(row_payload)) {
        apply_ok(detail::mysql_async::parse_ok_packet(row_payload));
        break;
      }

      result.rows.push_back(parse_text_row(row_payload, result.columns.size()));
    }

    last_affect_rows_ = static_cast<int>(result.rows.size());
    co_return result;
  }

  detail::mysql_async::column_definition parse_column_definition(
      const detail::mysql_async::bytes& payload) {
    detail::mysql_async::packet_reader rd(payload);
    detail::mysql_async::column_definition col;
    col.schema = rd.read_lenenc_string();
    col.table = rd.read_lenenc_string();
    col.org_table = rd.read_lenenc_string();
    col.name = rd.read_lenenc_string();
    col.org_name = rd.read_lenenc_string();
    auto fixed_len = rd.read_lenenc_int();
    (void)fixed_len;
    col.character_set = rd.read_u16();
    col.column_length = rd.read_u32();
    col.type = rd.read_byte();
    col.flags = rd.read_u16();
    col.decimals = rd.read_byte();
    if (rd.remaining() >= 2) {
      rd.skip(2);
    }
    return col;
  }

  std::vector<std::optional<std::string>> parse_text_row(
      const detail::mysql_async::bytes& payload, std::size_t columns) {
    detail::mysql_async::packet_reader rd(payload);
    std::vector<std::optional<std::string>> row;
    row.reserve(columns);
    for (std::size_t i = 0; i < columns; ++i) {
      row.push_back(rd.read_lenenc_string_optional());
    }
    return row;
  }

  template <typename T>
  awaitable<int> insert_impl(OptType type, const T& item) {
    try {
      auto sql = generate_insert_sql<T>(db_type_v, type == OptType::insert);
      auto formatted = format_struct_sql<t_is_vector_false>(sql, item, type);
      auto ok = co_await execute(formatted);
      co_return ok ? last_affect_rows_ : std::numeric_limits<int>::min();
    }
    catch (const std::exception& e) {
      set_last_error(e.what());
      co_return std::numeric_limits<int>::min();
    }
  }

  template <typename T>
  awaitable<int> insert_impl(OptType type, const std::vector<T>& items) {
    try {
      auto sql = generate_insert_sql<T>(db_type_v, type == OptType::insert);
      int affected = 0;
      if (transaction_ && !items.empty()) {
        if (!(co_await begin())) {
          co_return std::numeric_limits<int>::min();
        }
      }

      for (const auto& item : items) {
        auto formatted = format_struct_sql<t_is_vector_false>(sql, item, type);
        if (!(co_await execute(formatted))) {
          if (transaction_ && !items.empty()) {
            co_await rollback();
          }
          co_return std::numeric_limits<int>::min();
        }
        affected += last_affect_rows_;
      }

      if (transaction_ && !items.empty()) {
        if (!(co_await commit())) {
          co_return std::numeric_limits<int>::min();
        }
      }
      co_return affected;
    }
    catch (const std::exception& e) {
      set_last_error(e.what());
      co_return std::numeric_limits<int>::min();
    }
  }

  static bool contains_select(const std::string& sql) {
    std::string lower(sql.size(), '\0');
    std::transform(sql.begin(), sql.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower.find("select") != std::string::npos;
  }

  struct t_is_vector_false {};

  template <typename Tag, typename T, typename... Args>
  std::string format_struct_sql(const std::string& sql, const T& value,
                                OptType type, Args&&... args) const {
    std::vector<std::string> values;
    values.reserve(ylt::reflection::members_count_v<T> * 2 + sizeof...(Args));

    ylt::reflection::for_each(value, [&](auto& field, auto name, auto /*index*/) {
      if (type == OptType::insert && is_auto_key<T>(name)) {
        return;
      }
      values.push_back(detail::mysql_async::to_query_arg_impl(
          field, !backslash_escapes_));
    });

    if constexpr (sizeof...(Args) == 0) {
      if (type == OptType::update) {
        ylt::reflection::for_each(value, [&](auto& field, auto name, auto /*index*/) {
          std::string key = detail::mysql_async::escape_identifier(name);
          if (is_conflict_key<T>(key, db_type_v)) {
            values.push_back(detail::mysql_async::to_query_arg_impl(
                field, !backslash_escapes_));
          }
        });
      }
    }
    else {
      (values.push_back(detail::mysql_async::to_query_arg_impl(
           std::forward<Args>(args), !backslash_escapes_)),
       ...);
    }

    std::string formatted = sql;
    for (auto& item : values) {
      detail::mysql_async::replace_first_placeholder(formatted, std::move(item));
    }
    if (formatted.find('?') != std::string::npos) {
      throw std::runtime_error("mysql_async: placeholder count mismatch");
    }
    return formatted;
  }

 private:
  executor_type executor_;
  asio::ip::tcp::resolver resolver_;
  asio::steady_timer timer_;
  std::optional<asio::ip::tcp::socket> socket_;

  std::string host_;
  std::string user_;
  std::string password_;
  std::string database_;
  int port_ = 3306;
  int timeout_seconds_ = 0;
  bool connected_ = false;

  std::uint32_t server_capabilities_ = 0;
  std::uint32_t negotiated_capabilities_ = 0;
  std::uint32_t max_packet_size_ = 1024 * 1024 * 16;
  detail::mysql_async::byte collation_id_ = 45;
  detail::mysql_async::byte sequence_id_ = 0;
  std::uint16_t status_flags_ = 0;
  bool backslash_escapes_ = true;
  std::string auth_plugin_name_ = std::string(detail::mysql_async::mysql_native_password);
  std::array<detail::mysql_async::byte, 20> scramble_{};
  std::uint64_t last_insert_id_ = 0;
  int last_affect_rows_ = 0;

  inline static std::string last_error_;
  inline static bool has_error_ = false;
  inline static bool transaction_ = true;
};

template <>
struct db_execution_traits<mysql_async> {
  static constexpr bool is_async = true;

  template <typename T>
  using awaitable_type = mysql_async::awaitable<T>;
};

}  // namespace ormpp

#endif
