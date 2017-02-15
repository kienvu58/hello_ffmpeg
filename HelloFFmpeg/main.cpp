#pragma warning(disable: 4996)
#include <iostream>
#include <Windows.h>

extern "C"
{
#include <libavcodec\avcodec.h>
#include <libavformat\avformat.h>
#include <libavutil\imgutils.h>
#include <libswscale\swscale.h>
#include <SDL.h>
#include <SDL_thread.h>
}
#undef main

const char *videoPath = "Wildlife.wmv";

void SaveFrame(uint8_t *const data[], const int linesize[], int width, int height, int iFrame);

int OpenCodecContext(int *streamIndex,
    AVCodecContext **ppDecoderContext,
    AVFormatContext *pFormatContext,
    enum AVMediaType type
);

int Decode(AVCodecContext *pCodecContext, AVFrame *pFrame, int *gotFrame, AVPacket *pPacket);

int main(int argc, char** argv)
{
    /// Register all formats and codecs
    av_register_all();

    /// Open input file, and allocate format context
    AVFormatContext *pFormatContext = nullptr;
    if (avformat_open_input(&pFormatContext, videoPath, nullptr, nullptr) < 0)
    {
        OutputDebugString("Could not open file!\n");
        return -1;
    }

    /// Retrieve stream information
    if (avformat_find_stream_info(pFormatContext, nullptr) < 0)
    {
        OutputDebugString("Could not find stream info!\n");
        return -1;
    }

    AVCodecContext *pVideoDecoderContext = nullptr;
    int videoStreamIndex;
    if (OpenCodecContext(&videoStreamIndex, &pVideoDecoderContext, pFormatContext, AVMEDIA_TYPE_VIDEO) < 0)
    {
        OutputDebugString("Could not open codec context\n");
        return -1;
    }
    AVStream *pVideoStream = pFormatContext->streams[videoStreamIndex];

    int width = pVideoDecoderContext->width;
    int height = pVideoDecoderContext->height;
    enum AVPixelFormat pixelFormat = pVideoDecoderContext->pix_fmt;

    av_dump_format(pFormatContext, 0, argv[1], 0);

    if (pVideoStream == nullptr)
    {
        OutputDebugString("Could not find video stream in the input, aborting\n");
        return -1;
    }

    AVFrame *pFrame = av_frame_alloc();
    if (pFrame == nullptr)
    {
        OutputDebugString("Couldn't allocate frame!\n");
        return -1;
    }

    struct SwsContext *pSwsContext = sws_getContext(width,
        height,
        pixelFormat,
        width,
        height,
        AV_PIX_FMT_YUV420P,
        SWS_BILINEAR,
        nullptr,
        nullptr,
        nullptr);

    /// SDL
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);
    SDL_Surface *screen = SDL_SetVideoMode(width, height, 0, 0);
    SDL_Overlay *bmp = SDL_CreateYUVOverlay(width, height, SDL_YV12_OVERLAY, screen);
    SDL_Rect rect;
    rect.x = 0;
    rect.y = 0;
    rect.w = width;
    rect.h = height;
    SDL_Event event;

    AVPacket packet;
    while (av_read_frame(pFormatContext, &packet) >= 0)
    {
        if (packet.stream_index == videoStreamIndex)
        {
            int gotFrame;
            if (Decode(pVideoDecoderContext, pFrame, &gotFrame, &packet) < 0)
            {
                OutputDebugString("Error\n");
                return -1;
            }

            if (gotFrame)
            {
                SDL_LockYUVOverlay(bmp);

                AVPicture pict;
                pict.data[0] = bmp->pixels[0];
                pict.data[1] = bmp->pixels[2];
                pict.data[2] = bmp->pixels[1];

                pict.linesize[0] = bmp->pitches[0];
                pict.linesize[1] = bmp->pitches[2];
                pict.linesize[2] = bmp->pitches[1];
                // Convert the image from its native format to RGB
                sws_scale(pSwsContext, pFrame->data,
                    pFrame->linesize,
                    0,
                    height,
                    pict.data,
                    pict.linesize);

                SDL_UnlockYUVOverlay(bmp);
                SDL_DisplayYUVOverlay(bmp, &rect);
            }
        }

        // Free the packet that was allocated by av_read_frame
        av_free_packet(&packet);

        SDL_PollEvent(&event);
        switch (event.type) {
        case SDL_QUIT:
            SDL_Quit();
            exit(0);
            break;
        default:
            break;
        }
    }

    /// Clean up
    avcodec_free_context(&pVideoDecoderContext);
    avformat_close_input(&pFormatContext);
    av_frame_free(&pFrame);
    system("pause");
    return 0;
}

