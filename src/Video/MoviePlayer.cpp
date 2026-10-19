/*******************************************************************************
 * Copyright (c) 2018-2026 openblack developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/openblack/openblack
 *
 * openblack is licensed under the GNU General Public License version 3.
 *******************************************************************************/

#include "MoviePlayer.h"

#include <spdlog/spdlog.h>

#if defined(OPENBLACK_HAVE_FFMPEG)
#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <vector>

#include <AL/al.h>
#include <bgfx/bgfx.h>
#include <imgui.h>

#include "Debug/ImGuiUtils.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace
{
struct Movie
{
	AVFormatContext* format = nullptr;
	AVCodecContext* video = nullptr;
	AVCodecContext* audio = nullptr;
	int videoStream = -1;
	int audioStream = -1;
	SwsContext* scale = nullptr;
	SwrContext* resample = nullptr;
	AVPacket* packet = nullptr;
	AVFrame* frame = nullptr;
	AVFrame* due = nullptr; // the newest decoded picture not yet shown
	double dueAt = 0.0;     // its time, seconds from the start
	bool dueValid = false;
	bool ended = false; // no more packets
	bgfx::TextureHandle texture = BGFX_INVALID_HANDLE;
	int width = 0;
	int height = 0;
	std::vector<uint8_t> rgba;
	ALuint source = 0;
	std::array<ALuint, 32> buffers {};
	std::vector<ALuint> freeBuffers;
	int sampleRate = 0;
	std::chrono::steady_clock::time_point start;
};

Movie* g_movie = nullptr;

double Seconds(const AVStream* stream, int64_t pts)
{
	return pts == AV_NOPTS_VALUE ? 0.0 : static_cast<double>(pts) * av_q2d(stream->time_base);
}

AVCodecContext* OpenDecoder(AVFormatContext* format, int stream)
{
	const auto* parameters = format->streams[stream]->codecpar;
	const auto* codec = avcodec_find_decoder(parameters->codec_id);
	if (codec == nullptr)
	{
		return nullptr;
	}
	auto* context = avcodec_alloc_context3(codec);
	if (avcodec_parameters_to_context(context, parameters) < 0 || avcodec_open2(context, codec, nullptr) < 0)
	{
		avcodec_free_context(&context);
		return nullptr;
	}
	return context;
}

// Decoded sound into OpenAL's queue: 16-bit stereo, played as it comes (the source restarts if it ran dry).
void QueueAudio(Movie& movie, const AVFrame* frame)
{
	ALint processed = 0;
	alGetSourcei(movie.source, AL_BUFFERS_PROCESSED, &processed);
	while (processed-- > 0)
	{
		ALuint buffer = 0;
		alSourceUnqueueBuffers(movie.source, 1, &buffer);
		movie.freeBuffers.push_back(buffer);
	}
	if (movie.freeBuffers.empty())
	{
		return; // every buffer still waiting to be heard: this frame's sound dropped rather than the picture held up
	}
	const int outSamples = swr_get_out_samples(movie.resample, frame->nb_samples);
	std::vector<int16_t> pcm(static_cast<size_t>(std::max(outSamples, 0)) * 2);
	auto* out = reinterpret_cast<uint8_t*>(pcm.data());
	const int got = swr_convert(movie.resample, &out, outSamples, const_cast<const uint8_t**>(frame->extended_data), frame->nb_samples);
	if (got <= 0)
	{
		return;
	}
	const auto buffer = movie.freeBuffers.back();
	movie.freeBuffers.pop_back();
	alBufferData(buffer, AL_FORMAT_STEREO16, pcm.data(), got * 4, movie.sampleRate);
	alSourceQueueBuffers(movie.source, 1, &buffer);
	ALint state = 0;
	alGetSourcei(movie.source, AL_SOURCE_STATE, &state);
	if (state != AL_PLAYING)
	{
		alSourcePlay(movie.source);
	}
}

void ShowFrame(Movie& movie, const AVFrame* frame)
{
	uint8_t* planes[1] = {movie.rgba.data()};
	const int strides[1] = {movie.width * 4};
	sws_scale(movie.scale, frame->data, frame->linesize, 0, movie.height, planes, strides);
	bgfx::updateTexture2D(movie.texture, 0, 0, 0, 0, static_cast<uint16_t>(movie.width), static_cast<uint16_t>(movie.height),
	                      bgfx::copy(movie.rgba.data(), static_cast<uint32_t>(movie.rgba.size())));
}

// Reads one packet and decodes it; false at the end of the file.
bool ReadPacket(Movie& movie)
{
	if (av_read_frame(movie.format, movie.packet) < 0)
	{
		return false;
	}
	if (movie.packet->stream_index == movie.videoStream && avcodec_send_packet(movie.video, movie.packet) >= 0)
	{
		while (avcodec_receive_frame(movie.video, movie.frame) >= 0)
		{
			// Kept to be shown when its time comes (UpdateMovie reads on only once the one before is shown).
			av_frame_unref(movie.due);
			av_frame_move_ref(movie.due, movie.frame);
			movie.dueAt = Seconds(movie.format->streams[movie.videoStream], movie.due->best_effort_timestamp);
			movie.dueValid = true;
		}
	}
	else if (movie.packet->stream_index == movie.audioStream && movie.audio != nullptr &&
	         avcodec_send_packet(movie.audio, movie.packet) >= 0)
	{
		while (avcodec_receive_frame(movie.audio, movie.frame) >= 0)
		{
			QueueAudio(movie, movie.frame);
			av_frame_unref(movie.frame);
		}
	}
	av_packet_unref(movie.packet);
	return true;
}
} // namespace

