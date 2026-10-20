#pragma once

#include <stdexcept>
#include <string>

inline double parse_double(
	const std::string& option, const std::string& value)
{
	std::size_t consumed = 0;
	const double result = std::stod(value, &consumed);
	if (consumed != value.size()) {
		throw std::invalid_argument("invalid value for " + option + ": " + value);
	}
	return result;
}

inline std::size_t parse_size(
	const std::string& option, const std::string& value)
{
	std::size_t consumed = 0;
	const auto result = std::stoull(value, &consumed);
	if (consumed != value.size()) {
		throw std::invalid_argument("invalid value for " + option + ": " + value);
	}
	return static_cast<std::size_t>(result);
}
