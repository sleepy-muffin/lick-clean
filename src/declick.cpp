#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

#include "shark/audio.hpp"
#include "shark/log.hpp"

#include "declick.hpp"

#include "utils.hpp"

namespace lo = shark::media;
namespace sd = lo::declick;



void validate(
	const sd::options& config)
{
	if (!(config.clip_level > 0.0 && config.clip_level <= 1.0)) {
		throw std::invalid_argument("clip level must be in (0, 1]");
	}
	if (config.derivative_floor < 0.0 || config.hard_derivative < 0.0 ||
		config.derivative_ratio <= 0.0 || config.baseline_window_ms <= 0.0 ||
		config.baseline_exclusion_ms < 0.0 || config.merge_gap_ms < 0.0 ||
		config.pre_roll_ms < 0.0 || config.post_roll_ms < 0.0 || config.max_repair_ms <= 0.0 ||
		config.lpc_order < 2 || config.lpc_context_ms <= 0.0 || config.lpc_mix < 0.0 ||
		config.lpc_mix > 1.0)
	{
		throw std::invalid_argument("invalid non-positive detector or reconstruction setting");
	}
}

void write_report(
	const std::filesystem::path& path, const lo::decoded_pcm& audio, const sd::Result& result)
{
	std::ofstream out(path);
	if (!out) {
		throw std::runtime_error("cannot open report: " + path.string());
	}
	out << "channel,kind,core_begin_frame,core_end_frame,repair_begin_frame,repair_end_frame,"
		   "start_seconds,end_seconds,duration_ms,score,original_peak,repaired_peak\n";
	out << std::setprecision(10);
	for (const auto& event : result.events) {
		const double start = static_cast<double>(event.repair_begin) / audio.sample_rate;
		const double end = static_cast<double>(event.repair_end + 1) / audio.sample_rate;
		out << event.channel << ',' << sd::to_string(event.kind) << ',' << event.core_begin << ','
			<< event.core_end << ',' << event.repair_begin << ',' << event.repair_end << ','
			<< start << ',' << end << ',' << (end - start) * 1000.0 << ',' << event.score << ','
			<< event.original_peak << ',' << event.repaired_peak << '\n';
	}
}