void SaveFrame(uint8_t *const data[], const int linesize[], int width, int height, int iFrame)
{
    FILE *pFile;
    char szFilename[32];

    // Open file
    sprintf(szFilename, "frame%d.ppm", iFrame);
    pFile = fopen(szFilename, "wb");
    if (pFile == nullptr)
    {
        OutputDebugString("Couldn't open file to write frame!\n");
        return;
    }

    // Write header
    fprintf(pFile, "P6\n%d %d\n255\n", width, height);

    // Write pixel data
    for (int y = 0; y < height; y++)
    {
        fwrite(data[0] + y * linesize[0], 1, width * 3, pFile);
    }

    fclose(pFile);
}

int OpenCodecContext(int * streamIndex,
    AVCodecContext **ppDecoderContext,
    AVFormatContext * pFormatContext,
    AVMediaType type
)
{
    int ret = av_find_best_stream(pFormatContext, type, -1, -1, nullptr, 0);
    if (ret < 0)
    {
        std::string err = "Could not find " + std::string(av_get_media_type_string(type)) + " in input file\n";
        OutputDebugString(err.c_str());
        return ret;
    }
    *streamIndex = ret;
    AVStream *pStream = pFormatContext->streams[*streamIndex];

    // Find decoder for the stream
    AVCodecContext *pCodecContext = pStream->codec;
    AVCodec *pCodec = avcodec_find_decoder(pStream->codecpar->codec_id);
    if (pCodec == nullptr)
    {
        std::string err = "Failed to find " + std::string(av_get_media_type_string(type)) + " codec\n";
        OutputDebugString(err.c_str());
        return AVERROR(EINVAL);
    }

    // Allocate a codec context for the decoder
    *ppDecoderContext = avcodec_alloc_context3(pCodec);
    if (*ppDecoderContext == nullptr)
    {
        std::string err = "Failed to allocate the " + std::string(av_get_media_type_string(type)) + " codec context\n";
        OutputDebugString(err.c_str());
        return AVERROR(ENOMEM);
    }

    // Copy codec parameters from input stream to output codec context
    if ((ret = avcodec_parameters_to_context(*ppDecoderContext, pStream->codecpar)) < 0)
    {
        std::string err = "Failed to copy " + std::string(av_get_media_type_string(type)) +
            " codec parameters to decoder context\n";
        OutputDebugString(err.c_str());
        return ret;
    }

    // Init the decoder
    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "refcounted_frames", "0", 0);
    ret = avcodec_open2(*ppDecoderContext, pCodec, &opts);
    if (ret < 0)
    {
        std::string err = "Failed to open " + std::string(av_get_media_type_string(type)) + " codec\n";
        OutputDebugString(err.c_str());
        return ret;
    }

    return 0;
}

int Decode(AVCodecContext * pCodecContext, AVFrame * pFrame, int * gotFrame, AVPacket * pPacket)
{
    int ret;
    *gotFrame = 0;

    if (pPacket != nullptr)
    {
        ret = avcodec_send_packet(pCodecContext, pPacket);
        if (ret < 0)
        {
            return ret == AVERROR_EOF ? 0 : ret;
        }
    }

    ret = avcodec_receive_frame(pCodecContext, pFrame);
    if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
    {
        return ret;
    }
    if (ret >= 0)
    {
        *gotFrame = 1;
    }

    return 0;
}
