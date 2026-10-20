#include <vector>
#include <iostream>

#include "declick.hpp"
#include "shark/audio.hpp"
#include "utils.hpp"

#include <iomanip>

void usage(
	std::ostream& out)
{
	constexpr const char* use =
		"shark-declick: asmr short-pulse and clipping repair for WAV, Opus, and MP3 audio\n\n"
		"usage:\n"
		"  shark-declick [options] input.{wav,opus,mp3} output.{wav,opus,mp3}\n\n"
		"examples:\n"
		"  shark-declick --preset asmr input.wav output.wav\n\n"
		"  shark-declick --preset asmr input.opus output.opus\n\n"
		"  shark-declick --preset asmr input.wav output.opus\n\n"
		"  shark-declick --preset aggressive input.opus output.wav\n\n"
		"  shark-declick --preset conservative input.mp3 output.opus\n\n"
		"options:\n"
		"  --preset NAME              aggressive, asmr (default), conservative, clip-only\n"
		"  --clip-level X             full-scale seed threshold, default 0.985\n"
		"  --derivative-floor X       minimum jump used with local-ratio test\n"
		"  --hard-derivative X        always detect jumps at least this large\n"
		"  --derivative-ratio X       jump / robust local baseline threshold\n"
		"  --merge-gap-ms X           merge nearby seed samples into one pulse\n"
		"  --pre-roll-ms X            context replaced before detected pulse\n"
		"  --post-roll-ms X           context replaced after detected pulse\n"
		"  --max-repair-ms X          cap ordinary repair-region length\n"
		"  --repair-mode NAME         cubic, hybrid, or lpc\n"
		"  --lpc-order N              linear-prediction order\n"
		"  --lpc-context-ms X         context used on each side\n"
		"  --lpc-mix X                LPC share in hybrid mode, 0..1\n"
		"  --report FILE.csv          write detected-event diagnostics\n"
		"  --chunk-seconds X          streamed core block duration, default 120\n"
		"  --threads N                channel worker threads; 0 = auto (default)\n"
		"  --opus-bitrate KBPS        target Opus bitrate, 6..512; default 192\n"
		"  --mp3-bitrate KBPS         target MP3 bitrate, 8..320; default 192\n"
		"  --dry-run                  detect/report without writing output\n"
		"  -h, --help                 show this message\n";
	out << use;
}




options parse_options(
	int argc, char** argv)
{
	options options;
	std::vector<std::string> positional;
	for (int i = 1; i < argc; ++i) {
		const std::string arg = argv[i];
		if (arg == "-h" || arg == "--help") {
			usage(std::cout);
			std::exit(0);
		}
		if (arg == "--dry-run") {
			options.dry_run = true;
			continue;
		}
		if (arg.rfind("--", 0) == 0) {
			if (i + 1 >= argc) {
				throw std::invalid_argument("missing value after " + arg);
			}
			const std::string value = argv[++i];
			if (arg == "--preset") {
				options.preset = value;
			}
			else if (arg == "--report") {
				options.report = value;
			}
			else if (arg == "--chunk-seconds") {
				options.chunk_seconds = parse_double(arg, value);
			}
			else if (arg == "--threads") {
				options.threads = parse_size(arg, value);
			}
			else if (arg == "--opus-bitrate") {
				options.opus_bitrate_kbps = parse_size(arg, value);
			}
			else if (arg == "--mp3-bitrate") {
				options.mp3_bitrate_kbps = parse_size(arg, value);
			}
			else {
				options.overrides.emplace_back(arg, value);
			}
			continue;
		}
		positional.push_back(arg);
	}

	if (options.dry_run) {
		if (positional.size() != 1 && positional.size() != 2) {
			throw std::invalid_argument("dry-run expects input.wav and optionally output.wav");
		}
	}
	else if (positional.size() != 2) {
		throw std::invalid_argument("expected input.wav and output.wav");
	}

	options.input = positional[0];
	if (positional.size() == 2) {
		options.output = positional[1];
	}
	return options;
}



int declick_cli(
	int argc, char** argv, std::ostream& out, std::ostream& err)
{
	try {
		const options options = parse_options(argc, argv);
		return run_lick_clean(
			options,
			[&out](const progress_info& info)
			{
				switch (info.status_) {
				case progress_info::status::started:
					out << "input: " << info.input << '\n'
						<< "output: " << info.output << '\n'
						<< "preset: " << info.preset << '\n';
					break;
				case progress_info::status::in_progress:
					out << "\rprogress: ";
					if (info.sample_rate != 0)
						out << std::fixed << std::setprecision(1)
							<< static_cast<double>(info.output_frames) / info.sample_rate << " s";
					else
						out << info.output_frames << " frame(s)";
					if (info.total_frames != 0) {
						out << " / " << info.total_frames << " frame(s) (" << std::setprecision(0)
							<< 100.0 * info.output_frames / info.total_frames << "%)";
					}
					out << ", chunk " << info.completed_chunks << std::flush;
					break;
				case progress_info::status::completed:
					out << "\nformat: " << info.sample_rate << " Hz, " << info.channels
						<< " channel(s), " << info.bits_per_sample << "-bit\n"
						<< "frames: " << info.frame_count << '\n'
						<< "repair mode: " << info.repair_mode << '\n'
						<< "events: " << info.event_count << '\n'
						<< "clipped seed frames: " << info.clipped_seed_frames << '\n'
						<< "impulse seed frames: " << info.impulse_seed_frames << '\n'
						<< "output: " << info.output << '\n';
					break;
				case progress_info::status::failed:
					out << "\nerror: " << info.error << '\n';
					break;
				}
			});
	} catch (const std::exception& error) {
		err << "error: " << error.what() << "\n\n";
		usage(err);
		return 1;
	}

	return 0;
}

#ifdef _WIN32

int wmain()
{
	int argc = 0;
	wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	if (!argv)
	{
		std::cerr << "Failed to parse command line.\n";
		return 1;
	}

	std::vector<std::string> args;
	args.reserve(argc);

	for (int i = 0; i < argc; ++i)
	{
		const int size = WideCharToMultiByte(
			CP_UTF8, WC_ERR_INVALID_CHARS,
			argv[i], -1, nullptr, 0, nullptr, nullptr);

		if (size == 0)
		{
			LocalFree(argv);
			std::cerr << "Failed to convert command-line arguments to UTF-8.\n";
			return 1;
		}

		std::string arg(size, '\0');

		if (WideCharToMultiByte(
				CP_UTF8, WC_ERR_INVALID_CHARS,
				wargv[i], -1, arg.data(), size, nullptr, nullptr) == 0)
		{
			LocalFree(argv);
			std::cerr << "Failed to convert command-line arguments to UTF-8.\n";
			return 1;
		}

		arg.pop_back(); // Remove terminating null byte.
		args.push_back(std::move(arg));
	}

	LocalFree(argv);

	std::vector<char*> argv;
	argv.reserve(args.size());

	for (auto& arg : args)
		argv.push_back(arg.data());

	return declick_cli(argc, argv.data(), std::cout, std::cerr);
}
#elif defined(__linux__)
int main(
	int argc, char** argv)
{
	return declick_cli(argc, argv, std::cout, std::cerr);
}
#else
int main(
	int argc, char** argv)
{
	return declick_cli(argc, argv, std::cout, std::cerr);
}
#endif