bool is_opus(
	const std::filesystem::path& path)
{
	std::string extension = path.extension().string();
	std::transform(extension.begin(), extension.end(), extension.begin(),
				   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return extension == ".opus";
}

bool is_wav(
	const std::filesystem::path& path)
{
	std::string extension = path.extension().string();
	std::transform(extension.begin(), extension.end(), extension.begin(),
				   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return extension == ".wav";
}

bool is_mp3(
	const std::filesystem::path& path)
{
	std::string extension = path.extension().string();
	std::transform(extension.begin(), extension.end(), extension.begin(),
				   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return extension == ".mp3";
}

bool is_supported_audio(
	const std::filesystem::path& path)
{
	return is_opus(path) || is_wav(path) || is_mp3(path);
}

void append_frames(
	lo::decoded_pcm& destination, const lo::decoded_pcm& source, std::size_t begin, std::size_t end)
{
	if (begin > end || end > source.frames() || destination.sample_rate != source.sample_rate ||
		destination.channels != source.channels)
	{
		throw std::runtime_error("incompatible streamed audio block");
	}
	const std::size_t first = begin * source.channels;
	const std::size_t last = end * source.channels;
	destination.samples.insert(destination.samples.end(), source.samples.begin() + first,
							   source.samples.begin() + last);
}

lo::decoded_pcm take_frames(
	lo::decoded_pcm& source, std::size_t count)
{
	const std::size_t frames = std::min(count, source.frames());
	lo::decoded_pcm result = source;
	result.samples.clear();
	append_frames(result, source, 0, frames);
	source.samples.erase(source.samples.begin(), source.samples.begin() + frames * source.channels);
	return result;
}

template <typename reader_type, typename writer_type>
sd::Result process_streamed(
	const options& options, sd::options& config, lo::decoded_pcm& format,
	std::size_t& processed_frames, progress_info& info, const progress_callback& on_progress)
{
	reader_type reader(options.input);
	format = reader.format();
	info.sample_rate = format.sample_rate;
	info.channels = format.channels;
	info.bits_per_sample = format.bits_per_sample;
	const std::size_t core_frames = std::max<std::size_t>(
		1, static_cast<std::size_t>(options.chunk_seconds * format.sample_rate));
	const std::size_t overlap_frames = std::max<std::size_t>(1, format.sample_rate / 2);
	config.worker_threads = options.threads;
	const std::size_t total_frames = reader.total_frames();
	lo::decoded_pcm history = format;
	lo::decoded_pcm future = format;
	std::optional<writer_type> writer;
	if (!options.dry_run) {
		if constexpr (std::is_same_v<writer_type, lo::opus_writer>) {
			writer.emplace(options.output, format, options.opus_bitrate_kbps * 1000);
		}
		else if constexpr (std::is_same_v<writer_type, lo::mp3_writer>) {
			writer.emplace(options.output, format, options.mp3_bitrate_kbps * 1000);
		}
		else {
			writer.emplace(options.output, format);
		}
	}

	sd::Result total;
	std::size_t output_frames = 0;
	std::size_t completed_chunks = 0;
	auto show_progress = [&]
	{
		info.status_ = progress_info::status::in_progress;
		info.output_frames = output_frames;
		info.total_frames = total_frames;
		info.completed_chunks = completed_chunks;
		if (on_progress)
			on_progress(info);
	};
	show_progress();
	while (true) {
		while (future.frames() < core_frames) {
			lo::decoded_pcm block = reader.read_frames(core_frames - future.frames());
			if (block.samples.empty())
				break;
			append_frames(future, block, 0, block.frames());
		}
		if (future.samples.empty())
			break;
		lo::decoded_pcm core = take_frames(future, core_frames);
		while (future.frames() < overlap_frames) {
			lo::decoded_pcm block = reader.read_frames(overlap_frames - future.frames());
			if (block.samples.empty())
				break;
			append_frames(future, block, 0, block.frames());
		}

		lo::decoded_pcm window = format;
		append_frames(window, history, 0, history.frames());
		append_frames(window, core, 0, core.frames());
		append_frames(window, future, 0, future.frames());
		const std::size_t left = history.frames();
		const std::size_t right = left + core.frames();
		sd::Result result = sd::process(window, config);
		for (const auto& event : result.events) {
			if (event.core_begin >= left && event.core_begin < right) {
				sd::event shifted = event;
				const std::size_t offset = output_frames - left;
				shifted.core_begin += offset;
				shifted.core_end += offset;
				shifted.repair_begin += offset;
				shifted.repair_end += offset;
				total.events.push_back(shifted);
			}
		}
		total.clipped_seed_frames += result.clipped_seed_frames;
		total.impulse_seed_frames += result.impulse_seed_frames;
		if (writer) {
			lo::decoded_pcm output = format;
			append_frames(output, window, left, right);
			writer->write_frames(output);
		}
		output_frames += core.frames();
		++completed_chunks;
		show_progress();
		history = std::move(core);
		if (history.frames() > overlap_frames) {
			history.samples.erase(history.samples.begin(),
								  history.samples.end() - overlap_frames * history.channels);
		}
	}
	if (writer)
		writer->finish();
	processed_frames = output_frames;
	std::sort(total.events.begin(), total.events.end(),
			  [](const sd::event& a, const sd::event& b)
			  {
				  return a.repair_begin != b.repair_begin ? a.repair_begin < b.repair_begin
														  : a.channel < b.channel;
			  });
	return total;
}

std::filesystem::path default_windows_output_path(
	const std::filesystem::path& input)
{
	std::filesystem::path filename = input.stem();
	filename += ".processed";
	filename += input.extension();

	std::filesystem::path output = input.parent_path();
	output /= filename;
	return output;
}


void apply_overrides(
	shark::media::declick::options& config, const options& options)
{
	for (const auto& [name, value] : options.overrides) {
		if (name == "--clip-level") {
			config.clip_level = parse_double(name, value);
		}
		else if (name == "--derivative-floor") {
			config.derivative_floor = parse_double(name, value);
		}
		else if (name == "--hard-derivative") {
			config.hard_derivative = parse_double(name, value);
		}
		else if (name == "--derivative-ratio") {
			config.derivative_ratio = parse_double(name, value);
		}
		else if (name == "--merge-gap-ms") {
			config.merge_gap_ms = parse_double(name, value);
		}
		else if (name == "--pre-roll-ms") {
			config.pre_roll_ms = parse_double(name, value);
		}
		else if (name == "--post-roll-ms") {
			config.post_roll_ms = parse_double(name, value);
		}
		else if (name == "--max-repair-ms") {
			config.max_repair_ms = parse_double(name, value);
		}
		else if (name == "--repair-mode") {
			config.repair_mode = shark::media::declick::parse_repair_mode(value);
		}
		else if (name == "--lpc-order") {
			config.lpc_order = parse_size(name, value);
		}
		else if (name == "--lpc-context-ms") {
			config.lpc_context_ms = parse_double(name, value);
		}
		else if (name == "--lpc-mix") {
			config.lpc_mix = parse_double(name, value);
		}
		else {
			throw std::invalid_argument("unknown option: " + name);
		}
	}
}


int run_lick_clean(
	const options& options, progress_callback on_progress)
{
	progress_info info;
	info.input = options.input;
	info.output = options.output;
	info.preset = options.preset;
	if (on_progress)
		on_progress(info);

	try {
		sd::options config = sd::preset_config(options.preset);
		apply_overrides(config, options);
		validate(config);
		info.repair_mode = sd::to_string(config.repair_mode);
		if (!(options.chunk_seconds > 0.0)) {
			throw std::invalid_argument("chunk seconds must be positive");
		}
		if (options.opus_bitrate_kbps < 6 || options.opus_bitrate_kbps > 512) {
			throw std::invalid_argument("Opus bitrate must be between 6 and 512 kbps");
		}
		if (options.mp3_bitrate_kbps < 8 || options.mp3_bitrate_kbps > 320) {
			throw std::invalid_argument("MP3 bitrate must be between 8 and 320 kbps");
		}

		lo::decoded_pcm audio;
		sd::Result result;
		std::size_t frame_count = 0;
		const bool streamed_opus = is_opus(options.input);
		const bool streamed_wav = is_wav(options.input);
		const bool streamed_mp3 = is_mp3(options.input);
		const bool streamed = streamed_opus || streamed_wav || streamed_mp3;

		shark::log::info("from: {}; to: {}; dry_run: {}", options.input.string(),
						 options.output.string(), options.dry_run);
		if (!options.dry_run && !is_supported_audio(options.output)) {
			throw std::invalid_argument("output must use a .wav, .opus, or .mp3 extension");
		}
		if (streamed_opus) {
			if (is_opus(options.output)) {
				shark::log::info("lo::opus_reader -> lo::opus_writer");
				result = process_streamed<lo::opus_reader, lo::opus_writer>(
					options, config, audio, frame_count, info, on_progress);
			}
			else if (is_mp3(options.output)) {
				shark::log::info("lo::opus_reader -> lo::mp3_writer");
				result = process_streamed<lo::opus_reader, lo::mp3_writer>(
					options, config, audio, frame_count, info, on_progress);
			}
			else {
				shark::log::info("lo::opus_reader -> lo::wav_writer");
				result = process_streamed<lo::opus_reader, lo::wav_writer>(
					options, config, audio, frame_count, info, on_progress);
			}
		}
		else if (streamed_wav) {
			if (is_opus(options.output)) {
				shark::log::info("lo::wav_reader -> lo::opus_writer");
				result = process_streamed<lo::wav_reader, lo::opus_writer>(
					options, config, audio, frame_count, info, on_progress);
			}
			else if (is_mp3(options.output)) {
				shark::log::info("lo::wav_reader -> lo::mp3_writer");
				result = process_streamed<lo::wav_reader, lo::mp3_writer>(
					options, config, audio, frame_count, info, on_progress);
			}
			else {
				shark::log::info("lo::wav_reader -> lo::wav_writer");
				result = process_streamed<lo::wav_reader, lo::wav_writer>(
					options, config, audio, frame_count, info, on_progress);
			}
		}
		else if (streamed_mp3) {
			if (is_opus(options.output)) {
				result = process_streamed<lo::mp3_reader, lo::opus_writer>(
					options, config, audio, frame_count, info, on_progress);
			}
			else if (is_wav(options.output)) {
				result = process_streamed<lo::mp3_reader, lo::wav_writer>(
					options, config, audio, frame_count, info, on_progress);
			}
			else {
				result = process_streamed<lo::mp3_reader, lo::mp3_writer>(
					options, config, audio, frame_count, info, on_progress);
			}
		}
		else {
			audio = lo::read_audio(options.input);
			result = sd::process(audio, config);
			frame_count = audio.frames();
		}
		if (!options.report.empty()) {
			write_report(options.report, audio, result);
		}
		if (!options.dry_run && !streamed) {
			lo::write_audio(options.output, audio);
		}

		info.status_ = progress_info::status::completed;
		info.output_frames = frame_count;
		info.total_frames = frame_count;
		info.frame_count = frame_count;
		info.event_count = result.events.size();
		info.clipped_seed_frames = result.clipped_seed_frames;
		info.impulse_seed_frames = result.impulse_seed_frames;
		info.sample_rate = audio.sample_rate;
		info.channels = audio.channels;
		info.bits_per_sample = audio.bits_per_sample;
		info.stream_mode = streamed ? (streamed_opus	  ? "Opus"
										   : streamed_wav ? "WAV"
														  : "MP3")
									: "memory";
		if (on_progress)
			on_progress(info);
		return 0;
	} catch (const std::exception& error) {
		info.status_ = progress_info::status::failed;
		info.error = error.what();
		if (on_progress)
			on_progress(info);
		return 1;
	}
}