namespace openblack::video
{
bool StartMovie(const std::filesystem::path& path)
{
	StopMovie();
	auto movie = std::make_unique<Movie>();
	// "file:": a Switch path starts "sdmc:", which ffmpeg would take for a protocol name.
	const auto url = "file:" + path.generic_string();
	if (avformat_open_input(&movie->format, url.c_str(), nullptr, nullptr) < 0 ||
	    avformat_find_stream_info(movie->format, nullptr) < 0)
	{
		SPDLOG_LOGGER_ERROR(spdlog::get("game"), "Movie {}: can't open it", path.generic_string());
		if (movie->format != nullptr)
		{
			avformat_close_input(&movie->format);
		}
		return false;
	}
	movie->videoStream = av_find_best_stream(movie->format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
	movie->audioStream = av_find_best_stream(movie->format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
	movie->video = movie->videoStream >= 0 ? OpenDecoder(movie->format, movie->videoStream) : nullptr;
	if (movie->video == nullptr)
	{
		SPDLOG_LOGGER_ERROR(spdlog::get("game"), "Movie {}: no video decoder", path.generic_string());
		avformat_close_input(&movie->format);
		return false;
	}
	movie->width = movie->video->width;
	movie->height = movie->video->height;
	movie->scale = sws_getContext(movie->width, movie->height, movie->video->pix_fmt, movie->width, movie->height, AV_PIX_FMT_RGBA,
	                              SWS_POINT, nullptr, nullptr, nullptr);
	movie->rgba.assign(static_cast<size_t>(movie->width * movie->height * 4), 0);
	movie->texture = bgfx::createTexture2D(static_cast<uint16_t>(movie->width), static_cast<uint16_t>(movie->height), false, 1,
	                                       bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
	movie->audio = movie->audioStream >= 0 ? OpenDecoder(movie->format, movie->audioStream) : nullptr;
	if (movie->audio != nullptr)
	{
		movie->sampleRate = movie->audio->sample_rate;
		AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
		swr_alloc_set_opts2(&movie->resample, &stereo, AV_SAMPLE_FMT_S16, movie->sampleRate, &movie->audio->ch_layout,
		                    movie->audio->sample_fmt, movie->sampleRate, 0, nullptr);
		if (movie->resample == nullptr || swr_init(movie->resample) < 0)
		{
			avcodec_free_context(&movie->audio);
		}
		else
		{
			alGenSources(1, &movie->source);
			// Not placed in the world: heard the same wherever the camera is.
			alSourcei(movie->source, AL_SOURCE_RELATIVE, AL_TRUE);
			alSource3f(movie->source, AL_POSITION, 0.0f, 0.0f, 0.0f);
			alGenBuffers(static_cast<ALsizei>(movie->buffers.size()), movie->buffers.data());
			movie->freeBuffers.assign(movie->buffers.begin(), movie->buffers.end());
		}
	}
	movie->packet = av_packet_alloc();
	movie->frame = av_frame_alloc();
	movie->due = av_frame_alloc();
	movie->start = std::chrono::steady_clock::now();
	SPDLOG_LOGGER_INFO(spdlog::get("game"), "Movie {}: {}x{}, {:.1f} s, sound {}", path.generic_string(), movie->width,
	                   movie->height, static_cast<double>(movie->format->duration) / AV_TIME_BASE,
	                   movie->audio != nullptr ? fmt::format("{} Hz", movie->sampleRate) : "none");
	g_movie = movie.release();
	return true;
}

bool MoviePlaying()
{
	return g_movie != nullptr;
}

void UpdateMovie()
{
	if (g_movie == nullptr)
	{
		return;
	}
	auto& movie = *g_movie;
	const double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - movie.start).count();
	// Read on until the next picture is in the future (sound comes along in the same packets).
	while (!movie.ended && (!movie.dueValid || movie.dueAt <= now))
	{
		if (movie.dueValid)
		{
			ShowFrame(movie, movie.due);
			movie.dueValid = false;
		}
		if (!ReadPacket(movie))
		{
			movie.ended = true;
		}
	}
	if (movie.ended)
	{
		if (movie.dueValid && movie.dueAt <= now)
		{
			ShowFrame(movie, movie.due);
			movie.dueValid = false;
		}
		ALint state = AL_STOPPED;
		if (movie.source != 0)
		{
			alGetSourcei(movie.source, AL_SOURCE_STATE, &state);
		}
		if (!movie.dueValid && state != AL_PLAYING)
		{
			SPDLOG_LOGGER_INFO(spdlog::get("game"), "Movie finished after {:.1f} s", now);
			StopMovie();
		}
	}
}

void StopMovie()
{
	if (g_movie == nullptr)
	{
		return;
	}
	auto* movie = g_movie;
	g_movie = nullptr;
	if (movie->source != 0)
	{
		alSourceStop(movie->source);
		alSourcei(movie->source, AL_BUFFER, 0);
		alDeleteSources(1, &movie->source);
		alDeleteBuffers(static_cast<ALsizei>(movie->buffers.size()), movie->buffers.data());
	}
	if (bgfx::isValid(movie->texture))
	{
		bgfx::destroy(movie->texture);
	}
	av_frame_free(&movie->due);
	av_frame_free(&movie->frame);
	av_packet_free(&movie->packet);
	swr_free(&movie->resample);
	sws_freeContext(movie->scale);
	avcodec_free_context(&movie->audio);
	avcodec_free_context(&movie->video);
	avformat_close_input(&movie->format);
	delete movie;
}

void DrawMovie()
{
	if (g_movie == nullptr)
	{
		return;
	}
	const auto& movie = *g_movie;
	const auto& display = ImGui::GetIO().DisplaySize;
	auto* draw = ImGui::GetForegroundDrawList();
	draw->AddRectFilled(ImVec2(0.0f, 0.0f), display, IM_COL32(0, 0, 0, 255));
	// As large as fits, its own shape kept.
	const float fit = std::min(display.x / static_cast<float>(movie.width), display.y / static_cast<float>(movie.height));
	const ImVec2 size(static_cast<float>(movie.width) * fit, static_cast<float>(movie.height) * fit);
	const ImVec2 corner((display.x - size.x) * 0.5f, (display.y - size.y) * 0.5f);
	union
	{
		struct
		{
			bgfx::TextureHandle handle;
			uint8_t flags;
			uint8_t mip;
		} s;
		ImTextureID ptr;
	} texture {};
	texture.s.handle = movie.texture;
	texture.s.flags = IMGUI_FLAGS_NONE;
	draw->AddImage(texture.ptr, corner, ImVec2(corner.x + size.x, corner.y + size.y));
}
} // namespace openblack::video

#else

namespace openblack::video
{
bool StartMovie(const std::filesystem::path& path)
{
	SPDLOG_LOGGER_WARN(spdlog::get("game"), "Movie {}: this build has no movie decoder (ffmpeg), not played", path.generic_string());
	return false;
}

bool MoviePlaying()
{
	return false;
}

void UpdateMovie() {}

void StopMovie() {}

void DrawMovie() {}
} // namespace openblack::video

#endif
