#pragma once

namespace iguana
{
	template <typename alloc_ty>
	struct basic_string_stream
	{
	private:
		alloc_ty alloc;
	public:
		enum { good, read_overflow };

		char*		m_header_ptr;
		char*		m_read_ptr;
		char*		m_write_ptr;
		char*		m_tail_ptr;
		int			m_status;
		std::size_t	m_length;

		enum { INIT_BUFF_SIZE = 1024 };

		basic_string_stream() 
			: m_length(INIT_BUFF_SIZE)
			, m_status(good)
		{
			this->m_header_ptr = this->alloc.allocate(INIT_BUFF_SIZE);
			this->m_read_ptr = this->m_header_ptr;
			this->m_write_ptr = this->m_header_ptr;
			this->m_tail_ptr = this->m_header_ptr + m_length;
		}

		~basic_string_stream()
		{
			this->alloc.deallocate(m_header_ptr, this->m_length);
		}

		inline std::size_t write(const char * buffer)
		{
			return write(buffer, strlen(buffer));
		}

		inline std::size_t read(const char * buffer, std::size_t len)
		{
			if (this->m_read_ptr + len > this->m_tail_ptr)
			{
				m_status = read_overflow;
				return 0;
			}
			std::memcpy(buffer, this->m_read_ptr, len);
			this->m_read_ptr += len;
			return len;
		}

		inline std::size_t growpup(std::size_t want_size)
		{
			std::size_t new_size = ((want_size + INIT_BUFF_SIZE - 1) / INIT_BUFF_SIZE)*INIT_BUFF_SIZE;
			std::size_t write_pos = this->m_write_ptr - this->m_header_ptr;
			std::size_t read_pos = this->m_read_ptr - this->m_header_ptr;
			char * temp = this->m_header_ptr;
			this->m_header_ptr = this->alloc.allocate(new_size);
			std::memcpy(this->m_header_ptr, temp, this->m_length);
			this->alloc.deallocate(temp, this->m_length);
			this->m_length = new_size;
			this->m_write_ptr = this->m_header_ptr + write_pos;
			this->m_read_ptr = this->m_header_ptr + read_pos;
			this->m_tail_ptr = this->m_header_ptr + m_length;
			return new_size;
		}

		inline std::size_t write(const char * buffer, std::size_t len)
		{
			std::size_t writed_len = this->m_write_ptr + len - this->m_header_ptr;
			if (writed_len > this->m_length)
			{
				this->growpup(writed_len);
			}
			std::memcpy((void*)this->m_write_ptr, buffer, len);
			this->m_write_ptr += len;
			return len;
		}

		inline void put(char c)
		{
			std::size_t writed_len = this->m_write_ptr + 1 - this->m_header_ptr;
			if (writed_len > this->m_length)
			{
				this->growpup(writed_len);
			}
			*this->m_write_ptr = c;
			++this->m_write_ptr;
		}

		inline bool bad()const { return m_status != good; }

		inline void clear()
		{
			this->m_read_ptr = this->m_header_ptr;
			this->m_write_ptr = this->m_header_ptr;
		}

		inline const char * data() const
		{
			return this->m_header_ptr;
		}

		std::basic_string<char, std::char_traits<char>, alloc_ty> str()
		{
			std::basic_string<char, std::char_traits<char>, alloc_ty> s(this->m_header_ptr, this->write_length());
			return s;
		}

		inline ::std::size_t read_length() const
		{
			return this->m_read_ptr - this->m_header_ptr;
		}

		inline ::std::size_t write_length() const
		{
			return this->m_write_ptr - this->m_header_ptr;
		}
	};

	typedef basic_string_stream<std::allocator<char>> string_stream;
}