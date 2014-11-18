/*
 * Copyright (c) 2014 Christian Authmann
 *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge, publish, distribute,
 * sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once

#include "typeid.h"

#include <unistd.h>
#include <string>
#include <vector>
#include <type_traits>
#include <exception>

/*
 * This is a stream class for IPC, meant to allow serialization of objects.
 *
 * We could use the POSIX socket API directly, but we choose to use a simple
 * wrapper for convenience and error handling via exceptions.
 *
 * We are not using std::iostream for several reasons.
 * First, the default overloads are meant for human-readable display, not
 * for efficient binary serialization.
 * Second, they're not reversible:
 *   out << 2 << 7 << 42
 * will result in a stream "2742", which cannot be correctly deserialized using
 *   in >> a >> b >> c
 *
 * Instead, we're opting for a very simple binary stream implementation
 * providing nothing but read() and write() functions, including some
 * overloads.
 *
 * - Primitive types are serialized as their binary representation.
 *   Do not attempt to communicate between machines of different word size
 *   or endianess!
 * - some native types (std::string, ...) have their own serialization functions
 * - other classes must implement serialize() and deserialize() methods
 *   (See foo.h for an example)
 *
 * Note that this is not meant as a lesson in good IPC or serialization design,
 * it's just a simple helper class to keep the rest of the code more readable.
 */

class BinaryStream {
	public:
		BinaryStream(int read_fd, int write_fd);
		~BinaryStream();
		void close();

		BinaryStream(const BinaryStream &) = delete;
		BinaryStream &operator=(const BinaryStream &) = delete;
		BinaryStream(BinaryStream &&);
		BinaryStream &operator=(BinaryStream &&);

		static BinaryStream connectToUnixSocket(const char *);

		void write(const char *buffer, size_t len);
		template<typename T> void write(const T& t);
		template<typename T> void write(T& t);

		size_t read(char *buffer, size_t len);
		template<typename T> typename std::enable_if< std::is_arithmetic<T>::value, size_t>::type
			read(T *t) { return read((char *) t, sizeof(T)); }
		template<typename T>
			T read();

		class stream_exception : std::exception {
		};

	private:
		bool is_eof;
		int read_fd, write_fd;
};



/*
 * For void_t, see the CppCon2014 talk by Walter E. Brown: "Modern Template Metaprogramming: A Compendium", Part II
 */
template<typename...>
	struct void_t_struct { using type = void; };
template<typename... C>
	using void_t = typename void_t_struct<C...>::type;

/*
 * Figure out if a class has serialize or deserialize methods
 */
template<typename T, typename = void>
struct has_typeid : std::is_class<T> { };
/*
template<typename T, typename = void>
struct has_typeid : std::false_type { };

template<typename T>
struct has_typeid<T, void_t<decltype(TYPEID<T>())> > : std::true_type { };
*/

template<typename T, typename = void>
struct is_serializable : std::false_type { };

template<typename T>
struct is_serializable<T, void_t<decltype(T::TYPEID)> > : std::true_type { };
//  decltype(T::deserialize(BinaryStream()))*/

/*
 * Declare functions for serialization/deserialization of important native classes
 */
namespace serialization {
	template <typename T>
	struct serializer { };

	template <>
	struct serializer<std::string> {
		static void serialize(BinaryStream &, const std::string &);
		static std::string deserialize(BinaryStream &);
	};

	template <typename T>
	struct serializer<std::vector<T>> {
		static void serialize(BinaryStream &, const std::vector<T> &);
		static std::vector<T> deserialize(BinaryStream &);
	};
}


/*
 * Templates for serialization
 */
// Arithmetic types: serialize the binary representation
template <typename T>
typename std::enable_if< std::is_arithmetic<T>::value >::type stream_write_helper(BinaryStream &stream, T& t) {
	stream.write((const char *) &t, sizeof(T));
}

// User-defined types: call .serialize()
template <typename T>
typename std::enable_if< is_serializable<T>::value && has_typeid<T>::value >::type stream_write_helper(BinaryStream &stream, T& t) {
	t.serialize(stream);
}

// Other classes: hopefully there's a function in the serialization namespace
template <typename T>
typename std::enable_if< !is_serializable<T>::value && has_typeid<T>::value >::type stream_write_helper(BinaryStream &stream, T &t) {
	serialization::serializer< typename std::decay<T>::type >::serialize(stream, t);
}


template<typename T> void BinaryStream::write(const T& t) {
	stream_write_helper<const T>(*this, t);
}
template<typename T> void BinaryStream::write(T& t) {
	stream_write_helper<T>(*this, t);
}


/*
 * Typed template for unserialization
 */
// Arithmetic types: deserialize the binary representation
template <typename T>
typename std::enable_if< std::is_arithmetic<T>::value, T >::type stream_read_helper(BinaryStream &stream) {
	T value;
	stream.read(&value);
	return value;
}

// User-defined types: call ::deserialize()
template <typename T>
typename std::enable_if< is_serializable<T>::value && has_typeid<T>::value, T >::type stream_read_helper(BinaryStream &stream) {
	return T::deserialize(stream);
}

// Other classes: hopefully there's a function in the serialization namespace
template <typename T>
typename std::enable_if< !is_serializable<T>::value && has_typeid<T>::value, T >::type stream_read_helper(BinaryStream &stream) {
	return serialization::serializer< typename std::decay<T>::type >::deserialize(stream);
}

template<typename T> T BinaryStream::read() {
	return stream_read_helper<T>(*this);
}
