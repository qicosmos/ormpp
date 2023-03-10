#pragma once
#include "reflection.hpp"
#include "../third_party/msgpack/include/msgpack.hpp"

namespace msgpack { MSGPACK_API_VERSION_NAMESPACE(v2)
{
	namespace adaptor
	{
		template <typename Tuple, size_t ... Is>
		inline auto make_define_array_from_tuple_impl(Tuple& tuple, std::index_sequence<Is...>)
		{
			return v1::type::make_define_array(std::get<Is>(tuple)...);
		}

		template <typename Tuple>
		inline auto make_define_array_from_tuple(Tuple& tuple)
		{
			using indices_type = std::make_index_sequence<std::tuple_size<Tuple>::value>;
			return make_define_array_from_tuple_impl(tuple, indices_type{});
		}

		template <typename T>
		struct convert<T, std::enable_if_t<::iguana::is_reflection<T>::value>>
		{
			msgpack::object const& operator()(msgpack::object const& o, T& v) const
			{
				auto tuple = ::iguana::get_ref(v);
				make_define_array_from_tuple(tuple).msgpack_unpack(o.convert());
				return o;
			}
		};

		template <typename T>
		struct pack<T, std::enable_if_t<::iguana::is_reflection<T>::value>>
		{
			template <typename Stream>
			msgpack::packer<Stream>& operator()(msgpack::packer<Stream>& o, T const& v) const
			{
				auto tuple = ::iguana::get(v);
				make_define_array_from_tuple(tuple).msgpack_pack(o);
				return o;
			}
		};
	}
} }

namespace iguana
{
	struct blob_t
	{
		blob_t() : raw_ref_() {}
		blob_t(char const* data, size_t size)
			: raw_ref_(data, static_cast<uint32_t>(size))
		{
		}

		template <typename Packer>
		void msgpack_pack(Packer& pk) const
		{
			pk.pack_bin(raw_ref_.size);
			pk.pack_bin_body(raw_ref_.ptr, raw_ref_.size);
		}

		void msgpack_unpack(::msgpack::object const& o)
		{
			::msgpack::operator >> (o, raw_ref_);
		}

		auto data() const
		{
			return raw_ref_.ptr;
		}

		size_t size() const
		{
			return raw_ref_.size;
		}

		::msgpack::type::raw_ref	raw_ref_;
	};

	class memory_buffer
	{
	public:
		memory_buffer()
			: memory_buffer(0)
		{ }

		explicit memory_buffer(size_t len)
			: buffer_(len, 0)
			, offset_(0)
		{ }

		memory_buffer(memory_buffer const&) = default;
		memory_buffer(memory_buffer &&) = default;
		memory_buffer& operator= (memory_buffer const&) = default;
		memory_buffer& operator= (memory_buffer &&) = default;

		void write(char const* data, size_t length)
		{
			if (buffer_.size() - offset_ < length)
				buffer_.resize(length + offset_);

			std::memcpy(buffer_.data() + offset_, data, length);
			offset_ += length;
		}

		std::vector<char> release() const noexcept
		{
			return std::move(buffer_);
		}

	private:
		std::vector<char>		buffer_;
		size_t				offset_;
	};

	namespace msgpack
	{
		template <typename T>
		inline void to_msgpack(memory_buffer& buf, T&& t)
		{
			::msgpack::pack(buf, std::forward<T>(t));
		}

		template <typename T>
		inline void from_msgpack(T& t, ::msgpack::unpacked& msg, char const* data, size_t length)
		{
			::msgpack::unpack(&msg, data, length);
			t = std::move(msg.get().as<T>());
		}

		template <typename T>
		inline void from_msgpack(T& t, char const* data, size_t length)
		{
			::msgpack::unpacked msg;
			from_msgpack(t, msg, data, length);
		}
	}
}
