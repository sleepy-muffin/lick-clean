#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>


struct progress_info
{
	enum class status { started, in_progress, completed, failed };

	status status_ = status::started;
	std::filesystem::path input;
	std::filesystem::path output;
	std::string preset;
	std::string repair_mode;
	std::string stream_mode;
	std::string error;
	std::size_t output_frames = 0;
	std::size_t total_frames = 0;
	std::size_t completed_chunks = 0;
	std::size_t frame_count = 0;
	std::size_t event_count = 0;
	std::size_t clipped_seed_frames = 0;
	std::size_t impulse_seed_frames = 0;
	std::uint32_t sample_rate = 0;
	std::uint16_t channels = 0;
	std::uint16_t bits_per_sample = 0;
};


struct options
{
	std::string preset = "asmr";
	std::filesystem::path input;
	std::filesystem::path output;
	std::filesystem::path report;
	bool dry_run = false;
	double chunk_seconds = 120.0;
	std::size_t threads = 0;
	std::size_t opus_bitrate_kbps = 192;
	std::size_t mp3_bitrate_kbps = 192;
	std::vector<std::pair<std::string, std::string>> overrides;
};


using progress_callback = std::function<void(const progress_info&)>;


int run_lick_clean(
	const options& options, progress_callback on_progress);
