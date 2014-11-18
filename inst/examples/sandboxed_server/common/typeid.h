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

#include <cstdint>
#include <type_traits>
#include <string>
#include <vector>

/*
 * We need a value for each type so we can communicate which type to send or receive over the socket.
 *
 * std::type_info won't help, since its values may change on each program start, making them unsuitable for client/server-communication.
 *
 * Our typeid is an int32_t. Negative values are reserved for native types (int, float, std::string, ...) while positive values
 * can be used in custom classes. See datatypes/foo.h for the syntax.
 */



/*
 * Note: Calling TYPEID() on an unsupported type yields some cryptic compiler errors. If you have seen errors in one of the lines below,
 * make sure that the type you're calling TYPEID() on is either
 * - a supported native type and has a specialization below
 * or
 * - a custom class with TYPEID, serialize() and deserialize() members
 */
template <typename T>
struct typeid_helper {
	static const typename std::enable_if< T::TYPEID, int32_t >::type
	value = T::TYPEID;
};

template <>
struct typeid_helper<void> {
	static const int32_t value = 0;
};

template <>
struct typeid_helper<int> {
	static const int32_t value = -1;
};

template <>
struct typeid_helper<float> {
	static const int32_t value = -2;
};

template <>
struct typeid_helper<std::vector<int>> {
	static const int32_t value = -11;
};

template <>
struct typeid_helper<std::vector<float>> {
	static const int32_t value = -12;
};

template <>
struct typeid_helper<std::string> {
	static const int32_t value = -20;
};



template <typename T>
constexpr int32_t TYPEID() {
	return typeid_helper< typename std::decay<T>::type >::value;
}

