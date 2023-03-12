#pragma once

namespace iguana {
template <typename alloc_ty>
struct basic_string_stream {
 private:
  alloc_ty alloc;

 public:
  enum { good, read_overflow };

  char* m_header_ptr;
  char* m_read_ptr;
  char* m_write_ptr;
  char* m_tail_ptr;
  int m_status;
  std::size_t m_length;

  enum { INIT_BUFF_SIZE = 1024 };

  basic_string_stream() : m_status(good), m_length(INIT_BUFF_SIZE) {
    this->m_header_ptr = this->alloc.allocate(INIT_BUFF_SIZE);
    this->m_read_ptr = this->m_header_ptr;
    this->m_write_ptr = this->m_header_ptr;
    this->m_tail_ptr = this->m_header_ptr + m_length;
  }

  ~basic_string_stream() {
    this->alloc.deallocate(m_header_ptr, this->m_length);
  }

  inline std::size_t write(const char* buffer) {
    return write(buffer, strlen(buffer));
  }

  inline std::size_t read(const char* buffer, std::size_t len) {
    if (this->m_read_ptr + len > this->m_tail_ptr) {
      m_status = read_overflow;
      return 0;
    }
    std::memcpy(buffer, this->m_read_ptr, len);
    this->m_read_ptr += len;
    return len;
  }

  inline std::size_t growpup(std::size_t want_size) {
    std::size_t new_size =
        ((want_size + INIT_BUFF_SIZE - 1) / INIT_BUFF_SIZE) * INIT_BUFF_SIZE;
    std::size_t write_pos = this->m_write_ptr - this->m_header_ptr;
    std::size_t read_pos = this->m_read_ptr - this->m_header_ptr;
    char* temp = this->m_header_ptr;
    this->m_header_ptr = this->alloc.allocate(new_size);
    std::memcpy(this->m_header_ptr, temp, this->m_length);
    this->alloc.deallocate(temp, this->m_length);
    this->m_length = new_size;
    this->m_write_ptr = this->m_header_ptr + write_pos;
    this->m_read_ptr = this->m_header_ptr + read_pos;
    this->m_tail_ptr = this->m_header_ptr + m_length;
    return new_size;
  }

  inline std::size_t write(const char* buffer, std::size_t len) {
    std::size_t writed_len = this->m_write_ptr + len - this->m_header_ptr;
    if (writed_len > this->m_length) {
      this->growpup(writed_len);
    }
    std::memcpy((void*)this->m_write_ptr, buffer, len);
    this->m_write_ptr += len;
    return len;
  }

  inline void write_str(char const* str, size_t len) {
    put('"');
    char const* ptr = str;
    char const* end = ptr + len;
    while (ptr < end) {
      const char c = *ptr;
      if (c == 0)
        break;
      ++ptr;
      if (escape[(unsigned char)c]) {
        char buff[6] = {'\\', '0'};
        size_t len = 2;
        buff[1] = escape[(unsigned char)c];
        if (buff[1] == 'u') {
          if (ptr < end) {
            buff[2] = (hex_table[((unsigned char)c) >> 4]);
            buff[3] = (hex_table[((unsigned char)c) & 0xF]);
            const char c1 = *ptr;
            ++ptr;
            buff[4] = (hex_table[((unsigned char)c1) >> 4]);
            buff[5] = (hex_table[((unsigned char)c1) & 0xF]);
          }
          else {
            buff[2] = '0';
            buff[3] = '0';
            buff[4] = (hex_table[((unsigned char)c) >> 4]);
            buff[5] = (hex_table[((unsigned char)c) & 0xF]);
          }
          len = 6;
        }
        write(buff, len);
      }
      else {
        put(c);
      }
    }
    put('"');
  }

  inline void put(char c) {
    std::size_t writed_len = this->m_write_ptr + 1 - this->m_header_ptr;
    if (writed_len > this->m_length) {
      this->growpup(writed_len);
    }
    *this->m_write_ptr = c;
    ++this->m_write_ptr;
  }

  inline bool bad() const { return m_status != good; }

  inline void clear() {
    this->m_read_ptr = this->m_header_ptr;
    this->m_write_ptr = this->m_header_ptr;
  }

  inline const char* data() const { return this->m_header_ptr; }

  std::basic_string<char, std::char_traits<char>, alloc_ty> str() {
    std::basic_string<char, std::char_traits<char>, alloc_ty> s(
        this->m_header_ptr, this->write_length());
    return s;
  }

  inline ::std::size_t read_length() const {
    return this->m_read_ptr - this->m_header_ptr;
  }

  inline ::std::size_t write_length() const {
    return this->m_write_ptr - this->m_header_ptr;
  }

  inline static char const* hex_table = "0123456789ABCDEF";
  inline static char const escape[256] = {
#define Z16 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
      // 0    1    2    3    4    5    6    7    8    9    A    B    C    D    E
      // F
      'u', 'u', 'u',  'u', 'u', 'u', 'u', 'u', 'b', 't',
      'n', 'u', 'f',  'r', 'u', 'u',  // 00
      'u', 'u', 'u',  'u', 'u', 'u', 'u', 'u', 'u', 'u',
      'u', 'u', 'u',  'u', 'u', 'u',  // 10
      0,   0,   '"',  0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,    0,   0,   0,  // 20
      Z16, Z16,                     // 30~4F
      0,   0,   0,    0,   0,   0,   0,   0,   0,   0,
      0,   0,   '\\', 0,   0,   0,                       // 50
      Z16, Z16, Z16,  Z16, Z16, Z16, Z16, Z16, Z16, Z16  // 60~FF
#undef Z16
  };
};

typedef basic_string_stream<std::allocator<char>> string_stream;
}  // namespace iguana