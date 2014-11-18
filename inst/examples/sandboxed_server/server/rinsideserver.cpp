/*
 * Copyright (c) 2014 Christian Authmann
 */

#define LOG(...) {fprintf(stderr, "%d: ", getpid());fprintf(stderr, __VA_ARGS__);fprintf(stderr, "\n");}

#include <RInside.h>

#include "rinsideserver.h"
#include "internalfunction_clone.h"

#include <stdexcept>
#include <fstream>


// Two helper functions.
static void replace_all(std::string &str, const std::string &search, const std::string &replace) {
    size_t start_pos = 0;
    while ((start_pos = str.find(search, start_pos)) != std::string::npos) {
        str.replace(start_pos, search.length(), replace);
        start_pos += replace.length();
    }
};

static std::string read_file_as_string(const std::string &filename) {
	std::ifstream in(filename, std::ios::in | std::ios::binary);
	if (in) {
		std::string contents;
		in.seekg(0, std::ios::end);
		contents.resize(in.tellg());
		in.seekg(0, std::ios::beg);
		in.read(&contents[0], contents.size());
		in.close();
		return contents;
	}
	throw std::runtime_error("Could not read file");
}


std::map<int32_t, std::function<SEXP(BinaryStream &)> > RInsideServer::registry_sexp_from_stream;
std::map<int32_t, std::function<void(RInsideServer &, SEXP, bool)> > RInsideServer::registry_sexp_to_stream;



RInsideServer::RInsideServer(BinaryStream &stream, RInside &R, RInsideCallbacks &Rcallbacks)
	: stream(stream), R(R), Rcallbacks(Rcallbacks), can_send_reply(false) {

}
RInsideServer::~RInsideServer() {

}

/*
 * Just a shorthand for the repetitive error handling that follows.
 * The CMD_TRY must only start after all relevant input has been read, i.e. when the server would be allowed to send a reply.
 * Connection errors are always considered fatal and will cause the server to stop.
 */
#define CMD_TRY try {
#define CMD_CATCH } catch (const BinaryStream::stream_exception) { throw; } catch (const std::exception &e) { std::string s = e.what(); LOG("Command failed: %s", s.c_str()); sendReply(RIS_REPLY_ERROR); stream.write(s); }



void RInsideServer::run() {
	auto magic = stream.read<int>();
	if (magic != RIS_MAGIC_NUMBER)
		throw std::runtime_error("Client sent the wrong magic number");

	while (true) {
		auto cmd = stream.read<char>();
		allowSendReply();
		LOG("Requested command: %d", cmd);

		if (cmd == RIS_CMD_EXIT) {
			return;
		}
		else if (cmd == RIS_CMD_SETVALUE) {
			auto name = stream.read<std::string>();
			auto sexp = sexp_from_stream();
			CMD_TRY
				R[name] = sexp;
				sendReply(RIS_REPLY_OK);
			CMD_CATCH
		}
		else if (cmd == RIS_CMD_GETVALUE) {
			auto name = stream.read<std::string>();
			auto type = stream.read<int32_t>();

			CMD_TRY
				auto sexp = R[name];
				sexp_to_stream(sexp, type, true);
			CMD_CATCH
		}
		else if (cmd == RIS_CMD_SETCALLBACK) {
			auto name = stream.read<std::string>();
			auto callback_id = stream.read<uint32_t>();
			auto result_type = stream.read<uint32_t>();
			auto paramcount = stream.read<size_t>();

			std::vector<int32_t> param_types;
			param_types.reserve(paramcount+1);
			param_types.push_back(result_type);
			for (size_t i=0;i<paramcount;i++) {
				auto type = stream.read<int32_t>();
				param_types.push_back(type);
			}

			/*
			 * Ok, frankly, this is a hack.
			 * We need an InternalFunction, but we don't have a compatible C++ function - we only have
			 * the parameter count and typeids.
			 *
			 * To do this, we need to get down to the point where the function parameters are nothing but an array of SEXP.
			 * And that's so deep in the CppFunction implementation of Rcpp, that we need some detours to get there.
			 */
			CMD_TRY
				R[name] = Rcpp::InternalFunctionForRInsideServer(*this, callback_id, param_types);
				sendReply(RIS_REPLY_OK);
			CMD_CATCH
			LOG("Callback %s initialized", name.c_str());
		}
		else if (cmd == RIS_CMD_RUN) {
			auto source = stream.read<std::string>();
			// R on linux doesn't deal well with windows \r\n line endings, so we replace those
			replace_all(source, "\r\n", "\n");

			auto type = stream.read<int32_t>();

			CMD_TRY
				std::string delimiter = "\n\n";
				size_t start = 0;
				size_t end = 0;
				while (true) {
					end = source.find(delimiter, start);
					if (end == std::string::npos)
						break;
					std::string line = source.substr(start, end-start);
					start = end+delimiter.length();
					LOG("src: %s", line.c_str());
					R.parseEvalQ(line);
				}
				std::string lastline = source.substr(start);
				LOG("src: %s", lastline.c_str());
				auto result = R.parseEval(lastline);
				LOG("Sending reply for type %d", type);
				if (type == 0)
					sendReply(RIS_REPLY_OK);
				else {
					sexp_to_stream(result, type, true);
				}
			CMD_CATCH
		}
		else if (cmd == RIS_CMD_GETCONSOLE) {
			std::string output = Rcallbacks.getConsoleOutput();
			Rcallbacks.resetConsoleOutput();
			sendReply(RIS_REPLY_VALUE);
			stream.write(output);
		}
		else if (cmd == RIS_CMD_INITPLOT) {
			auto width = stream.read<uint32_t>();
			auto height = stream.read<uint32_t>();

			CMD_TRY
				R.parseEval("rserver_plot_tempfile = tempfile(\"rs_plot\", fileext=\".png\")");
				R.parseEval("png(rserver_plot_tempfile, width=" + std::to_string(width) + ", height=" + std::to_string(height)+", bg=\"transparent\")");
				sendReply(RIS_REPLY_OK);
			CMD_CATCH
		}
		else if (cmd == RIS_CMD_GETPLOT) {
			CMD_TRY
				R.parseEval("dev.off()");
				std::string filename = Rcpp::as<std::string>(R["rserver_plot_tempfile"]);
				std::string output = read_file_as_string(filename);
				std::remove(filename.c_str());
				sendReply(RIS_REPLY_VALUE);
				stream.write(output);
			CMD_CATCH
		}
		else
			throw std::runtime_error("Client sent unknown command");
	}
}

SEXP RInsideServer::sexp_from_stream() {
	auto type = stream.read<int32_t>();

	if (registry_sexp_from_stream.count(type) < 1) {
		LOG("unknown type in sexp_from_stream: %d", type);
		throw std::runtime_error("Unknown datatype in sexp_from_stream");
	}

	return registry_sexp_from_stream[type](stream);
}

void RInsideServer::sexp_to_stream(SEXP sexp, int32_t type, bool include_reply) {
	if (registry_sexp_to_stream.count(type) < 1) {
		LOG("unknown type in sexp_to_stream: %d", type);
		throw std::runtime_error("Unknown datatype in sexp_to_stream");
	}

	registry_sexp_to_stream[type](*this, sexp, include_reply);
}

void RInsideServer::registerDefaultTypes() {
	registerType<int>();
	registerType<float>();
	registerType<std::vector<int>>();
	registerType<std::vector<float>>();
	registerType<std::string>();
}
