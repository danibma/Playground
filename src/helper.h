#pragma once

#include <chrono>
#include <vector>
#include <string>
#include <fstream>

namespace Timer
{
	std::chrono::high_resolution_clock::time_point timestamp = std::chrono::high_resolution_clock::now();

	// Record a reference timestamp
	inline void record()
	{
		timestamp = std::chrono::high_resolution_clock::now();
	}

	// Elapsed time in seconds since the Timer creation or last call to record()
	inline double elapsed_seconds()
	{
		auto timestamp2 = std::chrono::high_resolution_clock::now();
		std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(timestamp2 - timestamp);
		return time_span.count();
	}

	// Elapsed time in milliseconds since the Timer creation or last call to record()
	inline double elapsed_milliseconds()
	{
		return elapsed_seconds() * 1000.0;
	}

	// Elapsed time in milliseconds since the Timer creation or last call to record()
	inline double elapsed()
	{
		return elapsed_milliseconds();
	}
}

inline std::vector<char> readFile(const std::string& filename)
{
	std::ifstream file(filename, std::ios::ate | std::ios::binary);
	bool exists = (bool)file;

	if (!exists || !file.is_open())
	{
		throw std::runtime_error("failed to open file!");
	}

	size_t fileSize = (size_t)file.tellg();
	std::vector<char> buffer(fileSize);

	file.seekg(0);
	file.read(buffer.data(), fileSize);

	file.close();

	return buffer;
};
